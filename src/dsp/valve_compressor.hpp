#pragma once

#include "module_json.hpp"

#include <atomic>
#include <cstddef>
#include <string>

class ValveCompressor {
public:
    static std::string name() { return "compressor"; }

    struct Params {
        float threshold_db = -18.0f;
        float ratio = 3.0f;
        float attack_ms = 15.0f;
        float release_ms = 100.0f;
        float drive = 1.25f;
        float makeup_db = 0.0f;

        void write(std::string& out, bool& first) const;
        bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    };

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
