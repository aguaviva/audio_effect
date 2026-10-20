#pragma once

#include "module.hpp"

#include <string>

class Oscilloscope : public Module {
public:
    float display_gain = 1.0f;

    std::string name() const override { return "oscilloscope"; }
    Role role() const override { return Role::visualizer; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;
};
