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
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

    void process(float* interleaved, size_t sample_count, float sample_rate);
    void reset();

    void copy_controls_from(const NoiseGate& other) {
        threshold_db = other.threshold_db;
        attack_ms = other.attack_ms;
        release_ms = other.release_ms;
    }

private:
    float envelope_ = 0.0f;
    float gain_ = 0.0f;
};
