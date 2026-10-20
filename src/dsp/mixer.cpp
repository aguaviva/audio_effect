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
        std::snprintf(key, sizeof(key), "muted_%d", i);
        module_json::write_bool(out, key, input_muted[i]);
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
        std::snprintf(key, sizeof(key), "muted_%d", i);
        module_json::read(json, key, input_muted[i]);
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

void Mixer::process(const float* const* inputs, int input_count, float* output,
                    size_t sample_count, float /*sample_rate*/) {
    std::fill(output, output + sample_count, 0.0f);

    size_t connected = 0;
    for (int s = 0; s < input_count && s < kChannels; ++s) {
        const float* src = inputs[s];
        if (src == nullptr || input_muted[s]) {
            continue;
        }
        const float g = gain[static_cast<size_t>(s)];
        for (size_t i = 0; i < sample_count; ++i) {
            output[i] += src[i] * g;
        }
        ++connected;
    }

    if (connected > 0) {
        const float scale = 1.0f / static_cast<float>(connected);
        for (size_t i = 0; i < sample_count; ++i) {
            output[i] *= scale;
        }
    }
}
