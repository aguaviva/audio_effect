#include "noise_gate.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

void NoiseGate::reset() {
    envelope_ = 0.0f;
    gain_ = 0.0f;
}

void NoiseGate::process(float* interleaved, size_t sample_count, float sample_rate) {
    if (interleaved == nullptr || sample_count < 2 || sample_rate <= 0.0f) {
        return;
    }
    const float threshold =
        std::pow(10.0f, std::clamp(threshold_db, -80.0f, 0.0f) / 20.0f);
    const float attack_coef =
        std::exp(-1.0f / std::max(1.0f, attack_ms * 0.001f * sample_rate));
    const float release_coef =
        std::exp(-1.0f / std::max(1.0f, release_ms * 0.001f * sample_rate));

    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        const float level = 0.5f * (std::fabs(interleaved[i]) + std::fabs(interleaved[i + 1]));
        if (level > envelope_) {
            envelope_ = attack_coef * envelope_ + (1.0f - attack_coef) * level;
        } else {
            envelope_ = release_coef * envelope_ + (1.0f - release_coef) * level;
        }
        const float target = envelope_ >= threshold ? 1.0f : 0.0f;
        if (target > gain_) {
            gain_ = attack_coef * gain_ + (1.0f - attack_coef) * target;
        } else {
            gain_ = release_coef * gain_ + (1.0f - release_coef) * target;
        }
        interleaved[i] *= gain_;
        interleaved[i + 1] *= gain_;
    }
}

bool NoiseGate::render() {
    bool changed = false;
    ImGui::TextUnformatted("Thresh dB");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##gate_th", &threshold_db, -80.0f, -10.0f, "%.1f");
    ImGui::TextUnformatted("Attack ms");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##gate_atk", &attack_ms, 1.0f, 50.0f, "%.1f");
    ImGui::TextUnformatted("Release ms");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##gate_rel", &release_ms, 10.0f, 400.0f, "%.0f");
    return changed;
}

void NoiseGate::save(std::string& out, bool& first) const {
    module_json::write_float(out, first, "threshold_db", threshold_db);
    module_json::write_float(out, first, "attack_ms", attack_ms);
    module_json::write_float(out, first, "release_ms", release_ms);
}

bool NoiseGate::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "threshold_db") { threshold_db = static_cast<float>(num); return true; }
    if (key == "attack_ms") { attack_ms = static_cast<float>(num); return true; }
    if (key == "release_ms") { release_ms = static_cast<float>(num); return true; }
    return false;
}

void NoiseGate::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    process(ctx.buffer, ctx.num_samples, ctx.sample_rate);
}
