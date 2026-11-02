#include "vu_meter.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

std::string VuMeter::save() const {
    std::string out = "{";
    module_json::write_float(out, "display_gain", display_gain);
    module_json::write_float(out, "decay", decay);
    out += '}';
    return out;
}

bool VuMeter::load(const std::string& json) {
    module_json::read(json, "display_gain", display_gain);
    module_json::read(json, "decay", decay);
    return true;
}

static float linear_to_db(float linear) {
    return 20.0f * std::log10(std::max(linear, 1.0e-6f));
}

bool VuMeter::render() {
    bool changed = false;
    //ImGui::TextUnformatted("Gain");
    //ImGui::SetNextItemWidth(-1.0f);
    //changed |= ImGui::SliderFloat("##vu_gain", &display_gain, 0.25f, 4.0f, "%.2f");
    ImGui::TextUnformatted("Decay");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##vu_decay", &decay, 0.5f, 0.99f, "%.2f");

    const float peak = peak_.load(std::memory_order_relaxed);
    const float rms = rms_.load(std::memory_order_relaxed);
    ImGui::Text("Peak %.1f dB", linear_to_db(peak));
    ImGui::ProgressBar(std::clamp(peak, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "");
    ImGui::Text("RMS  %.1f dB", linear_to_db(rms));
    ImGui::ProgressBar(std::clamp(rms * 1.5f, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "");
    return changed;
}

void VuMeter::process(const float* const* inputs, int input_count,
                      size_t sample_count, float /*sample_rate*/) {
    const float* in = input_count > 0 ? inputs[0] : nullptr;

    const size_t frames = in ? sample_count / 2 : 0;
    float peak = 0.0f;
    double sum_sq = 0.0;
    for (size_t i = 0; i < frames; ++i) {
        const float s = 0.5f * (in[i * 2] + in[i * 2 + 1]);
        peak = std::max(peak, std::abs(s));
        sum_sq += static_cast<double>(s) * s;
    }
    const float rms = frames ? static_cast<float>(std::sqrt(sum_sq / frames)) : 0.0f;

    const float d = std::clamp(decay, 0.5f, 0.99f);
    peak_hold_ = std::max(peak, peak_hold_ * d);
    rms_hold_ = std::max(rms, rms_hold_ * d);
    peak_.store(peak_hold_, std::memory_order_relaxed);
    rms_.store(rms_hold_, std::memory_order_relaxed);
}
