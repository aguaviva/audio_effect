#pragma once

#include "dsp/chord_catalog.hpp"
#include "dsp/dynamic_high_pass_filter.hpp"
#include "dsp/dynamic_low_pass_filter.hpp"
#include "dsp/echo_effect.hpp"
#include "dsp/eq_three_band.hpp"
#include "dsp/guitar_pedals.hpp"
#include "dsp/module_json.hpp"
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

// Per-node modules. DSP-backed effects: name() on the DSP class, write/read on
// Params. Graph-only modules keep name/write/read here.
// Graph load matches JSON "type" to the DSP/module name(), then fills via read().
namespace node_params {

struct None {
    static std::string name() { return "none"; }

    void write(std::string& /*out*/, bool& /*first*/) const {}
    bool read(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) {
        return false;
    }
};

struct WavPlayer {
    bool loop = true;
    std::string path;

    static std::string name() { return "wav_player"; }

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
};

struct AudioInput {
    std::string device = "default";

    static std::string name() { return "audio_input"; }

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
};

struct AudioOutput {
    std::string device = "default";

    static std::string name() { return "output"; }

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
};

struct Chord {
    int index = 0;

    static std::string name() { return "chord_generator"; }

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
};

struct WhiteNoise {
    float level = 0.2f;

    static std::string name() { return "white_noise"; }

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
};

// DSP-backed modules: Params live on the DSP class.
using Echo = EchoEffect::Params;
using Preamp = ::Preamp::Params;
using HighPass = DynamicHighPassFilter::Params;
using LowPass = DynamicLowPassFilter::Params;
using Drive = guitar_pedals::DrivePedal::Params;
using Compressor = ValveCompressor::Params;
using NoiseGate = ::NoiseGate::Params;
using EqThreeBand = ::EqThreeBand::Params;
using Reverb = SimpleReverb::Params;
using WavRecorder = ::WavRecorder::Params;

struct DryWet {
    float mix = 0.5f;

    static std::string name() { return "dry_wet_mix"; }

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
};

struct VuMeter {
    float display_gain = 1.0f;
    float decay = 0.85f;

    static std::string name() { return "vu_meter"; }

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
};

struct Oscilloscope {
    float display_gain = 1.0f;

    static std::string name() { return "oscilloscope"; }

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
};

struct Spectrum {
    float display_gain = 1.0f;

    static std::string name() { return "spectrum"; }

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
};

struct PitchDetector {
    static std::string name() { return "pitch_detector"; }

    void write(std::string& /*out*/, bool& /*first*/) const {}
    bool read(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) {
        return false;
    }
};

struct Mixer {
    static std::string name() { return "mixer"; }

    void write(std::string& /*out*/, bool& /*first*/) const {}
    bool read(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) {
        return false;
    }
};

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
};
