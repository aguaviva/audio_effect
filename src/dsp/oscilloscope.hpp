#pragma once

#include "module.hpp"

#include <mutex>
#include <string>

class Oscilloscope : public Module {
public:
    static constexpr int kPoints = 128;

    float display_gain = 1.0f;

    std::string name() const override { return "oscilloscope"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* const* outputs, size_t sample_count,
                 float sample_rate) override;

private:
    // The audio thread only try_locks, so drawing never stalls audio.
    std::mutex points_mutex_;
    float points_[kPoints]{};
};
