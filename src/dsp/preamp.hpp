#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

class Preamp : public Module {
public:
    float gain_db = 0.0f;
    int selected_preset = 0; // 0 = Custom

    std::string name() const override { return "preamp"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;
};
