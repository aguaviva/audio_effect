#include "white_noise.hpp"

#include <algorithm>

#include "imgui.h"

std::string WhiteNoise::save() const {
    std::string out = "{";
    module_json::write_float(out, "level", level);
    out += '}';
    return out;
}

bool WhiteNoise::load(const std::string& json) {
    module_json::read(json, "level", level);
    level = std::clamp(level, 0.0f, 1.0f);
    return true;
}

bool WhiteNoise::render() {
    ImGui::TextUnformatted("Level");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##wn_level", &level, 0.0f, 1.0f, "%.2f");
}
void WhiteNoise::process(const float* const* /*inputs*/, int /*input_count*/, float* output,
                         size_t sample_count, float /*sample_rate*/) {
    const float amp = std::clamp(level, 0.0f, 1.0f);
    for (size_t i = 0; i < sample_count; ++i) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const float unit = static_cast<float>((rng_ >> 8) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
        output[i] = (unit * 2.0f - 1.0f) * amp;
    }
}
