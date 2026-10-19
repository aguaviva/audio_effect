#include "preamp.hpp"

#include <cmath>

#include "imgui.h"

void Preamp::process(float* samples, size_t num_samples) {
    float gain = std::pow(10.0f, gain_db / 20.0f);
    for (size_t i = 0; i < num_samples; ++i) {
        samples[i] *= gain;
    }
}

bool Preamp::render() {
    bool changed = false;

    static const char* preset_names[] = {
        "Custom", "Unity (0 dB)", "Clean Boost (+6 dB)", "Mid Boost (+9 dB)", "Hot Boost (+12 dB)"
    };
    static const float preset_gains[] = {0.0f, 0.0f, 6.0f, 9.0f, 12.0f};

    ImGui::TextUnformatted("Preset");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##preamp_preset", &selected_preset, preset_names, IM_ARRAYSIZE(preset_names))) {
        if (selected_preset > 0 && selected_preset < IM_ARRAYSIZE(preset_gains)) {
            gain_db = preset_gains[selected_preset];
        }
        changed = true;
    }

    ImGui::TextUnformatted("Gain dB");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##preamp_gain", &gain_db, -12.0f, 24.0f, "%.1f")) {
        selected_preset = 0;
        changed = true;
    }

    return changed;
}

std::string Preamp::save() const {
    std::string out = "{";
    module_json::write_float(out, "gain_db", gain_db);
    module_json::write_int(out, "selected_preset", selected_preset);
    out += '}';
    return out;
}

bool Preamp::load(const std::string& json) {
    return module_json::for_each_member(json, [&](const std::string& key, const std::string& text, size_t& i) -> bool {
        if (key == "gain_db") {
            double num = 0.0;
            if (!module_json::parse_number_value(text, i, num)) {
                return false;
            }
            gain_db = static_cast<float>(num);
            return true;
        }
        if (key == "selected_preset") {
            double num = 0.0;
            if (!module_json::parse_number_value(text, i, num)) {
                return false;
            }
            selected_preset = static_cast<int>(num);
            return true;
        }
        return module_json::skip_value(text, i);
    });
}


void Preamp::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    process(ctx.buffer, ctx.num_samples);
}
