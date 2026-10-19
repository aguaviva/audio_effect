#include "guitar_pedals.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

namespace guitar_pedals {

void DrivePedal::process(float* samples, size_t sample_count, DriveModel model,
                        float drive, float tone, float sample_rate) {
    float cutoff = 1800.0f + tone * 10000.0f;
    float alpha = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * cutoff / sample_rate);
    constexpr float bias = 0.06f;
    float bias_offset = std::tanh(bias);
    float positive_scale = std::tanh(drive + bias) - bias_offset;
    float negative_scale = bias_offset - std::tanh(-drive + bias);
    float fuzz_normalization = std::tanh(1.8f);

    for (size_t i = 0; i < sample_count; i += 2) {
        float output_l = shape(samples[i], model, drive, bias, bias_offset,
                               positive_scale, negative_scale, fuzz_normalization);
        float output_r = shape(samples[i + 1], model, drive, bias, bias_offset,
                               positive_scale, negative_scale, fuzz_normalization);
        tone_state_l_ += alpha * (output_l - tone_state_l_);
        tone_state_r_ += alpha * (output_r - tone_state_r_);
        samples[i] = tone_state_l_;
        samples[i + 1] = tone_state_r_;
    }
}

void DrivePedal::reset() {
    tone_state_l_ = 0.0f;
    tone_state_r_ = 0.0f;
}

float DrivePedal::shape(float input, DriveModel model, float drive, float bias,
                        float bias_offset, float positive_scale, float negative_scale,
                        float fuzz_normalization) {
    switch (model) {
    case DriveModel::tube_screamer: {
        float shaped = std::tanh(drive * input + bias) - bias_offset;
        float scale = input >= 0.0f ? positive_scale : negative_scale;
        return std::clamp(shaped / scale, -1.0f, 1.0f);
    }
    case DriveModel::rat:
        return std::clamp(input * drive, -1.0f, 1.0f);
    case DriveModel::big_muff: {
        float first_stage = std::tanh(input * drive * 1.5f);
        return std::clamp(std::tanh(first_stage * 1.8f) / fuzz_normalization, -1.0f, 1.0f);
    }
    }
    return input;
}

void DrivePedal::render(std::atomic<bool>& enabled, std::atomic<int>& model,
                        std::atomic<float>& drive, std::atomic<float>& tone,
                        int& selected_preset, bool& controls_changed) {
    bool active = enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Drive Pedal##enable", &active)) {
        enabled.store(active, std::memory_order_relaxed);
        controls_changed = true;
    }
    ImGui::Indent();
    ImGui::BeginDisabled(!active);

    static const char* model_names[] = {
        "Custom", "Tube Screamer-style Overdrive", "RAT-style Distortion", "Big Muff Pi-style Fuzz"
    };
    int selected_model = selected_preset;
    if (ImGui::Combo("Drive Pedal Model", &selected_model, model_names, IM_ARRAYSIZE(model_names))) {
        selected_preset = selected_model;
        if (selected_model > 0) {
            model.store(selected_model - 1, std::memory_order_relaxed);
            enabled.store(true, std::memory_order_relaxed);
        } else {
            enabled.store(false, std::memory_order_relaxed);
        }
        controls_changed = true;
    }

    float drive_value = drive.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Pedal Drive", &drive_value, 1.0f, 8.0f, "%.2f")) {
        drive.store(drive_value, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = true;
    }
    float tone_value = tone.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Pedal Tone", &tone_value, 0.0f, 1.0f, "%.2f")) {
        tone.store(tone_value, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = true;
    }

    ImGui::EndDisabled();
    ImGui::Unindent();
    ImGui::Separator();
}

} // namespace guitar_pedals

void guitar_pedals::DrivePedal::Params::write(std::string& out, bool& first) const {
    module_json::write_int(out, first, "model", model);
    module_json::write_float(out, first, "amount", amount);
    module_json::write_float(out, first, "tone", tone);
}
bool guitar_pedals::DrivePedal::Params::read(const std::string& key, double num, bool, const std::string&,
                                             int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "model") {
        model = static_cast<int>(num);
        return true;
    }
    if (key == "amount") {
        amount = static_cast<float>(num);
        return true;
    }
    if (key == "tone") {
        tone = static_cast<float>(num);
        return true;
    }
    return false;
}
