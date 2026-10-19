#include "graph_processor.hpp"

#include "node_types.hpp"

#include <algorithm>
#include <cstring>

GraphProcessor::GraphProcessor(float sample_rate)
    : sample_rate_(sample_rate) {}

void GraphProcessor::set_block_size(size_t block_size) {
    if (block_size_ == block_size) {
        return;
    }
    block_size_ = block_size;
    for (auto& [id, buffer] : buffers_) {
        (void)id;
        buffer.assign(block_size_, 0.0f);
    }
}

void GraphProcessor::set_loaded_wav(std::shared_ptr<const WavClip> clip) {
    wav_clip_ = std::move(clip);
}

void GraphProcessor::set_record_path(const std::string& path) {
    record_path_ = path;
}

std::shared_ptr<const WavClip> GraphProcessor::load_wav(const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }
    auto it = wav_cache_.find(path);
    if (it != wav_cache_.end()) {
        return it->second;
    }
    std::string error;
    auto clip = WavGenerator::load(path, static_cast<uint32_t>(sample_rate_), error);
    if (clip) {
        wav_cache_[path] = clip;
    }
    return clip;
}

void GraphProcessor::analyze(int node_id, const std::string& module_name, const float* block,
                             size_t num_samples, float display_gain, float vu_decay) {
    NodeType type = NodeType::count;
    if (!node_params::try_resolve_name(module_name, type)) {
        return;
    }
    g_visualizer_bus.analyze(node_id, type, block, num_samples, sample_rate_, display_gain,
                             vu_decay);
}

void GraphProcessor::ensure_buffers(const CompiledGraph& graph) {
    assert(block_size_ > 0 && "graph block size must be set before process");

    for (const auto& node : graph.order) {
        auto& buffer = buffers_[node.id()];
        if (buffer.size() != block_size_) {
            buffer.assign(block_size_, 0.0f);
        }
    }
    if (buffers_.size() == graph.order.size()) {
        return;
    }
    for (auto it = buffers_.begin(); it != buffers_.end();) {
        if (!graph.index_by_id.count(it->first)) {
            it = buffers_.erase(it);
        } else {
            ++it;
        }
    }
}

void GraphProcessor::gather_inputs(const CompiledGraph::Node& node,
                                   const std::unordered_map<int, std::vector<float>*>& buffers,
                                   const float** out_inputs, size_t max_inputs,
                                   size_t& out_count) const {
    out_count = 0;
    // Slot-indexed for mixer / dry-wet; append order for simple effects.
    const bool slotted = node.state().module().role() == Module::Role::multi_in;
    if (slotted) {
        for (size_t i = 0; i < max_inputs; ++i) {
            out_inputs[i] = nullptr;
        }
        out_count = max_inputs;
        for (const auto& in : node.inputs()) {
            if (in.to_slot < 0 || static_cast<size_t>(in.to_slot) >= max_inputs) {
                continue;
            }
            auto it = buffers.find(in.from_node);
            if (it == buffers.end() || !it->second || it->second->size() != block_size_) {
                continue;
            }
            out_inputs[static_cast<size_t>(in.to_slot)] = it->second->data();
        }
        return;
    }

    for (const auto& in : node.inputs()) {
        if (out_count >= max_inputs) {
            break;
        }
        auto it = buffers.find(in.from_node);
        if (it == buffers.end() || !it->second || it->second->size() != block_size_) {
            continue;
        }
        out_inputs[out_count++] = it->second->data();
    }
}

void GraphProcessor::sum_inputs(const float* const* inputs, size_t input_count,
                                float* destination) const {
    std::fill(destination, destination + block_size_, 0.0f);
    for (size_t s = 0; s < input_count; ++s) {
        const float* src = inputs[s];
        if (!src) {
            continue;
        }
        for (size_t i = 0; i < block_size_; ++i) {
            destination[i] += src[i];
        }
    }
}

bool GraphProcessor::process(const CompiledGraph& graph,
                             const int32_t* live_pcm,
                             float* out_float,
                             size_t block_size) {
    if (block_size == 0) {
        return false;
    }
    if (block_size_ != block_size) {
        set_block_size(block_size);
    }

    if (!graph.valid || graph.order.empty()) {
        std::fill(out_float, out_float + block_size_, 0.0f);
        return false;
    }

    ensure_buffers(graph);

    std::vector<int> alive_ids;
    alive_ids.reserve(graph.order.size());
    std::unordered_map<int, std::vector<float>*> buffers;
    buffers.reserve(graph.order.size());
    for (const auto& node : graph.order) {
        buffers[node.id()] = &buffers_[node.id()];
        alive_ids.push_back(node.id());
    }
    g_visualizer_bus.retain_only(alive_ids);

    bool wrote_output = false;
    std::fill(out_float, out_float + block_size_, 0.0f);

    const float* input_ptrs[kMaxMixerInputs]{};
    for (const auto& node : graph.order) {
        Module& mod = node.state().shared_module();
        float* block = buffers_[node.id()].data();
        const bool bypassed = mod.supports_bypass() && node.state().bypassed;

        size_t input_count = 0;
        gather_inputs(node, buffers, input_ptrs, static_cast<size_t>(kMaxMixerInputs),
                      input_count);

        if (mod.role() == Module::Role::multi_in) {
            std::fill(block, block + block_size_, 0.0f);
        } else if (!mod.is_source()) {
            sum_inputs(input_ptrs, input_count, block);
        }

        // multi_in modules always run (legacy: blending lived in mix_inputs).
        const bool run = !bypassed || mod.role() == Module::Role::multi_in;
        if (run) {
            ProcessContext ctx;
            ctx.buffer = block;
            ctx.num_samples = block_size_;
            ctx.sample_rate = sample_rate_;
            ctx.inputs = input_ptrs;
            ctx.input_count = input_count;
            ctx.input_muted = node.state().input_muted.data();
            ctx.live_pcm = live_pcm;
            ctx.node_id = node.id();
            ctx.host = this;
            mod.process(ctx);
        } else if (mod.is_source()) {
            std::fill(block, block + block_size_, 0.0f);
        }

        if (mod.is_output_sink() && !bypassed) {
            for (size_t i = 0; i < block_size_; ++i) {
                out_float[i] += block[i];
            }
            wrote_output = true;
        }
    }

    return wrote_output;
}
