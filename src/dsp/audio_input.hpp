#pragma once

#include "module.hpp"

#include <string>

class AudioInput : public Module {
public:
    std::string device = "default";

    std::string name() const override { return "audio_input"; }
    Role role() const override { return Role::source; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;
};
