#include "dynamic_high_pass_filter.hpp"

#include <cmath>

#include "imgui.h"

void DynamicHighPassFilter::process(float* samples, size_t num_samples, float sample_rate) {
    float dt = 1.0f / sample_rate;
    float rc = 1.0f / (2.0f * static_cast<float>(M_PI) * cutoff_hz);
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
    bool changed = false;

    static const char* preset_names[] = {
        "Custom", "Guitar Low Cut (70 Hz)", "Tight Low End (100 Hz)", "Lean Low End (150 Hz)"
    };
    static const float preset_cutoffs[] = {0.0f, 70.0f, 100.0f, 150.0f};

    ImGui::TextUnformatted("Preset");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##hpf_preset", &selected_preset, preset_names, IM_ARRAYSIZE(preset_names))) {
        if (selected_preset > 0 && selected_preset < IM_ARRAYSIZE(preset_cutoffs)) {
            cutoff_hz = preset_cutoffs[selected_preset];
        }
        changed = true;
    }

    ImGui::TextUnformatted("Cutoff Hz");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##hpf_cutoff", &cutoff_hz, 20.0f, 1000.0f, "%.0f")) {
        selected_preset = 0;
        changed = true;
    }

    return changed;
}

std::string DynamicHighPassFilter::save() const {
    std::string out = "{";
    module_json::write_float(out, "cutoff_hz", cutoff_hz);
    module_json::write_int(out, "selected_preset", selected_preset);
    out += '}';
    return out;
}

bool DynamicHighPassFilter::load(const std::string& json) {
    return module_json::for_each_member(json, [&](const std::string& key, const std::string& text, size_t& i) -> bool {
        if (key == "cutoff_hz") {
            double num = 0.0;
            if (!module_json::parse_number_value(text, i, num)) {
                return false;
            }
            cutoff_hz = static_cast<float>(num);
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


void DynamicHighPassFilter::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    process(ctx.buffer, ctx.num_samples, ctx.sample_rate);
}
