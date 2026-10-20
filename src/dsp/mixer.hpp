#pragma once

#include "module.hpp"

#include <array>
#include <cstddef>
#include <string>

class Mixer : public Module {
public:
    static constexpr int kChannels = 8;

    // Per-input linear gain (1 = unity). Serialized as gain_0 .. gain_7.
    std::array<float, kChannels> gain{{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f}};
    bool input_muted[kChannels];

    // How many channel sliders to draw this frame (set by node UI from pin count).
    int ui_slots = kChannels;

    std::string name() const override { return "mixer"; }
    Role role() const override { return Role::multi_in; }
    bool supports_bypass() const override { return false; }
    std::string save() const override;
    bool load(const std::string& json) override;
    void get_inputs(int *count, const char* const** str);
    bool render() override;
    void process(ProcessContext& ctx) override;
};
