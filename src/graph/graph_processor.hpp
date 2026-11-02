#pragma once

#include "audio_graph.hpp"
#include "dsp/module.hpp"

#include <memory>
#include <string>
#include <vector>

// Runs a compiled graph. Knows Module only — no concrete DSP types.
// Each node's Module is the same instance the UI edits; this class owns the link buffers.
class GraphProcessor : public GraphHost {
public:
    explicit GraphProcessor(float sample_rate);

    bool process(const CompiledGraph& graph,
                 const float* live_input,
                 float* out_float,
                 size_t block_size);

    // GraphHost
    const float* live_input() const override { return live_input_; }

private:
    float* buffer(size_t index) { return buffers_.data() + index * block_size_; }

    float sample_rate_;
    size_t block_size_ = 0;
    const float* live_input_ = nullptr;
    // One block per link, plus one that sinks write for the device.
    std::vector<float> buffers_;
};
