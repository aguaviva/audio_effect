#pragma once

#include "module_json.hpp"

#include <atomic>
#include <cstddef>
#include <string>

class DynamicHighPassFilter {
public:
    // Controllable module state: name() + write/read (JSON assembled outside).
    struct Params {
        float cutoff_hz = 70.0f;

        static std::string name() { return "high_pass"; }

        void write(std::string& out, bool& first) const;
        bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    };

    // samples: normalized interleaved stereo values, modified in place.
    // num_samples: number of float values, not stereo frames.
    // cutoff: raise in Hz to remove more bass and rumble.
    // sample_rate: audio samples per second.
    void process(float* samples, size_t num_samples, float cutoff, float sample_rate);
    void reset();
    static void render(std::atomic<bool>& enabled, std::atomic<float>& cutoff_hz,
                       int& selected_preset, bool& controls_changed);

private:
    float prev_input_l_{0.0f};
    float prev_input_r_{0.0f};
    float prev_output_l_{0.0f};
    float prev_output_r_{0.0f};
};
