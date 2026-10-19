#pragma once

#include "module_json.hpp"

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

class EchoEffect {
public:
    static std::string name() { return "echo"; }

    struct Params {
        float delay_ms = 320.0f;
        float feedback = 0.35f;
        float mix = 0.25f;

        void write(std::string& out, bool& first) const;
        bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    };

    // max_delay_samples: delay-line capacity in samples per channel; allocate at least
    // sample_rate * the longest delay time you intend to use.
    explicit EchoEffect(size_t max_delay_samples);

    // samples: normalized interleaved stereo values, modified in place.
    // sample_count: number of float values, not stereo frames.
    // delay_ms: time before the repeat; feedback controls how many repeats follow.
    // mix: 0 is dry/original only, 1 is delayed signal only.
    // sample_rate: audio samples per second.
    void process(float* samples, size_t sample_count, float delay_ms,
                 float feedback, float mix, float sample_rate);
    void reset();
    static void render(std::atomic<bool>& enabled, std::atomic<float>& delay_ms,
                       std::atomic<float>& feedback, std::atomic<float>& mix,
                       int& selected_preset, bool& controls_changed);

private:
    std::vector<float> delay_left_;
    std::vector<float> delay_right_;
    size_t write_index_{0};
};
