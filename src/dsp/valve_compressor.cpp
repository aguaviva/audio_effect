#include "valve_compressor.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

void ValveCompressor::process(float* samples, size_t num_samples, float threshold_db, float ratio,
                              float attack_ms, float release_ms, float drive, float makeup_db,
                              float sample_rate) {
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

void ValveCompressor::render(std::atomic<bool>& enabled,
                             std::atomic<float>& threshold_db, std::atomic<float>& ratio,
                             std::atomic<float>& attack_ms, std::atomic<float>& release_ms,
                             std::atomic<float>& drive, std::atomic<float>& makeup_db,
                             int& selected_preset, bool& controls_changed,
                             bool& compressor_controls_changed) {
    bool active = enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Valve Compressor##enable", &active)) {
        enabled.store(active, std::memory_order_relaxed);
        controls_changed = true;
    }
    ImGui::Indent();

    static const char* preset_names[] = {
        "Custom", "Gentle Leveling", "Smooth Sustain", "Fast Clamp", "Valve Color"
    };
    if (ImGui::Combo("Compressor Preset", &selected_preset, preset_names, IM_ARRAYSIZE(preset_names))) {
        switch (selected_preset) {
        case 1:
            threshold_db.store(-18.0f, std::memory_order_relaxed);
            ratio.store(2.0f, std::memory_order_relaxed);
            attack_ms.store(30.0f, std::memory_order_relaxed);
            release_ms.store(180.0f, std::memory_order_relaxed);
            drive.store(1.05f, std::memory_order_relaxed);
            makeup_db.store(1.0f, std::memory_order_relaxed);
            enabled.store(true, std::memory_order_relaxed);
            break;
        case 2:
            threshold_db.store(-26.0f, std::memory_order_relaxed);
            ratio.store(4.0f, std::memory_order_relaxed);
            attack_ms.store(12.0f, std::memory_order_relaxed);
            release_ms.store(160.0f, std::memory_order_relaxed);
            drive.store(1.2f, std::memory_order_relaxed);
            makeup_db.store(3.0f, std::memory_order_relaxed);
            enabled.store(true, std::memory_order_relaxed);
            break;
        case 3:
            threshold_db.store(-12.0f, std::memory_order_relaxed);
            ratio.store(6.0f, std::memory_order_relaxed);
            attack_ms.store(2.0f, std::memory_order_relaxed);
            release_ms.store(70.0f, std::memory_order_relaxed);
            drive.store(1.0f, std::memory_order_relaxed);
            makeup_db.store(0.0f, std::memory_order_relaxed);
            enabled.store(true, std::memory_order_relaxed);
            break;
        case 4:
            threshold_db.store(-20.0f, std::memory_order_relaxed);
            ratio.store(2.5f, std::memory_order_relaxed);
            attack_ms.store(20.0f, std::memory_order_relaxed);
            release_ms.store(110.0f, std::memory_order_relaxed);
            drive.store(2.0f, std::memory_order_relaxed);
            makeup_db.store(1.0f, std::memory_order_relaxed);
            enabled.store(true, std::memory_order_relaxed);
            break;
        }
        controls_changed = true;
    }

    ImGui::BeginDisabled(!enabled.load(std::memory_order_relaxed));
    float threshold = threshold_db.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Compressor Threshold (dB)", &threshold, -36.0f, 0.0f, "%.1f dB")) {
        threshold_db.store(threshold, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = compressor_controls_changed = true;
    }
    float ratio_value = ratio.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Compressor Ratio", &ratio_value, 1.0f, 12.0f, "%.1f:1")) {
        ratio.store(ratio_value, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = compressor_controls_changed = true;
    }
    float attack = attack_ms.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Compressor Attack (ms)", &attack, 1.0f, 100.0f, "%.1f ms")) {
        attack_ms.store(attack, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = compressor_controls_changed = true;
    }
    float release = release_ms.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Compressor Release (ms)", &release, 20.0f, 500.0f, "%.0f ms")) {
        release_ms.store(release, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = compressor_controls_changed = true;
    }
    float drive_value = drive.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Valve Drive", &drive_value, 1.0f, 4.0f, "%.2f")) {
        drive.store(drive_value, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = compressor_controls_changed = true;
    }
    float makeup = makeup_db.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Makeup Gain (dB)", &makeup, 0.0f, 12.0f, "%.1f dB")) {
        makeup_db.store(makeup, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = compressor_controls_changed = true;
    }
    ImGui::EndDisabled();
    ImGui::Unindent();
    ImGui::Separator();
}

void ValveCompressor::Params::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "threshold_db", threshold_db);
    module_json::write_float(out, first, "ratio", ratio);
    module_json::write_float(out, first, "attack_ms", attack_ms);
    module_json::write_float(out, first, "release_ms", release_ms);
    module_json::write_float(out, first, "drive", drive);
    module_json::write_float(out, first, "makeup_db", makeup_db);
}
bool ValveCompressor::Params::read(const std::string& key, double num, bool, const std::string&,
                                   int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "threshold_db") { threshold_db = static_cast<float>(num); return true; }
    if (key == "ratio") { ratio = static_cast<float>(num); return true; }
    if (key == "attack_ms") { attack_ms = static_cast<float>(num); return true; }
    if (key == "release_ms") { release_ms = static_cast<float>(num); return true; }
    if (key == "drive") { drive = static_cast<float>(num); return true; }
    if (key == "makeup_db") { makeup_db = static_cast<float>(num); return true; }
    return false;
}
