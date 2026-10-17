#pragma once

#include <atomic>
#include <cstddef>

class ValveCompressor {
public:
    // samples: normalized interleaved stereo values, modified in place.
    // num_samples: number of float values, not stereo frames.
    // threshold_db: lower values compress more of the signal.
    // ratio: 1:1 is neutral; larger ratios reduce peaks more strongly.
    // attack_ms/release_ms: how quickly compression engages and relaxes.
    // drive adds asymmetric saturation; makeup_db restores post-compression level.
    // sample_rate: audio samples per second.
    void process(float* samples, size_t num_samples, float threshold_db, float ratio,
                 float attack_ms, float release_ms, float drive, float makeup_db,
                 float sample_rate);
    static void render(std::atomic<bool>& enabled,
                       std::atomic<float>& threshold_db, std::atomic<float>& ratio,
                       std::atomic<float>& attack_ms, std::atomic<float>& release_ms,
                       std::atomic<float>& drive, std::atomic<float>& makeup_db,
                       int& selected_preset, bool& controls_changed,
                       bool& compressor_controls_changed);

private:
    float envelope_{0.0f};
};
