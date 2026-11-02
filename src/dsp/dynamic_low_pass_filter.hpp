#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

class DynamicLowPassFilter : public Module {
public:
    float cutoff_hz = 8000.0f;
    int selected_preset = 0; // 0 = Custom

    std::string name() const override { return "low_pass"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;

private:
    float prev_l_{0.0f};
    float prev_r_{0.0f};
};
