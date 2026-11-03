#include "timeline_wav_lane.hpp"

#include "dsp/wav_generator.hpp"
#include "imgui.h"
#include "model/waveform_peaks.hpp"
#include "ui/timeline.hpp"

#include <algorithm>
#include <cmath>

namespace {
const ImU32 kClipBackground = IM_COL32(45, 75, 120, 255);
const ImU32 kClipDragged = IM_COL32(70, 110, 170, 255);
const ImU32 kClipForeground = IM_COL32(150, 200, 255, 255);
const ImU32 kClipLabel = IM_COL32(220, 230, 240, 200);
const ImU32 kClipOutline = IM_COL32(255, 255, 255, 230);
} // namespace

void draw_wav_clip(Timeline& timeline, const WavClip& clip, const WaveformPeaks& peaks,
                   double start, double sample_rate, const ClipStyle& style) {
    const size_t file_frames = clip.interleaved_stereo.size() / 2;
    const size_t offset = std::min(style.offset, file_frames);
    const int64_t frames = static_cast<int64_t>(std::min(style.length, file_frames - offset));
    const double end = start + static_cast<double>(frames) / sample_rate;
    if (!style.repeat) {
        timeline.ExtendTo(end);
    }
    const ImVec2 lane_min = timeline.LaneMin();
    const ImVec2 lane_max = timeline.LaneMax();
    const float x0 = timeline.XAt(start);
    const float x1 = std::max(timeline.XAt(end), x0 + 1.0f);
    if (x1 < lane_min.x || x0 > lane_max.x) {
        return;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    timeline.PushLaneClip();
    const float top = lane_min.y;
    const float height = lane_max.y - lane_min.y;
    const ImVec2 box_min(x0, top + 2.0f);
    const ImVec2 box_max(x1, top + height - 3.0f);
    draw->AddRectFilled(box_min, box_max, style.highlight ? kClipDragged : kClipBackground, 3.0f);
    const float mid = top + height * 0.5f;
    const float half = height * 0.5f - 4.0f;
    const auto frame_at = [&](float x) {
        return static_cast<int64_t>(std::floor((timeline.SecondsAt(x) - start) * sample_rate));
    };
    for (float x = std::floor(std::max(x0, lane_min.x)); x < std::min(x1, lane_max.x); x += 1.0f) {
        const int64_t begin = std::max<int64_t>(frame_at(x), 0);
        const int64_t stop = std::min(frame_at(x + 1.0f), frames);
        if (stop <= 0 || begin >= frames) {
            continue;
        }
        const auto [lo, hi] = peaks.range(clip, offset + static_cast<size_t>(begin),
                                          offset + static_cast<size_t>(std::max(stop, begin + 1)));
        draw->AddLine(ImVec2(x + 0.5f, mid - hi * half), ImVec2(x + 0.5f, mid - lo * half + 1.0f),
                      kClipForeground);
    }
    if (style.label && style.label[0] && x1 - x0 > 24.0f) {
        draw->PushClipRect(box_min, box_max, true);
        draw->AddText(ImVec2(std::max(x0, lane_min.x) + 4.0f, top + 3.0f), kClipLabel, style.label);
        draw->PopClipRect();
    }
    if (style.selected) {
        draw->AddRect(box_min, box_max, kClipOutline, 3.0f, 0, 2.0f);
    }
    timeline.PopLaneClip();
}
