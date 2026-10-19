#pragma once

#include "audio_graph.hpp"
#include "dsp/module.hpp"
#include "dsp/wav_generator.hpp"
#include "visualizer_bus.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Runs a compiled graph. Knows Module + ProcessContext only — no concrete DSP types.
class GraphProcessor : public GraphHost {
public:
    explicit GraphProcessor(float sample_rate);

    void set_block_size(size_t block_size);
    size_t block_size() const { return block_size_; }

    void set_loaded_wav(std::shared_ptr<const WavClip> clip);
    void set_record_path(const std::string& path);

    bool process(const CompiledGraph& graph,
                 const int32_t* live_pcm,
                 float* out_float,
                 size_t block_size);

    // GraphHost
    float sample_rate() const override { return sample_rate_; }
    std::shared_ptr<const WavClip> load_wav(const std::string& path) override;
    std::shared_ptr<const WavClip> fallback_wav() const override { return wav_clip_; }
    const std::string& record_path() const override { return record_path_; }
    void analyze(int node_id, const std::string& module_name, const float* block,
                 size_t num_samples, float display_gain, float vu_decay) override;

private:
    struct NodeRuntime {
        std::string module_name;
        std::unique_ptr<Module> module;
        std::vector<float> buffer;
    };

    void ensure_state(const CompiledGraph& graph);
    void gather_inputs(const CompiledGraph::Node& node,
                       const std::unordered_map<int, std::vector<float>*>& buffers,
                       const float** out_inputs, size_t max_inputs, size_t& out_count) const;
    void sum_inputs(const float* const* inputs, size_t input_count, float* destination) const;

    float sample_rate_;
    size_t block_size_ = 0;
    std::shared_ptr<const WavClip> wav_clip_;
    std::unordered_map<std::string, std::shared_ptr<const WavClip>> wav_cache_;
    std::string record_path_;
    std::unordered_map<int, std::unique_ptr<NodeRuntime>> states_;
};
