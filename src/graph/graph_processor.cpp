#include "graph_processor.hpp"

#include "node_types.hpp"

#include <algorithm>

GraphProcessor::GraphProcessor(float sample_rate)
    : sample_rate_(sample_rate) {}

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

bool GraphProcessor::process(const CompiledGraph& graph,
                             const int32_t* live_pcm,
                             float* out_float,
                             size_t block_size) {
    if (block_size == 0) {
        return false;
    }
    block_size_ = block_size;
    std::fill(out_float, out_float + block_size_, 0.0f);
    if (!graph.valid || graph.order.empty()) {
        return false;
    }

    const size_t needed = (graph.link_count + 1) * block_size_;
    if (buffers_.size() < needed) {
        buffers_.resize(needed);
    }
    float* scratch = buffer(graph.link_count);
    live_pcm_ = live_pcm;

    std::vector<int> alive_ids;
    alive_ids.reserve(graph.order.size());
    for (const auto& node : graph.order) {
        alive_ids.push_back(node.id());
    }
    g_visualizer_bus.retain_only(alive_ids);

    bool wrote_output = false;
    for (const auto& node : graph.order) {
        const float* inputs[kMaxMixerInputs] = {};
        for (const auto& in : node.inputs()) {
            inputs[in.to_slot] = buffer(static_cast<size_t>(in.link));
        }
        const int input_count = node_input_slot_limit(node.type());

        const auto& links_out = node.outputs();
        float* out = links_out.empty() ? scratch : buffer(static_cast<size_t>(links_out.front()));

        Module& mod = node.state().shared_module();
        if (mod.supports_bypass() && node.state().bypassed) {
            copy_input(inputs, input_count, 0, out, block_size_);
        } else {
            mod.host_ = this;
            mod.node_id_ = node.id();
            mod.process(inputs, input_count, out, block_size_, sample_rate_);
        }
        for (size_t k = 1; k < links_out.size(); ++k) {
            std::copy(out, out + block_size_, buffer(static_cast<size_t>(links_out[k])));
        }

        if (mod.is_output_sink()) {
            for (size_t i = 0; i < block_size_; ++i) {
                out_float[i] += out[i];
            }
            wrote_output = true;
        }
    }

    return wrote_output;
}
