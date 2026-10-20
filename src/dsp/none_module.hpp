#pragma once

#include "module.hpp"

#include <string>

// Placeholder module for nodes without a concrete type.
class NoneModule : public Module {
public:
    std::string name() const override { return "none"; }
    std::string save() const override { return "{}"; }
    bool load(const std::string& /*json*/) override { return true; }
    bool render() override { return false; }
    void process(const float* const* /*inputs*/, int /*input_count*/, float* output,
                 size_t sample_count, float /*sample_rate*/) override {
        std::fill(output, output + sample_count, 0.0f);
    }
};
