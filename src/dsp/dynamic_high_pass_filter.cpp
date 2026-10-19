#include "dynamic_high_pass_filter.hpp"

#include <cmath>

#include "imgui.h"

void DynamicHighPassFilter::process(float* samples, size_t num_samples, float sample_rate) {
    float dt = 1.0f / sample_rate;
    float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * params.cutoff_hz);
    float alpha = rc / (rc + dt);

    for (size_t i = 0; i < num_samples; i += 2) {
        float input_l = samples[i];
        float input_r = samples[i + 1];
        float output_l = alpha * (prev_output_l_ + input_l - prev_input_l_);
        float output_r = alpha * (prev_output_r_ + input_r - prev_input_r_);
        prev_input_l_ = input_l;
        prev_input_r_ = input_r;
        prev_output_l_ = output_l;
        prev_output_r_ = output_r;
        samples[i] = output_l;
        samples[i + 1] = output_r;
    }
}

void DynamicHighPassFilter::reset() {
    prev_input_l_ = 0.0f;
    prev_input_r_ = 0.0f;
    prev_output_l_ = 0.0f;
    prev_output_r_ = 0.0f;
}

bool DynamicHighPassFilter::render() {
    bool controls_changed = false;
    if (ImGui::Checkbox("High-Pass Filter##enable", &enabled)) {
        controls_changed = true;
    }
    ImGui::Indent();
    ImGui::BeginDisabled(!enabled);

    static const char* preset_names[] = {
        "Custom", "Guitar Low Cut (70 Hz)", "Tight Low End (100 Hz)", "Lean Low End (150 Hz)"
    };
    if (ImGui::Combo("High-Pass Preset", &selected_preset, preset_names, IM_ARRAYSIZE(preset_names)) &&
        selected_preset != 0) {
        static const float cutoffs[] = {0.0f, 70.0f, 100.0f, 150.0f};
        params.cutoff_hz = cutoffs[selected_preset];
        enabled = true;
        controls_changed = true;
    }

    if (ImGui::SliderFloat("Cutoff (Hz)##highpass", &params.cutoff_hz, 20.0f, 1000.0f, "%.0f Hz")) {
        selected_preset = 0;
        controls_changed = true;
    }

    ImGui::EndDisabled();
    ImGui::Unindent();
    ImGui::Separator();
    return controls_changed;
}

void DynamicHighPassFilter::Params::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "cutoff_hz", cutoff_hz);
}
bool DynamicHighPassFilter::Params::read(const std::string& key, double num, bool, const std::string&,
                                         int kind) {
    if (key == "cutoff_hz" && kind == module_json::kKindNumber) {
        cutoff_hz = static_cast<float>(num);
        return true;
    }
    return false;
}
