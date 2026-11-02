#include "agc.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

static float time_coef(float ms, float sample_rate) {
    return std::exp(-1.0f / std::max(1.0f, ms * 0.001f * sample_rate));
}

void Agc::process(const float* const* inputs, int input_count,
                  size_t sample_count, float sample_rate) {
    float* output = output_buffer(0);
    if (!output) {
        return;
    }
    copy_input(inputs, input_count, 0, output, sample_count);

    constexpr float kDbToLn = 0.11512925f; // ln(10) / 20
    const float rms_coef = time_coef(kRmsWindowMs, sample_rate);
    const float attack_coef = time_coef(attack_ms, sample_rate);
    const float release_coef = time_coef(release_ms, sample_rate);
    const float max_gain = std::max(0.0f, max_gain_db);

    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        const float l = output[i];
        const float r = output[i + 1];
        power_ = rms_coef * power_ + (1.0f - rms_coef) * 0.5f * (l * l + r * r);
        const float level_db = 10.0f * std::log10(std::max(power_, 1.0e-12f));

        if (level_db > gate_db) {
            const float wanted = std::clamp(target_db - level_db, kMaxCutDb, max_gain);
            const float coef = wanted < gain_db_ ? attack_coef : release_coef;
            gain_db_ = coef * gain_db_ + (1.0f - coef) * wanted;
        }

        const float gain = std::exp(gain_db_ * kDbToLn);
        output[i] = l * gain;
        output[i + 1] = r * gain;
    }
    shown_gain_db_.store(gain_db_, std::memory_order_relaxed);
}

bool Agc::render() {
    bool changed = false;
    ImGui::TextUnformatted("Target dB");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##agc_target", &target_db, -40.0f, -3.0f, "%.1f");
    ImGui::TextUnformatted("Max gain dB");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##agc_max", &max_gain_db, 0.0f, 40.0f, "%.1f");
    ImGui::TextUnformatted("Attack ms");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##agc_atk", &attack_ms, 1.0f, 200.0f, "%.0f");
    ImGui::TextUnformatted("Release ms");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##agc_rel", &release_ms, 50.0f, 3000.0f, "%.0f");
    ImGui::TextUnformatted("Gate dB");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##agc_gate", &gate_db, -90.0f, -20.0f, "%.0f");

    const float g = gain_db();
    ImGui::Text("Gain %+.1f dB", g);
    const float range = std::max(1.0f, max_gain_db) - kMaxCutDb;
    ImGui::ProgressBar(std::clamp((g - kMaxCutDb) / range, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "");
    return changed;
}

std::string Agc::save() const {
    std::string out = "{";
    module_json::write_float(out, "target_db", target_db);
    module_json::write_float(out, "max_gain_db", max_gain_db);
    module_json::write_float(out, "attack_ms", attack_ms);
    module_json::write_float(out, "release_ms", release_ms);
    module_json::write_float(out, "gate_db", gate_db);
    out += '}';
    return out;
}

bool Agc::load(const std::string& json) {
    module_json::read(json, "target_db", target_db);
    module_json::read(json, "max_gain_db", max_gain_db);
    module_json::read(json, "attack_ms", attack_ms);
    module_json::read(json, "release_ms", release_ms);
    module_json::read(json, "gate_db", gate_db);
    return true;
}
