#include "timeline_group.hpp"

#include "imgui.h"
#include "ui/timeline.hpp"

void timeline_title_row(Timeline& timeline, const char* title, const char* tooltip,
                        const char* note) {
    timeline.BeginTrack("title", Timeline::TitleHeight());
    ImGui::TextUnformatted(title);
    if (tooltip && tooltip[0] && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
    if (note && note[0]) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", note);
    }
    timeline.EndTrack();
}
