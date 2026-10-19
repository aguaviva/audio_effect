#include "mixer.hpp"

#include <algorithm>
#include <cstdio>

#include "imgui.h"

std::string Mixer::save() const {
    std::string out = "{";
    char key[16];
    for (int i = 0; i < kChannels; ++i) {
        std::snprintf(key, sizeof(key), "gain_%d", i);
        module_json::write_float(out, key, gain[static_cast<size_t>(i)]);
    }
    out += '}';
    return out;
}

bool Mixer::load(const std::string& json) {
    char key[16];
    for (int i = 0; i < kChannels; ++i) {
        std::snprintf(key, sizeof(key), "gain_%d", i);
        float& g = gain[static_cast<size_t>(i)];
        module_json::read(json, key, g);
        g = std::clamp(g, 0.0f, 2.0f);
    }
    return true;
}

bool Mixer::render() {
    bool changed = false;
    const int slots = std::clamp(ui_slots, 1, kChannels);
    char label[32];
    char id[32];
    for (int i = 0; i < slots; ++i) {
        std::snprintf(label, sizeof(label), "In %d", i + 1);
        std::snprintf(id, sizeof(id), "##mix_g%d", i);
        ImGui::TextUnformatted(label);
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat(id, &gain[static_cast<size_t>(i)], 0.0f, 2.0f, "%.2f")) {
            changed = true;
        }
    }
    return changed;
}

void Mixer::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    std::fill(ctx.buffer, ctx.buffer + ctx.num_samples, 0.0f);
    if (ctx.inputs == nullptr || ctx.input_count == 0) {
        return;
    }

    size_t connected = 0;
    for (size_t s = 0; s < ctx.input_count && s < static_cast<size_t>(kChannels); ++s) {
        if (ctx.input_muted && ctx.input_muted[s]) {
            continue;
        }
        const float* src = ctx.inputs[s];
        if (src == nullptr) {
            continue;
        }
        const float g = gain[s];
        for (size_t i = 0; i < ctx.num_samples; ++i) {
            ctx.buffer[i] += src[i] * g;
        }
        ++connected;
    }

    if (connected > 0) {
        const float scale = 1.0f / static_cast<float>(connected);
        for (size_t i = 0; i < ctx.num_samples; ++i) {
            ctx.buffer[i] *= scale;
        }
    }
}
