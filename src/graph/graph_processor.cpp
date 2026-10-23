#include "graph_processor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

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

    const size_t needed = (graph.buffer_count + 1) * block_size_;
    if (buffers_.size() < needed) {
        buffers_.resize(needed);
    }
    float* sink_block = buffer(graph.buffer_count);
    live_input_ = live_input;
    const float release = std::exp(-static_cast<float>(block_size_ / 2) / (0.25f * sample_rate_));

    bool wrote_output = false;
    for (const auto& node : graph.order) {
        Module& mod = *node.module;
        const float* inputs[Module::kMaxInputs] = {};
        for (const auto& in : node.inputs) {
            inputs[in.to_slot] = buffer(static_cast<size_t>(in.buffer));
        }
        const int input_count = mod.input_count();

        // Each output pin writes the buffer of its first link; its other links get copies.
        for (int k = 0; k < Module::kMaxOutputs; ++k) {
            mod.set_output_buffer(k, nullptr);
        }
        for (const auto& out : node.outputs) {
            if (!mod.output_buffer(out.from_slot)) {
                mod.set_output_buffer(out.from_slot, buffer(static_cast<size_t>(out.buffer)));
            }
        }
        if (mod.is_output_sink()) {
            mod.set_output_buffer(0, sink_block);
        }

        const auto start = std::chrono::steady_clock::now();
        if (mod.supports_bypass() && node.bypassed) {
            if (float* out = mod.output_buffer(0)) {
                copy_input(inputs, input_count, 0, out, block_size_);
            }
            for (int k = 1; k < Module::kMaxOutputs; ++k) {
                if (float* out = mod.output_buffer(k)) {
                    std::fill(out, out + block_size_, 0.0f);
                }
            }
        } else {
            mod.host_ = this;
            mod.process(inputs, input_count, block_size_, sample_rate_);
        }
        const float us = std::chrono::duration<float, std::micro>(
                             std::chrono::steady_clock::now() - start).count();
        const float smoothed = mod.process_us_.load(std::memory_order_relaxed);
        mod.process_us_.store(smoothed + 0.02f * (us - smoothed), std::memory_order_relaxed);

        for (const auto& out : node.outputs) {
            const float* written = mod.output_buffer(out.from_slot);
            float* copy = buffer(static_cast<size_t>(out.buffer));
            if (copy != written) {
                std::copy(written, written + block_size_, copy);
            }
        }

        const int pins = std::min(mod.output_count(), Module::kMaxOutputs);
        for (int k = 0; k < pins; ++k) {
            const float* out = mod.output_buffer(k);
            float peak = 0.0f;
            for (size_t i = 0; out && i < block_size_; ++i) {
                peak = std::max(peak, std::abs(out[i]));
            }
            auto& level = mod.output_levels_[k];
            level.store(std::max(peak, level.load(std::memory_order_relaxed) * release),
                        std::memory_order_relaxed);
        }

        if (mod.is_output_sink()) {
            for (size_t i = 0; i < block_size_; ++i) {
                out_float[i] += sink_block[i];
            }
            wrote_output = true;
        }
    }

    return wrote_output;
}
