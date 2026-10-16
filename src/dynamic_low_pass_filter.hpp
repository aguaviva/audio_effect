#pragma once

#include <atomic>
#include <cstddef>

class DynamicLowPassFilter {
public:
    // samples: normalized interleaved stereo values, modified in place.
    // num_samples: number of float values, not stereo frames.
    // cutoff: lower in Hz to reduce more treble; raise for a brighter sound.
    // sample_rate: audio samples per second.
    void process(float* samples, size_t num_samples, float cutoff, float sample_rate);
    void reset();
    static void render(std::atomic<bool>& enabled, std::atomic<float>& cutoff_hz,
                       int& selected_preset, bool& controls_changed);

private:
    float prev_l_{0.0f};
    float prev_r_{0.0f};
};
