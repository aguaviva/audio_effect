#pragma once

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
#include "dsp/module.hpp"
#include "dsp/noise_gate.hpp"
#include "dsp/none_module.hpp"
#include "dsp/oscilloscope.hpp"
#include "dsp/pitch_detector_module.hpp"
#include "dsp/preamp.hpp"
#include "dsp/simple_reverb.hpp"
#include "dsp/spectrum.hpp"
#include "dsp/valve_compressor.hpp"
#include "dsp/vu_meter.hpp"
#include "dsp/wav_player.hpp"
#include "dsp/wav_recorder.hpp"
#include "dsp/white_noise.hpp"
#include "node_types.hpp"

#include <cstddef>
#include <memory>
#include <string>

// Graph node modules: each concrete type implements Module
// (name / save / load / render). Fields live on the class itself.
namespace node_params {

using None = ::NoneModule;
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
using Agc = ::Agc;
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

    static NodeState make(NodeType type);

    std::string module_name() const;

    // JSON object: {"bypassed":false,"gain_db":0,...}
    std::string write_params() const;

    // Parse a JSON object starting at text[*i] (must be '{'). Advances *i.
    bool read_params(NodeType type, const std::string& text, size_t& i);


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
