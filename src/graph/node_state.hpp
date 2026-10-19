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
#include <string>
#include <variant>

// Graph node modules: each concrete type implements Module
// (name / save / load / render). Fields live on the class itself.
namespace node_params {

struct None : Module {
    std::string name() const override { return "none"; }
    Role role() const override { return Role::effect; }
    void save(std::string& /*out*/, bool& /*first*/) const override {}
    bool load(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) override {
        return false;
    }
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

using Variant = std::variant<None, WavPlayer, AudioInput, AudioOutput, Chord, WhiteNoise, Echo,
                             Preamp, HighPass, LowPass, Drive, Compressor, NoiseGate, EqThreeBand,
                             Reverb, DryWet, WavRecorder, VuMeter, Oscilloscope, Spectrum,
                             PitchDetector, Mixer>;

bool try_resolve_name(const std::string& module_name, NodeType& out);
std::string name_for_type(NodeType type);

} // namespace node_params

struct NodeState {
    bool bypassed = false;
    std::array<bool, kMaxMixerInputs> input_muted{};
    std::array<float, kMaxMixerInputs> input_gain = [] {
        std::array<float, kMaxMixerInputs> gains{};
        gains.fill(1.0f);
        return gains;
    }();

    node_params::Variant params{node_params::None{}};

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
        return std::get_if<T>(&params);
    }
    template <typename T>
    const T* as() const {
        return std::get_if<T>(&params);
    }

    // Polymorphic view of the active module (for GraphProcessor / UI).
    Module& module() {
        return std::visit([](auto& m) -> Module& { return m; }, params);
    }
    const Module& module() const {
        return std::visit([](const auto& m) -> const Module& { return m; }, params);
    }
};
