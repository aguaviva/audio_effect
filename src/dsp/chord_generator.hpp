#pragma once

#include "chord_catalog.hpp"
#include "module.hpp"

#include <array>
#include <string>

class ChordGenerator : public Module {
public:
    int index = 0;
    bool playing = true;

    std::string name() const override { return "chord_generator"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;

private:
    int active_chord_ = -1;
    std::array<double, kChordVoices> phases_{};
    std::array<double, kChordVoices> frequencies_{};
};
