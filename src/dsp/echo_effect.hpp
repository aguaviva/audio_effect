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

    // Default size is for NodeState copies; audio runtime passes a real capacity.
    explicit EchoEffect(size_t max_delay_samples = 48000);

    std::string name() const override { return "echo"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;

    void process(float* samples, size_t sample_count, float sample_rate);
    void reset();

    void copy_controls_from(const EchoEffect& other) {
        delay_ms = other.delay_ms;
        feedback = other.feedback;
        mix = other.mix;
        selected_preset = other.selected_preset;
    }

private:
    std::vector<float> delay_left_;
    std::vector<float> delay_right_;
    size_t write_index_{0};
};
