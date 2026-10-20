#pragma once

#include "module.hpp"

#include <cstdint>
#include <string>

class WhiteNoise : public Module {
public:
    float level = 0.2f;

    std::string name() const override { return "white_noise"; }
    Role role() const override { return Role::source; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

private:
    uint32_t rng_ = 0x12345678u;
};
