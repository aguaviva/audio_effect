#pragma once

#include "module.hpp"

#include <cstdint>
#include <string>

class WhiteNoise : public Module {
public:
    float level = 0.2f;
    bool playing = true;

    std::string name() const override { return "white_noise"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;

private:
    uint32_t rng_ = 0x12345678u;
};
