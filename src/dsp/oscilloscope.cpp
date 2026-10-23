#include "oscilloscope.hpp"

#include <algorithm>

#include "imgui.h"

std::string Oscilloscope::save() const {
    std::string out = "{";
    module_json::write_float(out, "display_gain", display_gain);
    out += '}';
    return out;
}

bool Oscilloscope::load(const std::string& json) {
    module_json::read(json, "display_gain", display_gain);
    return true;
}

bool Oscilloscope::render() {
    //ImGui::TextUnformatted("Gain");
    //ImGui::SetNextItemWidth(-1.0f);
    //return ImGui::SliderFloat("##scope_gain", &display_gain, 0.25f, 4.0f, "%.2f");
    float points[kPoints];
    {
        std::lock_guard<std::mutex> lock(points_mutex_);
        std::copy(points_, points_ + kPoints, points);
    }
    ImGui::PlotLines("##scope", points, kPoints, 0, nullptr, -1.0f, 1.0f, ImVec2(-1.0f, 48.0f));
    return false;
}

void Oscilloscope::process(const float* const* inputs, int input_count, float* const* outputs,
                           size_t sample_count, float /*sample_rate*/) {
    const float* in = pass_through(inputs, input_count, outputs, sample_count);

    const size_t frames = sample_count / 2;
    std::unique_lock<std::mutex> lock(points_mutex_, std::try_to_lock);
    if (!lock.owns_lock() || frames == 0) {
        return;
    }
    for (int i = 0; i < kPoints; ++i) {
        const size_t f = static_cast<size_t>(i) * (frames - 1) / (kPoints - 1);
        const float mono = in ? 0.5f * (in[f * 2] + in[f * 2 + 1]) : 0.0f;
        points_[i] = std::clamp(mono * display_gain, -1.5f, 1.5f);
    }
}
