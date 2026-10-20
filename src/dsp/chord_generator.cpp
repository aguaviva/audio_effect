#include "chord_generator.hpp"

#include "chord_catalog.hpp"

#include <algorithm>

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
    ImGui::TextUnformatted("Chord");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##chord", &index, chord_combo_items(), kChordCount)) {
        index = std::clamp(index, 0, kChordCount - 1);
        return true;
    }
    index = std::clamp(index, 0, kChordCount - 1);
    return false;
}
void ChordGenerator::process(const float* const* /*inputs*/, int /*input_count*/, float* output,
                             size_t sample_count, float sample_rate) {
    if (scratch_.size() != sample_count) {
        scratch_.assign(sample_count, 0);
    }
    chord_.render(index, scratch_.data(), sample_count / 2, 2,
                  static_cast<uint32_t>(sample_rate));
    for (size_t i = 0; i < sample_count; ++i) {
        output[i] = static_cast<float>(scratch_[i]) / 2147483648.0f;
    }
}
