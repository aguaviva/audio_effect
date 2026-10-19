#include "dynamic_low_pass_filter.hpp"

#include <cmath>

#include "imgui.h"

void DynamicLowPassFilter::process(float* samples, size_t num_samples, float sample_rate) {
    float dt = 1.0f / sample_rate;
    float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * params.cutoff_hz);
    float alpha = dt / (rc + dt);

    for (size_t i = 0; i < num_samples; i += 2) {
        prev_l_ += alpha * (samples[i] - prev_l_);
        samples[i] = prev_l_;
        prev_r_ += alpha * (samples[i + 1] - prev_r_);
        samples[i + 1] = prev_r_;
    }
}

void DynamicLowPassFilter::reset() {
    prev_l_ = 0.0f;
    prev_r_ = 0.0f;
}

bool DynamicLowPassFilter::render() {
    bool changed = false;

    static const char* preset_names[] = {
        "Custom", "Open (12 kHz)", "Bright (10 kHz)", "Guitar Bright (8 kHz)",
        "Vintage (6.5 kHz)", "RAT Tone (5.5 kHz)", "Fuzz Tone (5 kHz)"
    };
    static const float preset_cutoffs[] = {
        0.0f, 12000.0f, 10000.0f, 8000.0f, 6500.0f, 5500.0f, 5000.0f
    };

    ImGui::TextUnformatted("Preset");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##lpf_preset", &params.selected_preset, preset_names,
                     IM_ARRAYSIZE(preset_names))) {
        if (params.selected_preset > 0 &&
            params.selected_preset < IM_ARRAYSIZE(preset_cutoffs)) {
            params.cutoff_hz = preset_cutoffs[params.selected_preset];
        }
        changed = true;
    }

    ImGui::TextUnformatted("Cutoff Hz");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##lpf_cutoff", &params.cutoff_hz, 100.0f, 18000.0f, "%.0f")) {
        params.selected_preset = 0; // Custom
        changed = true;
    }

    return changed;
}

void DynamicLowPassFilter::Params::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "cutoff_hz", cutoff_hz);
    module_json::write_int(out, first, "selected_preset", selected_preset);
}
bool DynamicLowPassFilter::Params::read(const std::string& key, double num, bool, const std::string&,
                                        int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "cutoff_hz") {
        cutoff_hz = static_cast<float>(num);
        return true;
    }
    if (key == "selected_preset") {
        selected_preset = static_cast<int>(num);
        return true;
    }
    return false;
}
