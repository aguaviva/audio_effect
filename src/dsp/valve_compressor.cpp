#include "valve_compressor.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

void ValveCompressor::process(float* samples, size_t num_samples, float sample_rate) {
    float threshold = std::pow(10.0f, threshold_db / 20.0f);
    float attack_coefficient = std::exp(-1.0f / (sample_rate * attack_ms * 0.001f));
    float release_coefficient = std::exp(-1.0f / (sample_rate * release_ms * 0.001f));
    float makeup_gain = std::pow(10.0f, makeup_db / 20.0f);
    constexpr float bias = 0.12f;
    float bias_offset = std::tanh(bias);
    float positive_scale = std::tanh(drive + bias) - bias_offset;
    float negative_scale = bias_offset - std::tanh(-drive + bias);

    for (size_t i = 0; i < num_samples; i += 2) {
        float input_l = samples[i];
        float input_r = samples[i + 1];
        float level = std::max(std::abs(input_l), std::abs(input_r));
        float coefficient = level > envelope_ ? attack_coefficient : release_coefficient;
        envelope_ = coefficient * envelope_ + (1.0f - coefficient) * level;

        float gain = 1.0f;
        if (envelope_ > threshold) {
            float compressed_level = threshold * std::pow(envelope_ / threshold, 1.0f / ratio);
            gain = compressed_level / envelope_;
        }

        float driven_l = input_l * gain * makeup_gain;
        float driven_r = input_r * gain * makeup_gain;
        float shaped_l = std::tanh(drive * driven_l + bias) - bias_offset;
        float shaped_r = std::tanh(drive * driven_r + bias) - bias_offset;
        float output_l = shaped_l / (driven_l >= 0.0f ? positive_scale : negative_scale);
        float output_r = shaped_r / (driven_r >= 0.0f ? positive_scale : negative_scale);
        samples[i] = std::clamp(output_l, -1.0f, 1.0f);
        samples[i + 1] = std::clamp(output_r, -1.0f, 1.0f);
    }
}

bool ValveCompressor::render() {
    bool changed = false;

    static const char* preset_names[] = {
        "Custom", "Gentle Leveling", "Smooth Sustain", "Fast Clamp", "Valve Color"
    };

    ImGui::TextUnformatted("Preset");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##comp_preset", &selected_preset, preset_names, IM_ARRAYSIZE(preset_names))) {
        switch (selected_preset) {
        case 1:
            threshold_db = -18.0f;
            ratio = 2.0f;
            attack_ms = 30.0f;
            release_ms = 180.0f;
            drive = 1.05f;
            makeup_db = 1.0f;
            break;
        case 2:
            threshold_db = -26.0f;
            ratio = 4.0f;
            attack_ms = 12.0f;
            release_ms = 160.0f;
            drive = 1.2f;
            makeup_db = 3.0f;
            break;
        case 3:
            threshold_db = -12.0f;
            ratio = 6.0f;
            attack_ms = 2.0f;
            release_ms = 70.0f;
            drive = 1.0f;
            makeup_db = 0.0f;
            break;
        case 4:
            threshold_db = -20.0f;
            ratio = 2.5f;
            attack_ms = 20.0f;
            release_ms = 110.0f;
            drive = 2.0f;
            makeup_db = 1.0f;
            break;
        }
        changed = true;
    }

    ImGui::TextUnformatted("Thresh dB");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##comp_th", &threshold_db, -40.0f, 0.0f, "%.1f")) {
        selected_preset = 0;
        changed = true;
    }
    ImGui::TextUnformatted("Ratio");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##comp_ratio", &ratio, 1.0f, 20.0f, "%.1f")) {
        selected_preset = 0;
        changed = true;
    }
    ImGui::TextUnformatted("Attack ms");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##comp_atk", &attack_ms, 1.0f, 100.0f, "%.1f")) {
        selected_preset = 0;
        changed = true;
    }
    ImGui::TextUnformatted("Release ms");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##comp_rel", &release_ms, 10.0f, 500.0f, "%.0f")) {
        selected_preset = 0;
        changed = true;
    }
    ImGui::TextUnformatted("Drive");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##comp_drive", &drive, 0.5f, 5.0f, "%.2f")) {
        selected_preset = 0;
        changed = true;
    }
    ImGui::TextUnformatted("Makeup dB");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##comp_makeup", &makeup_db, -12.0f, 24.0f, "%.1f")) {
        selected_preset = 0;
        changed = true;
    }

    return changed;
}

void ValveCompressor::save(std::string& out, bool& first) const {
    module_json::write_float(out, first, "threshold_db", threshold_db);
    module_json::write_float(out, first, "ratio", ratio);
    module_json::write_float(out, first, "attack_ms", attack_ms);
    module_json::write_float(out, first, "release_ms", release_ms);
    module_json::write_float(out, first, "drive", drive);
    module_json::write_float(out, first, "makeup_db", makeup_db);
    module_json::write_int(out, first, "selected_preset", selected_preset);
}

bool ValveCompressor::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "threshold_db") { threshold_db = static_cast<float>(num); return true; }
    if (key == "ratio") { ratio = static_cast<float>(num); return true; }
    if (key == "attack_ms") { attack_ms = static_cast<float>(num); return true; }
    if (key == "release_ms") { release_ms = static_cast<float>(num); return true; }
    if (key == "drive") { drive = static_cast<float>(num); return true; }
    if (key == "makeup_db") { makeup_db = static_cast<float>(num); return true; }
    if (key == "selected_preset") { selected_preset = static_cast<int>(num); return true; }
    return false;
}
