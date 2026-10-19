#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

class NoiseGate : public Module {
public:
    float threshold_db = -40.0f;
    float attack_ms = 5.0f;
    float release_ms = 80.0f;

    std::string name() const override { return "noise_gate"; }
    Role role() const override { return Role::effect; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

    void process(float* interleaved, size_t sample_count, float sample_rate);
    void reset();


private:
    float envelope_ = 0.0f;
    float gain_ = 0.0f;
};
