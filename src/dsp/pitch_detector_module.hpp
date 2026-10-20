#pragma once

#include "module.hpp"

#include <string>

class PitchDetectorModule : public Module {
public:
    std::string name() const override { return "pitch_detector"; }
    std::string save() const override { return "{}"; }
    bool load(const std::string& /*json*/) override { return true; }
    bool render() override { return false; }
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;
};
