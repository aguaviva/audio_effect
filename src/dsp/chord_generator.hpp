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
    Role role() const override { return Role::source; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

private:
    ChordGeneratorInput chord_;
    std::vector<int32_t> scratch_;
};
