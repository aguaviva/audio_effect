#pragma once

#include "audio_input_sources.hpp"
#include "module.hpp"

#include <cstdint>
#include <string>
#include <vector>

class ChordGenerator : public Module {
public:
    int index = 0;

    std::string name() const override { return "chord_generator"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;

private:
    ChordGeneratorInput chord_;
    std::vector<int32_t> scratch_;
};
