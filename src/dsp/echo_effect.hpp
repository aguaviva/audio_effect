#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>
#include <vector>

class EchoEffect : public Module {
public:
    float delay_ms = 320.0f;
    float feedback = 0.35f;
    float mix = 0.25f;
    int selected_preset = 0; // 0 = Custom

    // Sized for 1 s at 48 kHz; process() grows it if the stream runs faster.
    explicit EchoEffect(size_t max_delay_samples = 48001);

    std::string name() const override { return "echo"; }
    Role role() const override { return Role::effect; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

    void process(float* samples, size_t sample_count, float sample_rate);
    void reset();


private:
    std::vector<float> delay_left_;
    std::vector<float> delay_right_;
    size_t write_index_{0};
};
