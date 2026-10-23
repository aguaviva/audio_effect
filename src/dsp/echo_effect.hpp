#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>
#include <vector>

class EchoEffect : public Module {
public:
    float delay_ms = 320.0f;
    float feedback = 0.35f;
    float mix = 0.25f;
    int selected_preset = 0; // 0 = Custom

    std::string name() const override { return "echo"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;

private:
    std::vector<float> delay_left_;
    std::vector<float> delay_right_;
    size_t write_index_{0};
};
