#include "dynamic_high_pass_filter.hpp"

#include <cmath>

#include "imgui.h"

void DynamicHighPassFilter::process(float* samples, size_t num_samples, float cutoff, float sample_rate) {
    float dt = 1.0f / sample_rate;
    float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * cutoff);
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

void DynamicHighPassFilter::render(std::atomic<bool>& enabled,
                                   std::atomic<float>& cutoff_hz,
                                   int& selected_preset, bool& controls_changed) {
    bool active = enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("High-Pass Filter##enable", &active)) {
        enabled.store(active, std::memory_order_relaxed);
        controls_changed = true;
    }
    ImGui::Indent();
    ImGui::BeginDisabled(!active);

    static const char* preset_names[] = {
        "Custom", "Guitar Low Cut (70 Hz)", "Tight Low End (100 Hz)", "Lean Low End (150 Hz)"
    };
    if (ImGui::Combo("High-Pass Preset", &selected_preset, preset_names, IM_ARRAYSIZE(preset_names)) && selected_preset != 0) {
        static const float cutoffs[] = {0.0f, 70.0f, 100.0f, 150.0f};
        cutoff_hz.store(cutoffs[selected_preset], std::memory_order_relaxed);
        enabled.store(true, std::memory_order_relaxed);
        controls_changed = true;
    }

    float cutoff = cutoff_hz.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Cutoff (Hz)##highpass", &cutoff, 20.0f, 1000.0f, "%.0f Hz")) {
        cutoff_hz.store(cutoff, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = true;
    }

    ImGui::EndDisabled();
    ImGui::Unindent();
    ImGui::Separator();
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
