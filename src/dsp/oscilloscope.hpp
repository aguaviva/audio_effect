#pragma once

#include "module.hpp"

#include <string>

class Oscilloscope : public Module {
public:
    float display_gain = 1.0f;

    std::string name() const override { return "oscilloscope"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;
};
