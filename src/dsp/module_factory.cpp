#include "module.hpp"

#include "dynamic_high_pass_filter.hpp"
#include "dynamic_low_pass_filter.hpp"
#include "echo_effect.hpp"
#include "eq_three_band.hpp"
#include "graph_modules.hpp"
#include "guitar_pedals.hpp"
#include "mixer.hpp"
#include "noise_gate.hpp"
#include "preamp.hpp"
#include "simple_reverb.hpp"
#include "valve_compressor.hpp"
#include "wav_recorder.hpp"

#include <algorithm>

void Module::apply_controls(const Module& from) {
    if (name() != from.name()) {
        return;
    }
    // Round-trip via Module::save/load — keeps GraphProcessor free of concrete types.
    load(from.save());
}

int Module::max_inputs() const {
    switch (role()) {
    case Role::source:
        return 0;
    case Role::multi_in:
        if (name() == "mixer") {
            return 8;
        }
        if (name() == "dry_wet_mix") {
            return 2;
        }
        return 1;
    default:
        return 1;
    }
}

std::unique_ptr<Module> make_runtime_module(const std::string& module_name, float sample_rate) {
    const size_t delay_cap = static_cast<size_t>(std::max(1.0f, sample_rate)) + 1;
    if (module_name == "audio_input") {
        return std::make_unique<AudioInput>();
    }
    if (module_name == "wav_player") {
        return std::make_unique<WavPlayer>();
    }
    if (module_name == "chord_generator") {
        return std::make_unique<ChordGenerator>();
    }
    if (module_name == "white_noise") {
        return std::make_unique<WhiteNoise>();
    }
    if (module_name == "echo") {
        return std::make_unique<EchoEffect>(delay_cap);
    }
    if (module_name == "preamp") {
        return std::make_unique<Preamp>();
    }
    if (module_name == "high_pass") {
        return std::make_unique<DynamicHighPassFilter>();
    }
    if (module_name == "low_pass") {
        return std::make_unique<DynamicLowPassFilter>();
    }
    if (module_name == "drive_pedal") {
        return std::make_unique<guitar_pedals::DrivePedal>();
    }
    if (module_name == "compressor") {
        return std::make_unique<ValveCompressor>();
    }
    if (module_name == "noise_gate") {
        return std::make_unique<NoiseGate>();
    }
    if (module_name == "eq_three_band") {
        return std::make_unique<EqThreeBand>();
    }
    if (module_name == "reverb") {
        return std::make_unique<SimpleReverb>();
    }
    if (module_name == "vu_meter") {
        return std::make_unique<VuMeter>();
    }
    if (module_name == "oscilloscope") {
        return std::make_unique<Oscilloscope>();
    }
    if (module_name == "spectrum") {
        return std::make_unique<Spectrum>();
    }
    if (module_name == "pitch_detector") {
        return std::make_unique<PitchDetectorModule>();
    }
    if (module_name == "mixer") {
        return std::make_unique<Mixer>();
    }
    if (module_name == "dry_wet_mix") {
        return std::make_unique<DryWetMix>();
    }
    if (module_name == "wav_recorder") {
        return std::make_unique<WavRecorder>();
    }
    if (module_name == "output") {
        return std::make_unique<AudioOutput>();
    }
    return nullptr;
}
