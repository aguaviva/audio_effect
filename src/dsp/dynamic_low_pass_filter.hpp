#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

class DynamicLowPassFilter : public Module {
public:
    float cutoff_hz = 8000.0f;
    int selected_preset = 0; // 0 = Custom

    std::string name() const override { return "low_pass"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;

    void process(float* samples, size_t num_samples, float sample_rate);
    void reset();

private:
    float prev_l_{0.0f};
    float prev_r_{0.0f};
};
