#pragma once

#include "module.hpp"
#include "pitch_detector.hpp"

#include <atomic>
#include <memory>
#include <string>

class PitchDetectorModule : public Module {
public:
    std::string name() const override { return "pitch_detector"; }
    std::string save() const override { return "{}"; }
    bool load(const std::string& /*json*/) override { return true; }
    bool render() override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;

private:
    std::unique_ptr<PitchDetector> detector_; // audio thread only
    float detector_rate_ = 0.0f;
    std::atomic<float> frequency_hz_{0.0f};
};
