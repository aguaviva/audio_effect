#include "spectrum.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

std::string Spectrum::save() const {
    std::string out = "{";
    module_json::write_float(out, "display_gain", display_gain);
    out += '}';
    return out;
}

bool Spectrum::load(const std::string& json) {
    module_json::read(json, "display_gain", display_gain);
    return true;
}

bool Spectrum::render() {
    //ImGui::TextUnformatted("Gain");
    //ImGui::SetNextItemWidth(-1.0f);
    //return ImGui::SliderFloat("##spec_gain", &display_gain, 0.25f, 4.0f, "%.2f");
    float bins[kBins];
    {
        std::lock_guard<std::mutex> lock(bins_mutex_);
        std::copy(bins_, bins_ + kBins, bins);
    }
    ImGui::PlotHistogram("##spectrum", bins, kBins, 0, nullptr, 0.0f, 1.0f, ImVec2(-1.0f, 48.0f));
    return false;
}

void Spectrum::process(const float* const* inputs, int input_count, float* const* outputs,
                       size_t sample_count, float /*sample_rate*/) {
    const float* in = pass_through(inputs, input_count, outputs, sample_count);

    constexpr size_t kN = 256;
    constexpr float kPi = 3.14159265358979323846f;
    const size_t n = in ? std::min(sample_count / 2, kN) : 0;
    float bins[kBins]{};
    if (n >= 2) {
        float windowed[kN]{};
        for (size_t i = 0; i < n; ++i) {
            const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * i / (n - 1)));
            windowed[i] = 0.5f * (in[i * 2] + in[i * 2 + 1]) * w;
        }
        // Cheap linear DFT bins (k = 1..32) — enough for a small in-node spectrum.
        for (int b = 0; b < kBins; ++b) {
            const int k = b + 1;
            float re = 0.0f;
            float im = 0.0f;
            for (size_t i = 0; i < kN; ++i) {
                const float angle = 2.0f * kPi * static_cast<float>(k * static_cast<int>(i)) / kN;
                re += windowed[i] * std::cos(angle);
                im -= windowed[i] * std::sin(angle);
            }
            const float mag = std::sqrt(re * re + im * im) / kN;
            bins[b] = std::min(mag * display_gain * 8.0f, 1.0f);
        }
    }

    std::unique_lock<std::mutex> lock(bins_mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        std::copy(bins, bins + kBins, bins_);
    }
}
