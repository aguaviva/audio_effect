#pragma once

#include "dsp/chord_catalog.hpp"
#include "node_types.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

// Per-node parameter bags. Public fields; each type serializes its own keys.
// Graph load/save embeds NodeState::write_params / read_params JSON objects.
// kind: 0 = number, 1 = bool, 2 = string (str populated).
namespace node_params {

struct None {
    void write(std::string& /*out*/, bool& /*first*/) const {}
    bool read(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) {
        return false;
    }
    void from_legacy(const float*, const int*, const bool*) {}
};

struct WavPlayer {
    bool loop = true;
    std::string path; // WAV file for this player node
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct AudioInput {
    std::string device = "default"; // capture device id ("default" or backend name)
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct AudioOutput {
    std::string device = "default"; // playback device id ("default" or backend name)
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Chord {
    int index = 0;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct WhiteNoise {
    float level = 0.2f; // linear amplitude 0..1
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Echo {
    float delay_ms = 320.0f;
    float feedback = 0.35f;
    float mix = 0.25f;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Preamp {
    float gain_db = 0.0f;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct CutoffFilter {
    float cutoff_hz = 1000.0f;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Drive {
    int model = 0;
    float amount = 3.0f;
    float tone = 0.65f;
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
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct NoiseGate {
    float threshold_db = -40.0f;
    float attack_ms = 5.0f;
    float release_ms = 80.0f;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct EqThreeBand {
    float low_db = 0.0f;
    float mid_db = 0.0f;
    float high_db = 0.0f;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct Reverb {
    float room = 0.55f;
    float damping = 0.4f;
    float mix = 0.25f;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct DryWet {
    float mix = 0.5f;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct WavRecorder {
    bool armed = false;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct VuMeter {
    float display_gain = 1.0f;
    float decay = 0.85f;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

struct DisplayGain {
    float display_gain = 1.0f;
    void write(std::string& out, bool& first) const;
    bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    void from_legacy(const float* f, const int* i, const bool* b);
};

using Variant = std::variant<None, WavPlayer, AudioInput, AudioOutput, Chord, WhiteNoise, Echo,
                             Preamp, CutoffFilter, Drive, Compressor, NoiseGate, EqThreeBand,
                             Reverb, DryWet, WavRecorder, VuMeter, DisplayGain>;

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

    // JSON object: {"bypassed":false,"gain_db":0,...}
    std::string write_params() const;

    // Parse a JSON object starting at text[*i] (must be '{'). Advances *i.
    bool read_params(NodeType type, const std::string& text, size_t& i);

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
