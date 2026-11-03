#include "white_noise.hpp"

#include <algorithm>
#include <cstdio>

#include "imgui.h"
#include "ui/timeline.hpp"
#include "ui/timeline_band_lane.hpp"

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
    if (ImGui::Button(playing ? "Pause###play" : "Play###play")) {
        playing = !playing;
    }
    ImGui::TextUnformatted("Level");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##wn_level", &level, 0.0f, 1.0f, "%.2f");
}

bool WhiteNoise::render_timeline(Timeline& timeline, const char* title) {
    timeline.BeginTrack("noise");
    ImGui::TextUnformatted(title);
    if (ImGui::SmallButton(playing ? "Pause###play" : "Play###play")) {
        playing = !playing;
    }
    char label[32];
    std::snprintf(label, sizeof(label), "Noise, level %.2f", level);
    draw_band(timeline, label, playing);
    timeline.EndTrack();
    return true;
}
void WhiteNoise::process(const float* const* /*inputs*/, int /*input_count*/,
                         size_t sample_count, float /*sample_rate*/) {
    float* output = output_buffer(0);
    if (!output) {
        return;
    }
    if (!playing) {
        std::fill(output, output + sample_count, 0.0f);
        return;
    }
    const float amp = std::clamp(level, 0.0f, 1.0f);
    for (size_t i = 0; i < sample_count; ++i) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const float unit = static_cast<float>((rng_ >> 8) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
        output[i] = (unit * 2.0f - 1.0f) * amp;
    }
}
