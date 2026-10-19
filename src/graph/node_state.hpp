#pragma once

#include "dsp/dynamic_high_pass_filter.hpp"
#include "dsp/dynamic_low_pass_filter.hpp"
#include "dsp/echo_effect.hpp"
#include "dsp/eq_three_band.hpp"
#include "dsp/graph_modules.hpp"
#include "dsp/guitar_pedals.hpp"
#include "dsp/mixer.hpp"
#include "dsp/module.hpp"
#include "dsp/noise_gate.hpp"
#include "dsp/preamp.hpp"
#include "dsp/simple_reverb.hpp"
#include "dsp/valve_compressor.hpp"
#include "dsp/wav_recorder.hpp"
#include "node_types.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>

// Graph node modules: each concrete type implements Module
// (name / save / load / render). Fields live on the class itself.
namespace node_params {

struct None : Module {
    std::string name() const override { return "none"; }
    Role role() const override { return Role::effect; }
    std::string save() const override { return "{}"; }
    bool load(const std::string& /*json*/) override { return true; }
    bool render() override { return false; }
    void process(ProcessContext& /*ctx*/) override {}
};

using WavPlayer = ::WavPlayer;
using AudioInput = ::AudioInput;
using AudioOutput = ::AudioOutput;
using Chord = ::ChordGenerator;
using WhiteNoise = ::WhiteNoise;
using Echo = EchoEffect;
using Preamp = ::Preamp;
using HighPass = DynamicHighPassFilter;
using LowPass = DynamicLowPassFilter;
using Drive = guitar_pedals::DrivePedal;
using Compressor = ValveCompressor;
using NoiseGate = ::NoiseGate;
using EqThreeBand = ::EqThreeBand;
using Reverb = SimpleReverb;
using DryWet = ::DryWetMix;
using WavRecorder = ::WavRecorder;
using VuMeter = ::VuMeter;
using Oscilloscope = ::Oscilloscope;
using Spectrum = ::Spectrum;
using PitchDetector = ::PitchDetectorModule;
using Mixer = ::Mixer; // dsp/mixer.hpp

bool try_resolve_name(const std::string& module_name, NodeType& out);
std::string name_for_type(NodeType type);

} // namespace node_params

// Per-node state. Copies share the same Module instance: the UI renders it and the
// audio thread processes it, so settings never need to be copied between them.
struct NodeState {
    bool bypassed = false;
    std::array<bool, kMaxMixerInputs> input_muted{};

    bool is_input_muted(int slot) const {
        return slot >= 0 && slot < kMaxMixerInputs && input_muted[static_cast<size_t>(slot)];
    }

    static NodeState make(NodeType type);

    std::string module_name() const;

    // JSON object: {"bypassed":false,"gain_db":0,...}
    std::string write_params() const;

    // Parse a JSON object starting at text[*i] (must be '{'). Advances *i.
    bool read_params(NodeType type, const std::string& text, size_t& i);

    // Resolve module_name against available modules, then load via read_params.
    bool load_by_module_name(const std::string& module_name, const std::string& text, size_t& i);

    template <typename T>
    T* as() {
        return dynamic_cast<T*>(module_.get());
    }
    template <typename T>
    const T* as() const {
        return dynamic_cast<const T*>(module_.get());
    }

    Module& module() { return *module_; }
    const Module& module() const { return *module_; }

    // Mutable access for the audio thread, which only holds const snapshots of NodeState.
    Module& shared_module() const { return *module_; }

private:
    std::shared_ptr<Module> module_ = std::make_shared<node_params::None>();
};
