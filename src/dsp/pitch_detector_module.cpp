#include "pitch_detector_module.hpp"

#include "imgui.h"

bool PitchDetectorModule::render() {
    const float frequency_hz = frequency_hz_.load(std::memory_order_relaxed);
    if (frequency_hz > 0.0f) {
        char note[16];
        pitch_frequency_to_note(frequency_hz, note, sizeof(note));
        ImGui::Text("Freq  %.1f Hz", frequency_hz);
        ImGui::Text("Note  %s", note);
    } else {
        ImGui::TextUnformatted("Freq  --");
        ImGui::TextUnformatted("Note  --");
    }
    return false;
}

void PitchDetectorModule::process(const float* const* inputs, int input_count,
                                  size_t sample_count, float sample_rate) {
    const float* in = pass_through(inputs, input_count, sample_count);
    if (!in) {
        return;
    }
    if (!detector_ || detector_rate_ != sample_rate) {
        detector_ = std::make_unique<PitchDetector>(sample_rate);
        detector_rate_ = sample_rate;
    }
    float frequency_hz = 0.0f;
    if (detector_->process(in, sample_count, 2, frequency_hz)) {
        frequency_hz_.store(frequency_hz, std::memory_order_relaxed);
    }
}
