#pragma once

#include "module_json.hpp"

#include <atomic>
#include <cstddef>
#include <string>

class DynamicLowPassFilter {
public:
    // Controllable module state: name() + write/read (JSON assembled outside).
    struct Params {
        float cutoff_hz = 8000.0f;

        static std::string name() { return "low_pass"; }

        void write(std::string& out, bool& first) const;
        bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    };

    Params params;

    // samples: normalized interleaved stereo values, modified in place.
    // num_samples: number of float values, not stereo frames.
    // sample_rate: audio samples per second.
    // Uses params.cutoff_hz.
    void process(float* samples, size_t num_samples, float sample_rate);
    void reset();
    static void render(std::atomic<bool>& enabled, std::atomic<float>& cutoff_hz,
                       int& selected_preset, bool& controls_changed);

private:
    float prev_l_{0.0f};
    float prev_r_{0.0f};
};
