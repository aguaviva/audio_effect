#include "echo_effect.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

EchoEffect::EchoEffect(size_t max_delay_samples)
    : delay_left_(max_delay_samples, 0.0f), delay_right_(max_delay_samples, 0.0f) {}

void EchoEffect::process(float* samples, size_t sample_count, float sample_rate) {
    size_t delay_samples = static_cast<size_t>(delay_ms * 0.001f * sample_rate);
    delay_samples = std::clamp<size_t>(delay_samples, 1, delay_left_.size() - 1);
    float fb = std::clamp(feedback, 0.0f, 0.9f);
    float wet = std::clamp(mix, 0.0f, 1.0f);

    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        size_t read_index = (write_index_ + delay_left_.size() - delay_samples) % delay_left_.size();
        float input_left = samples[i];
        float input_right = samples[i + 1];
        float delayed_left = delay_left_[read_index];
        float delayed_right = delay_right_[read_index];
        delay_left_[write_index_] = std::clamp(input_left + delayed_left * fb, -1.0f, 1.0f);
        delay_right_[write_index_] = std::clamp(input_right + delayed_right * fb, -1.0f, 1.0f);
        samples[i] = input_left * (1.0f - wet) + delayed_left * wet;
        samples[i + 1] = input_right * (1.0f - wet) + delayed_right * wet;
        write_index_ = (write_index_ + 1) % delay_left_.size();
    }
}

void EchoEffect::reset() {
    std::fill(delay_left_.begin(), delay_left_.end(), 0.0f);
    std::fill(delay_right_.begin(), delay_right_.end(), 0.0f);
    write_index_ = 0;
}

bool EchoEffect::render() {
    bool changed = false;

    static const char* preset_names[] = {"Custom", "Slapback", "Rock Echo", "Ambient Echo"};

    ImGui::TextUnformatted("Preset");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##echo_preset", &selected_preset, preset_names, IM_ARRAYSIZE(preset_names))) {
        if (selected_preset == 1) {
            delay_ms = 110.0f;
            feedback = 0.15f;
            mix = 0.2f;
        } else if (selected_preset == 2) {
            delay_ms = 320.0f;
            feedback = 0.35f;
            mix = 0.25f;
        } else if (selected_preset == 3) {
            delay_ms = 600.0f;
            feedback = 0.55f;
            mix = 0.4f;
        }
        changed = true;
    }

    ImGui::TextUnformatted("Delay ms");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##echo_delay", &delay_ms, 10.0f, 1000.0f, "%.0f")) {
        selected_preset = 0;
        changed = true;
    }
    ImGui::TextUnformatted("Feedback");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##echo_fb", &feedback, 0.0f, 0.95f, "%.2f")) {
        selected_preset = 0;
        changed = true;
    }
    ImGui::TextUnformatted("Mix");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##echo_mix", &mix, 0.0f, 1.0f, "%.2f")) {
        selected_preset = 0;
        changed = true;
    }

    return changed;
}

std::string EchoEffect::save() const {
    std::string out = "{";
    module_json::write_float(out, "delay_ms", delay_ms);
    module_json::write_float(out, "feedback", feedback);
    module_json::write_float(out, "mix", mix);
    module_json::write_int(out, "selected_preset", selected_preset);
    out += '}';
    return out;
}

bool EchoEffect::load(const std::string& json) {
    module_json::read(json, "delay_ms", delay_ms);
    module_json::read(json, "feedback", feedback);
    module_json::read(json, "mix", mix);
    module_json::read(json, "selected_preset", selected_preset);
    return true;
}

void EchoEffect::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    process(ctx.buffer, ctx.num_samples, ctx.sample_rate);
}
