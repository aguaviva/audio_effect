#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

namespace guitar_pedals {

enum class DriveModel : int {
    tube_screamer,
    rat,
    big_muff
};

class DrivePedal : public Module {
public:
    int model = 0;
    float amount = 3.0f;
    float tone = 0.65f;

    std::string name() const override { return "drive_pedal"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;

    void process(float* samples, size_t sample_count, float sample_rate);
    void reset();

    void copy_controls_from(const DrivePedal& other) {
        model = other.model;
        amount = other.amount;
        tone = other.tone;
    }

private:
    static float shape(float input, DriveModel model, float drive, float bias,
                       float bias_offset, float positive_scale, float negative_scale,
                       float fuzz_normalization);
    float tone_state_l_{0.0f};
    float tone_state_r_{0.0f};
};

} // namespace guitar_pedals
