#pragma once

#include "audio_graph.hpp"
#include "dsp/module.hpp"
#include "dsp/wav_generator.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Runs a compiled graph. Knows Module only — no concrete DSP types.
// Each node's Module is the same instance the UI edits; this class owns the link buffers.
class GraphProcessor : public GraphHost {
public:
    explicit GraphProcessor(float sample_rate);

    bool process(const CompiledGraph& graph,
                 const int32_t* live_pcm,
                 float* out_float,
                 size_t block_size);

    // GraphHost
    std::shared_ptr<const WavClip> load_wav(const std::string& path) override;
    const int32_t* live_input() const override { return live_pcm_; }

private:
    float* buffer(size_t index) { return buffers_.data() + index * block_size_; }

    float sample_rate_;
    size_t block_size_ = 0;
    const int32_t* live_pcm_ = nullptr;
    std::unordered_map<std::string, std::shared_ptr<const WavClip>> wav_cache_;
    // One block per link, plus one scratch block for nodes with no outgoing links.
    std::vector<float> buffers_;
};
