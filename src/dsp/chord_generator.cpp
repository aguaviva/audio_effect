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
void ChordGenerator::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    if (scratch_.size() != ctx.num_samples) {
        scratch_.assign(ctx.num_samples, 0);
    }
    chord_.render(index, scratch_.data(), ctx.num_samples / 2, 2,
                  static_cast<uint32_t>(ctx.sample_rate));
    for (size_t i = 0; i < ctx.num_samples; ++i) {
        ctx.buffer[i] = static_cast<float>(scratch_[i]) / 2147483648.0f;
    }
}
