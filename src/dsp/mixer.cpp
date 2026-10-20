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

void Mixer::get_inputs(int *count, const char* const** str) 
{   
    static const char* input_names[] = {"in 1", "in 2", "in 3", "in 4", "in 5", "in 6", "in 7", "in 8"};    
    *count=ui_slots; 
    *str=input_names;
} 

bool Mixer::render() {
    bool changed = false;
    const int slots = std::clamp(ui_slots, 1, kChannels);
    
    for (int i = 0; i < slots-1; ++i) 
    {
        ImGui::PushID(i);
        ImGui::Checkbox("", &input_muted[i]);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(input_muted[i] ? "Muted (click to unmute)" : "Mute this input");
        }        
        ImGui::SameLine(0.0f, 6.0f);

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("channel", &gain[static_cast<size_t>(i)], 0.0f, 2.0f, "%.2f")) {
            changed = true;
        }
        ImGui::PopID();
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
        if (ctx.input_muted && input_muted[s]) {
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
