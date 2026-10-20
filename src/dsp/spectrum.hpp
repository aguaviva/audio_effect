#pragma once

#include "module.hpp"

#include <mutex>
#include <string>

class Spectrum : public Module {
public:
    static constexpr int kBins = 32;

    float display_gain = 1.0f;

    std::string name() const override { return "spectrum"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;

private:
    // The audio thread only try_locks, so drawing never stalls audio.
    std::mutex bins_mutex_;
    float bins_[kBins]{};
};
