#pragma once

#include "module.hpp"

#include <string>

class DryWetMix : public Module {
public:
    float mix = 0.5f;

    std::string name() const override { return "dry_wet_mix"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    void get_inputs(int *count, const char* const** str) const override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;
};
