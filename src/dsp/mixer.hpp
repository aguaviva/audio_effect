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

    // How many channel sliders to draw this frame (set by node UI from pin count).
    int ui_slots = kChannels;

    std::string name() const override { return "mixer"; }
    Role role() const override { return Role::multi_in; }
    bool supports_bypass() const override { return false; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
    void process(ProcessContext& ctx) override;
};
