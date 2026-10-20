#pragma once

#include "module.hpp"

#include <string>

class AudioOutput : public Module {
public:
    std::string device = "default";

    std::string name() const override { return "output"; }
    Role role() const override { return Role::sink; }
    bool is_output_sink() const override { return true; }
    bool supports_bypass() const override { return false; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& /*ctx*/) override {}
};
