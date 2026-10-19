#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

class ValveCompressor : public Module {
public:
    float threshold_db = -18.0f;
    float ratio = 3.0f;
    float attack_ms = 15.0f;
    float release_ms = 100.0f;
    float drive = 1.25f;
    float makeup_db = 0.0f;
    int selected_preset = 0; // 0 = Custom

    std::string name() const override { return "compressor"; }
    Role role() const override { return Role::effect; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

    void process(float* samples, size_t num_samples, float sample_rate);


private:
    float envelope_{0.0f};
};
