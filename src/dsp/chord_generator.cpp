#include "chord_generator.hpp"

#include "chord_catalog.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

std::string ChordGenerator::save() const {
    std::string out = "{";
    module_json::write_int(out, "chord_index", index);
    out += '}';
    return out;
}

bool ChordGenerator::load(const std::string& json) {
    module_json::read(json, "chord_index", index);
    index = std::clamp(index, 0, kChordCount - 1);
    return true;
}

bool ChordGenerator::render() {
    if (ImGui::Button(playing ? "Pause###play" : "Play###play")) {
        playing = !playing;
    }
    ImGui::TextUnformatted("Chord");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##chord", &index, chord_combo_items(), kChordCount)) {
        index = std::clamp(index, 0, kChordCount - 1);
        return true;
    }
    index = std::clamp(index, 0, kChordCount - 1);
    return false;
}
void ChordGenerator::process(const float* const* /*inputs*/, int /*input_count*/,
                             size_t sample_count, float sample_rate) {
    float* output = output_buffer(0);
    if (!output) {
        return;
    }
    if (!playing) {
        std::fill(output, output + sample_count, 0.0f);
        return;
    }
    const int chord = std::clamp(index, 0, kChordCount - 1);
    if (chord != active_chord_) {
        phases_.fill(0.0);
        active_chord_ = chord;
        frequencies_ = chord_note_frequencies(chord);
    }
    constexpr double two_pi = 6.28318530717958647692;
    constexpr double gain = 0.90 / kChordVoices;
    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        double mixed = 0.0;
        for (size_t note = 0; note < phases_.size(); ++note) {
            mixed += std::sin(phases_[note]);
            phases_[note] += two_pi * frequencies_[note] / sample_rate;
            if (phases_[note] >= two_pi) {
                phases_[note] -= two_pi;
            }
        }
        output[i] = output[i + 1] = static_cast<float>(mixed * gain);
    }
}
