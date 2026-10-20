#pragma once

#include "module.hpp"

#include <string>

class AudioOutput : public Module {
public:
    std::string device = "default";
    bool muted = false;
    float volume_db = 0.0f;

    static constexpr float kMinVolumeDb = -60.0f;
    static constexpr float kMaxVolumeDb = 6.0f;

    std::string name() const override { return "output"; }
    void get_outputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    bool is_output_sink() const override { return true; }
    bool supports_bypass() const override { return false; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;

private:
    float gain_ = 1.0f; // audio thread only; ramps toward the target to avoid clicks
};
