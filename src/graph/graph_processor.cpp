#include "graph_processor.hpp"

#include <algorithm>

GraphProcessor::GraphProcessor(float sample_rate)
    : sample_rate_(sample_rate) {}

bool GraphProcessor::process(const CompiledGraph& graph,
                             const float* live_input,
                             float* out_float,
                             size_t block_size) {
    if (block_size == 0) {
        return false;
    }
    block_size_ = block_size;
    std::fill(out_float, out_float + block_size_, 0.0f);
    if (graph.order.empty()) {
        return false;
    }

    const size_t needed = (graph.link_count + 1) * block_size_;
    if (buffers_.size() < needed) {
        buffers_.resize(needed);
    }
    float* scratch = buffer(graph.link_count);
    live_input_ = live_input;

    bool wrote_output = false;
    for (const auto& node : graph.order) {
        Module& mod = *node.module;
        const float* inputs[Module::kMaxInputs] = {};
        for (const auto& in : node.inputs) {
            inputs[in.to_slot] = buffer(static_cast<size_t>(in.link));
        }
        const int input_count = mod.input_count();

        const auto& links_out = node.outputs;
        float* out = links_out.empty() ? scratch : buffer(static_cast<size_t>(links_out.front()));

        if (mod.supports_bypass() && node.bypassed) {
            copy_input(inputs, input_count, 0, out, block_size_);
        } else {
            mod.host_ = this;
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
