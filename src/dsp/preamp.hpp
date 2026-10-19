#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

class Preamp : public Module {
public:
    float gain_db = 0.0f;
    int selected_preset = 0; // 0 = Custom

    std::string name() const override { return "preamp"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;

    void process(float* samples, size_t num_samples);
};
