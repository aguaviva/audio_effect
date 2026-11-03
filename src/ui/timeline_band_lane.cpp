#include "timeline_band_lane.hpp"

#include "imgui.h"
#include "ui/timeline.hpp"

#include <algorithm>

void draw_band(Timeline& timeline, const char* label, bool active) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 lane_min = timeline.LaneMin();
    const ImVec2 lane_max = timeline.LaneMax();
    timeline.PushLaneClip();
    const float x0 = std::max(timeline.XAt(0.0), lane_min.x);
    const ImVec2 min(x0, lane_min.y + 6.0f);
    const ImVec2 max(lane_max.x, lane_max.y - 7.0f);
    draw->AddRectFilled(min, max, active ? IM_COL32(55, 110, 95, 255) : IM_COL32(45, 52, 55, 255));
    if (label && label[0]) {
        const float y = 0.5f * (min.y + max.y - ImGui::GetTextLineHeight());
        draw->AddText(ImVec2(x0 + 6.0f, y),
                      ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
    }
    timeline.PopLaneClip();
}
