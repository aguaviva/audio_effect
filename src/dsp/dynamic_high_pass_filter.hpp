#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

class DynamicHighPassFilter : public Module {
public:
    float cutoff_hz = 70.0f;
    int selected_preset = 0; // 0 = Custom

    std::string name() const override { return "high_pass"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;

    void process(float* samples, size_t num_samples, float sample_rate);
    void reset();

private:
    float prev_input_l_{0.0f};
    float prev_input_r_{0.0f};
    float prev_output_l_{0.0f};
    float prev_output_r_{0.0f};
};
