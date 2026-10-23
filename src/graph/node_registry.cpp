#include "node_registry.hpp"

#include "dsp/agc.hpp"
#include "dsp/audio_input.hpp"
#include "dsp/audio_output.hpp"
#include "dsp/chord_generator.hpp"
#include "dsp/dry_wet_mix.hpp"
#include "dsp/dynamic_high_pass_filter.hpp"
#include "dsp/dynamic_low_pass_filter.hpp"
#include "dsp/echo_effect.hpp"
#include "dsp/eq_three_band.hpp"
#include "dsp/guitar_pedals.hpp"
#include "dsp/mixer.hpp"
#include "dsp/noise_gate.hpp"
#include "dsp/oscilloscope.hpp"
#include "dsp/pitch_detector_module.hpp"
#include "dsp/preamp.hpp"
#include "dsp/simple_reverb.hpp"
#include "dsp/spectrum.hpp"
#include "dsp/timeline.hpp"
#include "dsp/valve_compressor.hpp"
#include "dsp/vu_meter.hpp"
#include "dsp/wav_player.hpp"
#include "dsp/wav_recorder.hpp"
#include "dsp/white_noise.hpp"

namespace {

template <typename T>
std::shared_ptr<Module> make() {
    return std::make_shared<T>();
}

// name() of each kind's module, in node_kinds() order.
const std::vector<std::string>& kind_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const auto& kind : node_kinds()) {
            out.push_back(kind.make()->name());
        }
        return out;
    }();
    return names;
}

} // namespace

const std::vector<NodeKind>& node_kinds() {
    static const std::vector<NodeKind> kinds = {
        {"Audio Input", "Sources", make<AudioInput>},
        {"WAV Player", "Sources", make<WavPlayer>},
        {"Chord Generator", "Sources", make<ChordGenerator>},
        {"White Noise", "Sources", make<WhiteNoise>},
        {"Timeline", "Sources", make<Timeline>},
        {"Echo", "Effects", make<EchoEffect>},
        {"Preamp", "Effects", make<Preamp>},
        {"High-Pass Filter", "Effects", make<DynamicHighPassFilter>},
        {"Drive Pedal", "Effects", make<guitar_pedals::DrivePedal>},
        {"Low-Pass Filter", "Effects", make<DynamicLowPassFilter>},
        {"Valve Compressor", "Effects", make<ValveCompressor>},
        {"Noise Gate", "Effects", make<NoiseGate>},
        {"3-Band EQ", "Effects", make<EqThreeBand>},
        {"Reverb", "Effects", make<SimpleReverb>},
        {"AGC", "Effects", make<Agc>},
        {"Mixer", "Effects", make<Mixer>},
        {"Dry/Wet Mix", "Effects", make<DryWetMix>},
        {"VU Meter", "Visualizers", make<VuMeter>},
        {"Oscilloscope", "Visualizers", make<Oscilloscope>},
        {"Spectrum", "Visualizers", make<Spectrum>},
        {"Pitch Detector", "Visualizers", make<PitchDetectorModule>},
        {"WAV Recorder", "Sinks", make<WavRecorder>},
        {"Output", "Sinks", make<AudioOutput>},
    };
    return kinds;
}

std::shared_ptr<Module> make_module(const std::string& name) {
    const auto& names = kind_names();
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            return node_kinds()[i].make();
        }
    }
    return nullptr;
}

const char* node_label(const Module& module) {
    const auto& names = kind_names();
    const std::string name = module.name();
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) {
            return node_kinds()[i].label;
        }
    }
    return "?";
}
