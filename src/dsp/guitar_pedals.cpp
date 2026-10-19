#include "guitar_pedals.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

namespace guitar_pedals {

void DrivePedal::process(float* samples, size_t sample_count, float sample_rate) {
    const DriveModel mdl = static_cast<DriveModel>(std::clamp(model, 0, 2));
    const float drive = amount;
    float cutoff = 1800.0f + tone * 10000.0f;
    float alpha = 1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * cutoff / sample_rate);
    constexpr float bias = 0.06f;
    float bias_offset = std::tanh(bias);
    float positive_scale = std::tanh(drive + bias) - bias_offset;
    float negative_scale = bias_offset - std::tanh(-drive + bias);
    float fuzz_normalization = std::tanh(1.8f);

    for (size_t i = 0; i < sample_count; i += 2) {
        float output_l = shape(samples[i], mdl, drive, bias, bias_offset, positive_scale,
                               negative_scale, fuzz_normalization);
        float output_r = shape(samples[i + 1], mdl, drive, bias, bias_offset, positive_scale,
                               negative_scale, fuzz_normalization);
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

float DrivePedal::shape(float input, DriveModel model, float drive, float bias, float bias_offset,
                        float positive_scale, float negative_scale, float fuzz_normalization) {
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

bool DrivePedal::render() {
    bool changed = false;

    static const char* model_names[] = {"Tube Screamer", "RAT", "Big Muff"};
    ImGui::TextUnformatted("Model");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##drive_model", &model, model_names, IM_ARRAYSIZE(model_names))) {
        model = std::clamp(model, 0, 2);
        changed = true;
    }

    ImGui::TextUnformatted("Drive");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##drive_amt", &amount, 0.0f, 10.0f, "%.2f");
    ImGui::TextUnformatted("Tone");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##drive_tone", &tone, 0.0f, 1.0f, "%.2f");

    return changed;
}

std::string DrivePedal::save() const {
    std::string out = "{";
    module_json::write_int(out, "model", model);
    module_json::write_float(out, "amount", amount);
    module_json::write_float(out, "tone", tone);
    out += '}';
    return out;
}

bool DrivePedal::load(const std::string& json) {
    module_json::read(json, "model", model);
    module_json::read(json, "amount", amount);
    module_json::read(json, "tone", tone);
    return true;
}

} // namespace guitar_pedals

void guitar_pedals::DrivePedal::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    process(ctx.buffer, ctx.num_samples, ctx.sample_rate);
}
