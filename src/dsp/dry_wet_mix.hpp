#pragma once

#include "module.hpp"

#include <string>

class DryWetMix : public Module {
public:
    float mix = 0.5f;

    std::string name() const override { return "dry_wet_mix"; }
    Role role() const override { return Role::multi_in; }
    std::string save() const override;
    bool load(const std::string& json) override;
    void get_inputs(int *count, const char* const** str);
    bool render() override;
    void process(ProcessContext& ctx) override;
};
