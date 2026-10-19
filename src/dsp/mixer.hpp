#pragma once

#include "module.hpp"

#include <cstddef>
#include <string>

// N-input equal-weight summer. Mute / wiring stay in the graph; this DSP only
// averages the sources it is given.
class Mixer : public Module {
public:
    std::string name() const override { return "mixer"; }
    void save(std::string& /*out*/, bool& /*first*/) const override {}
    bool load(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) override {
        return false;
    }
    bool render() override;

    // sources: array of interleaved blocks (or nullptr). Null / missing slots skipped.
    // destination: cleared then filled with the equal-weight average of connected sources.
    void process(const float* const* sources, size_t source_count, float* destination,
                 size_t num_samples) const;
};
