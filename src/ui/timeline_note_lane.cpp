#include "timeline_note_lane.hpp"

#include "imgui.h"
#include "ui/timeline.hpp"

#include <algorithm>

namespace {
const ImU32 kBlockBackground = IM_COL32(45, 75, 120, 255);
const ImU32 kNote = IM_COL32(150, 200, 255, 255);
const ImU32 kSoundingNote = IM_COL32(240, 248, 255, 255);
} // namespace

void draw_notes(Timeline& timeline, const std::vector<TimelineNote>& notes, double start,
                double end) {
    if (notes.empty()) {
        return;
    }
    timeline.ExtendTo(end);
    int low = notes.front().pitch;
    int high = low;
    for (const TimelineNote& n : notes) {
        low = std::min(low, n.pitch);
        high = std::max(high, n.pitch);
    }
    // At least an octave tall, so a few close notes are not stretched over the whole lane.
    const int span = std::max(high - low + 1, 12);
    const int base = low - (span - (high - low + 1)) / 2;
    const ImVec2 lane_min = timeline.LaneMin();
    const ImVec2 lane_max = timeline.LaneMax();
    const float top = lane_min.y + 2.0f;
    const float bottom = lane_max.y - 3.0f;
    constexpr float kPad = 3.0f;
    const float row = (bottom - top - 2.0f * kPad) / static_cast<float>(span);
    const double cursor = timeline.Cursor();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    timeline.PushLaneClip();
    draw->AddRectFilled(ImVec2(timeline.XAt(start), top), ImVec2(timeline.XAt(end), bottom),
                        kBlockBackground, 3.0f);
    for (const TimelineNote& n : notes) {
        const float x0 = timeline.XAt(n.start);
        const float x1 = std::max(timeline.XAt(n.start + n.duration), x0 + 1.0f);
        if (x1 < lane_min.x || x0 > lane_max.x) {
            continue;
        }
        const float y = bottom - kPad - static_cast<float>(n.pitch - base + 1) * row;
        const bool sounding = cursor >= n.start && cursor < n.start + n.duration;
        draw->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + std::max(row - 1.0f, 2.0f)),
                            sounding ? kSoundingNote : kNote);
    }
    timeline.PopLaneClip();
}
