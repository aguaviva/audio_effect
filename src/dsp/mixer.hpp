#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

class Mixer : public Module {
public:
    std::string name() const override { return "mixer"; }
    Role role() const override { return Role::multi_in; }
    bool supports_bypass() const override { return false; }
    void save(std::string& /*out*/, bool& /*first*/) const override {}
    bool load(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) override {
        return false;
    }
    bool render() override;
    void process(ProcessContext& ctx) override;
};
