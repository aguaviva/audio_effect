#include "preamp.hpp"

#include <cmath>

#include "imgui.h"

void Preamp::process(float* samples, size_t num_samples, float gain_db) {
    float gain = std::pow(10.0f, gain_db / 20.0f);
    for (size_t i = 0; i < num_samples; ++i) {
        samples[i] *= gain;
    }
}

void Preamp::render(std::atomic<bool>& enabled, std::atomic<float>& gain_db,
                    int& selected_preset, bool& controls_changed) {
    bool active = enabled.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Preamp##enable", &active)) {
        enabled.store(active, std::memory_order_relaxed);
        controls_changed = true;
    }
    ImGui::Indent();
    ImGui::BeginDisabled(!active);

    static const char* preset_names[] = {
        "Custom", "Unity (0 dB)", "Clean Boost (+6 dB)",
        "Mid Boost (+9 dB)", "Hot Boost (+12 dB)"
    };
    if (ImGui::Combo("Preamp Preset", &selected_preset, preset_names, IM_ARRAYSIZE(preset_names)) && selected_preset != 0) {
        static const float gains[] = {0.0f, 0.0f, 6.0f, 9.0f, 12.0f};
        gain_db.store(gains[selected_preset], std::memory_order_relaxed);
        enabled.store(true, std::memory_order_relaxed);
        controls_changed = true;
    }

    float gain = gain_db.load(std::memory_order_relaxed);
    if (ImGui::SliderFloat("Gain (dB)", &gain, -12.0f, 24.0f, "%.1f dB")) {
        gain_db.store(gain, std::memory_order_relaxed);
        selected_preset = 0;
        controls_changed = true;
    }

    ImGui::EndDisabled();
    ImGui::Unindent();
    ImGui::Separator();
}

void Preamp::Params::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "gain_db", gain_db);
}
bool Preamp::Params::read(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "gain_db" && kind == module_json::kKindNumber) {
        gain_db = static_cast<float>(num);
        return true;
    }
    return false;
}
