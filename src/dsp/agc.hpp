#pragma once

#include "module.hpp"

#include <atomic>
#include <cstddef>
#include <string>

// Automatic gain control: steers the RMS level toward target_db. Gain falls at the
// attack rate and recovers at the release rate; below gate_db it is held so pauses
// and background noise are not pumped up.
class Agc : public Module {
public:
    float target_db = -18.0f;
    float max_gain_db = 24.0f;
    float attack_ms = 10.0f;
    float release_ms = 500.0f;
    float gate_db = -60.0f;

    std::string name() const override { return "agc"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;

    float gain_db() const { return shown_gain_db_.load(std::memory_order_relaxed); }

private:
    static constexpr float kMaxCutDb = -40.0f;
    static constexpr float kRmsWindowMs = 30.0f;

    // Audio thread only.
    float power_ = 0.0f;
    float gain_db_ = 0.0f;
    std::atomic<float> shown_gain_db_{0.0f};
};
