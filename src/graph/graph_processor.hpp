#pragma once

#include "audio_graph.hpp"

#include "dsp/audio_input_sources.hpp"
#include "dsp/dynamic_high_pass_filter.hpp"
#include "dsp/dynamic_low_pass_filter.hpp"
#include "dsp/echo_effect.hpp"
#include "dsp/eq_three_band.hpp"
#include "dsp/graph_modules.hpp"
#include "dsp/guitar_pedals.hpp"
#include "dsp/mixer.hpp"
#include "dsp/noise_gate.hpp"
#include "dsp/preamp.hpp"
#include "dsp/simple_reverb.hpp"
#include "dsp/valve_compressor.hpp"
#include "dsp/wav_generator.hpp"
#include "dsp/wav_recorder.hpp"
#include "visualizer_bus.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class GraphProcessor {
public:
    explicit GraphProcessor(float sample_rate);

    void set_block_size(size_t block_size);
    size_t block_size() const { return block_size_; }

    // Optional fallback clip when a WavPlayer has no path / failed load.
    void set_loaded_wav(std::shared_ptr<const WavClip> clip);
    void set_record_path(const std::string& path);

    bool process(const CompiledGraph& graph,
                 const int32_t* live_pcm,
                 float* out_float,
                 size_t block_size);

private:
    struct NodeRuntime {
        NodeType type = NodeType::preamp;
        EchoEffect echo;
        Preamp preamp;
        DynamicHighPassFilter high_pass;
        DynamicLowPassFilter low_pass;
        guitar_pedals::DrivePedal drive;
        ValveCompressor compressor;
        NoiseGate gate;
        EqThreeBand eq;
        SimpleReverb reverb;
        WhiteNoise white_noise;
        DryWetMix dry_wet;
        VuMeter vu_meter;
        Oscilloscope oscilloscope;
        Spectrum spectrum;
        PitchDetectorModule pitch_detector;
        Mixer mixer;
        WavFileInput wav;
        ChordGeneratorInput chord;
        WavRecorderEngine recorder;
        std::shared_ptr<const WavClip> wav_clip;
        std::string wav_path;
        std::vector<float> buffer;

        explicit NodeRuntime(size_t max_delay_samples)
            : echo(max_delay_samples) {}
    };

    std::shared_ptr<const WavClip> clip_for_path(const std::string& path);

    void ensure_state(const CompiledGraph& graph);
    void mix_inputs(const CompiledGraph::Node& node,
                    const std::unordered_map<int, std::vector<float>*>& buffers,
                    float* destination) const;
    void run_effect_or_io(NodeRuntime& state,
                          const CompiledGraph::Node& node,
                          const int32_t* live_pcm,
                          float* block);

    float sample_rate_;
    size_t block_size_ = 0;
    std::vector<int32_t> source_scratch_;
    std::shared_ptr<const WavClip> wav_clip_;
    std::unordered_map<std::string, std::shared_ptr<const WavClip>> wav_cache_;
    std::string record_path_;
    std::unordered_map<int, std::unique_ptr<NodeRuntime>> states_;
};
