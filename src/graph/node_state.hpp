#pragma once

#include "dsp/chord_catalog.hpp"
#include "node_types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

// Per-node modules. Each type owns:
//   - name()           stable JSON type id
//   - save_json()      serialize module fields to a JSON object
//   - load_json()      deserialize module fields from a JSON object
// Graph load resolves "type" by matching name() across the registry, then
// calls that module's load path (via NodeState::load_by_module_name).
namespace node_params {

// kind: 0 = number, 1 = bool, 2 = string (str populated).
struct None {
    static std::string name() { return "none"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& /*out*/, bool& /*first*/) const {}
    bool read(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) {
        return false;
    }
    void from_legacy(const float*, const int*, const bool*) {}
};

struct WavPlayer {
    bool loop = true;
    std::string path;

    static std::string name() { return "wav_player"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct AudioInput {
    std::string device = "default";

    static std::string name() { return "audio_input"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct AudioOutput {
    std::string device = "default";

    static std::string name() { return "output"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Chord {
    int index = 0;

    static std::string name() { return "chord_generator"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct WhiteNoise {
    float level = 0.2f;

    static std::string name() { return "white_noise"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Echo {
    float delay_ms = 320.0f;
    float feedback = 0.35f;
    float mix = 0.25f;

    static std::string name() { return "echo"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Preamp {
    float gain_db = 0.0f;

    static std::string name() { return "preamp"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct HighPass {
    float cutoff_hz = 70.0f;

    static std::string name() { return "high_pass"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct LowPass {
    float cutoff_hz = 8000.0f;

    static std::string name() { return "low_pass"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Drive {
    int model = 0;
    float amount = 3.0f;
    float tone = 0.65f;

    static std::string name() { return "drive_pedal"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Compressor {
    float threshold_db = -18.0f;
    float ratio = 3.0f;
    float attack_ms = 15.0f;
    float release_ms = 100.0f;
    float drive = 1.25f;
    float makeup_db = 0.0f;

    static std::string name() { return "compressor"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct NoiseGate {
    float threshold_db = -40.0f;
    float attack_ms = 5.0f;
    float release_ms = 80.0f;

    static std::string name() { return "noise_gate"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct EqThreeBand {
    float low_db = 0.0f;
    float mid_db = 0.0f;
    float high_db = 0.0f;

    static std::string name() { return "eq_three_band"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Reverb {
    float room = 0.55f;
    float damping = 0.4f;
    float mix = 0.25f;

    static std::string name() { return "reverb"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct DryWet {
    float mix = 0.5f;

    static std::string name() { return "dry_wet_mix"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct WavRecorder {
    bool armed = false;

    static std::string name() { return "wav_recorder"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct VuMeter {
    float display_gain = 1.0f;
    float decay = 0.85f;

    static std::string name() { return "vu_meter"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Oscilloscope {
    float display_gain = 1.0f;

    static std::string name() { return "oscilloscope"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Spectrum {
    float display_gain = 1.0f;

    static std::string name() { return "spectrum"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct PitchDetector {
    static std::string name() { return "pitch_detector"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& /*out*/, bool& /*first*/) const {}
    bool read(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) {
        return false;
    }
    void from_legacy(const float*, const int*, const bool*) {}
};

struct Mixer {
    static std::string name() { return "mixer"; }
    std::string save_json() const;
    bool load_json(const std::string& text, size_t& i);

    void write(std::string& /*out*/, bool& /*first*/) const {}
    bool read(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) {
        return false;
    }
    void from_legacy(const float*, const int*, const bool*) {}
};

using Variant = std::variant<None, WavPlayer, AudioInput, AudioOutput, Chord, WhiteNoise, Echo,
                             Preamp, HighPass, LowPass, Drive, Compressor, NoiseGate, EqThreeBand,
                             Reverb, DryWet, WavRecorder, VuMeter, Oscilloscope, Spectrum,
                             PitchDetector, Mixer>;

// Match a JSON "type" string against every module's name(); set out on hit.
bool try_resolve_name(const std::string& module_name, NodeType& out);
std::string name_for_type(NodeType type);

} // namespace node_params

struct NodeState {
    bool bypassed = false;
    // Per input-slot mute (index = to_slot). Unused slots stay false.
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

    // Active module's name() (JSON type id).
    std::string module_name() const;

    // JSON object: {"bypassed":false,"gain_db":0,...}
    std::string write_params() const;

    // Parse a JSON object starting at text[*i] (must be '{'). Advances *i.
    bool read_params(NodeType type, const std::string& text, size_t& i);

    // Resolve module_name against available modules, then load with that module's path.
    bool load_by_module_name(const std::string& module_name, const std::string& text, size_t& i);

    // Legacy f[8]/i[4]/b[4] bag from older patches.
    void from_legacy_arrays(NodeType type, const float* f, const int* i, const bool* b);

    template <typename T>
    T* as() {
        return std::get_if<T>(&params);
    }
    template <typename T>
    const T* as() const {
        return std::get_if<T>(&params);
    }
};
