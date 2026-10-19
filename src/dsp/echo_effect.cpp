#include "echo_effect.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

EchoEffect::EchoEffect(size_t max_delay_samples)
    : delay_left_(max_delay_samples, 0.0f), delay_right_(max_delay_samples, 0.0f) {}

void EchoEffect::process(float* samples, size_t sample_count, float delay_ms,
                         float feedback, float mix, float sample_rate) {
    size_t delay_samples = static_cast<size_t>(delay_ms * 0.001f * sample_rate);
    delay_samples = std::clamp<size_t>(delay_samples, 1, delay_left_.size() - 1);
    feedback = std::clamp(feedback, 0.0f, 0.9f);
    mix = std::clamp(mix, 0.0f, 1.0f);

    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        size_t read_index = (write_index_ + delay_left_.size() - delay_samples) % delay_left_.size();
        float input_left = samples[i];
        float input_right = samples[i + 1];
        float delayed_left = delay_left_[read_index];
        float delayed_right = delay_right_[read_index];
        delay_left_[write_index_] = std::clamp(input_left + delayed_left * feedback, -1.0f, 1.0f);
        delay_right_[write_index_] = std::clamp(input_right + delayed_right * feedback, -1.0f, 1.0f);
        samples[i] = input_left * (1.0f - mix) + delayed_left * mix;
        samples[i + 1] = input_right * (1.0f - mix) + delayed_right * mix;
        write_index_ = (write_index_ + 1) % delay_left_.size();
    }
}

void EchoEffect::reset() {
    std::fill(delay_left_.begin(), delay_left_.end(), 0.0f);
    std::fill(delay_right_.begin(), delay_right_.end(), 0.0f);
    write_index_ = 0;
}

void EchoEffect::render(std::atomic<bool>& enabled, std::atomic<float>& delay_ms,
                        std::atomic<float>& feedback, std::atomic<float>& mix,
                        int& selected_preset, bool& controls_changed) {
    bool active = enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Echo##enable", &active)) {
        enabled.store(active, std::memory_order_relaxed);
        controls_changed = true;
    }
    ImGui::Indent();
    ImGui::BeginDisabled(!active);

    static const char* preset_names[] = {"Custom", "Slapback", "Rock Echo", "Ambient Echo"};
    if (ImGui::Combo("Echo Preset", &selected_preset, preset_names, IM_ARRAYSIZE(preset_names)) && selected_preset != 0) {
        if (selected_preset == 1) {
            delay_ms.store(110.0f, std::memory_order_relaxed);
            feedback.store(0.15f, std::memory_order_relaxed);
            mix.store(0.2f, std::memory_order_relaxed);
        } else if (selected_preset == 2) {
            delay_ms.store(320.0f, std::memory_order_relaxed);
            feedback.store(0.35f, std::memory_order_relaxed);
            mix.store(0.25f, std::memory_order_relaxed);
        } else {
            delay_ms.store(600.0f, std::memory_order_relaxed);
            feedback.store(0.55f, std::memory_order_relaxed);
            mix.store(0.4f, std::memory_order_relaxed);
        }
        enabled.store(true, std::memory_order_relaxed);
        controls_changed = true;
    }

    float delay = delay_ms.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Echo Delay (ms)", &delay, 20.0f, 1000.0f, "%.0f ms")) {
        delay_ms.store(delay, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = true;
    }
    float feedback_value = feedback.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Echo Feedback", &feedback_value, 0.0f, 0.9f, "%.2f")) {
        feedback.store(feedback_value, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = true;
    }
    float mix_value = mix.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Echo Mix", &mix_value, 0.0f, 1.0f, "%.2f")) {
        mix.store(mix_value, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = true;
    }

    ImGui::EndDisabled();
    ImGui::Unindent();
    ImGui::Separator();
}

void EchoEffect::Params::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "delay_ms", delay_ms);
    module_json::write_float(out, first, "feedback", feedback);
    module_json::write_float(out, first, "mix", mix);
}
bool EchoEffect::Params::read(const std::string& key, double num, bool, const std::string&, int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "delay_ms") {
        delay_ms = static_cast<float>(num);
        return true;
    }
    if (key == "feedback") {
        feedback = static_cast<float>(num);
        return true;
    }
    if (key == "mix") {
        mix = static_cast<float>(num);
        return true;
    }
    return false;
}
