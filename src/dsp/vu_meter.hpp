#pragma once

#include "module.hpp"

#include <atomic>
#include <string>

class VuMeter : public Module {
public:
    float display_gain = 1.0f;
    float decay = 0.85f;

    std::string name() const override { return "vu_meter"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* const* outputs, size_t sample_count,
                 float sample_rate) override;

private:
    // Audio thread only.
    float peak_hold_ = 0.0f;
    float rms_hold_ = 0.0f;
    // Written by the audio thread, drawn by the UI thread.
    std::atomic<float> peak_{0.0f};
    std::atomic<float> rms_{0.0f};
};
