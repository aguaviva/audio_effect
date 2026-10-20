#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

class DynamicHighPassFilter : public Module {
public:
    float cutoff_hz = 70.0f;
    int selected_preset = 0; // 0 = Custom

    std::string name() const override { return "high_pass"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;

    void reset();

private:
    float prev_input_l_{0.0f};
    float prev_input_r_{0.0f};
    float prev_output_l_{0.0f};
    float prev_output_r_{0.0f};
};
