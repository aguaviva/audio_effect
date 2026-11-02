#pragma once

#include "module.hpp"

#include <string>

class AudioInput : public Module {
public:
    std::string device = "default";

    std::string name() const override { return "audio_input"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;
};
