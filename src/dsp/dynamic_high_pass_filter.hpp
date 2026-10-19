#pragma once

#include "module_json.hpp"

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

    Params params;
    bool enabled = true;
    int selected_preset = 0;

    // samples: normalized interleaved stereo values, modified in place.
    // num_samples: number of float values, not stereo frames.
    // sample_rate: audio samples per second.
    // Uses params.cutoff_hz.
    void process(float* samples, size_t num_samples, float sample_rate);
    void reset();

    // ImGui controls; edits enabled / selected_preset / params. Returns true if changed.
    bool render();

private:
    float prev_input_l_{0.0f};
    float prev_input_r_{0.0f};
    float prev_output_l_{0.0f};
    float prev_output_r_{0.0f};
};
