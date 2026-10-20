#pragma once

#include "module.hpp"

#include <string>

class PitchDetectorModule : public Module {
public:
    std::string name() const override { return "pitch_detector"; }
    Role role() const override { return Role::visualizer; }
    std::string save() const override { return "{}"; }
    bool load(const std::string& /*json*/) override { return true; }
    bool render() override { return false; }
    void process(ProcessContext& ctx) override;
};
