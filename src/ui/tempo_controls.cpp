#include "tempo_controls.hpp"

#include "imgui.h"
#include "model/tempo_grid.hpp"

#include <iterator>

bool tempo_controls(TempoGrid& grid) {
    bool changed = false;
    ImGui::PushID("tempo");
    int signature[2] = {grid.numerator(), grid.denominator()};
    ImGui::SetNextItemWidth(70.0f);
    if (ImGui::DragInt2("##signature", signature, 0.05f, 1, 32)) {
        grid.set_time_signature(signature[0], signature[1]);
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Time signature: beats per bar / note value");
    }
    ImGui::SameLine();
    float bpm = static_cast<float>(grid.bpm());
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::DragFloat("##bpm", &bpm, 0.25f, 20.0f, 400.0f,
                         grid.has_tempo_changes() ? "%.1f bpm*" : "%.1f bpm")) {
        grid.set_bpm(bpm);
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(grid.has_tempo_changes()
                              ? "Tempo at the start; * the song's tempo changes after it.\n"
                                "Editing it sets one tempo for the whole song."
                              : "Tempo");
    }
    ImGui::SameLine();
    static const int kDivisions[] = {1, 2, 3, 4, 6, 8};
    static const char* kNames[] = {"Snap: beat", "Snap: 1/2 beat", "Snap: 1/3 beat",
                                   "Snap: 1/4 beat", "Snap: 1/6 beat", "Snap: 1/8 beat"};
    int current = 0;
    for (int i = 0; i < static_cast<int>(std::size(kDivisions)); ++i) {
        if (kDivisions[i] == grid.division()) {
            current = i;
        }
    }
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::Combo("##division", &current, kNames, static_cast<int>(std::size(kNames)))) {
        grid.set_division(kDivisions[current]);
        changed = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Clips and piano roll notes snap to this grid");
    }
    ImGui::PopID();
    return changed;
}
