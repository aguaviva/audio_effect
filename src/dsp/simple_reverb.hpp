#pragma once

#include "module.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

class SimpleReverb : public Module {
public:
    float room = 0.55f;
    float damping = 0.4f;
    float mix = 0.25f;

    std::string name() const override { return "reverb"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* const* outputs, size_t sample_count,
                 float sample_rate) override;

private:
    struct Comb {
        std::vector<float> buffer;
        size_t index = 0;
        float filter = 0.0f;
        float process(float input, float feedback, float damp);
    };
    struct Allpass {
        std::vector<float> buffer;
        size_t index = 0;
        float process(float input);
    };

    void ensure_buffers(float sample_rate);

    std::array<Comb, 4> combs_l_{};
    std::array<Comb, 4> combs_r_{};
    std::array<Allpass, 2> allpass_l_{};
    std::array<Allpass, 2> allpass_r_{};
    float configured_sr_ = 0.0f;
};
