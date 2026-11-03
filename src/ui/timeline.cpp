#include "timeline.hpp"

#include "model/tempo_grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

const ImU32 kRulerBackground = IM_COL32(40, 43, 47, 255);
const ImU32 kHeaderBackground = IM_COL32(36, 39, 42, 255);
const ImU32 kLaneBackground = IM_COL32(31, 34, 37, 255);
const ImU32 kRowLine = IM_COL32(61, 65, 68, 255);
const ImU32 kBarLine = IM_COL32(66, 71, 75, 255);
const ImU32 kBeatLine = IM_COL32(48, 52, 55, 255);
const ImU32 kDivisionLine = IM_COL32(39, 42, 45, 255);
const ImU32 kPlayheadColor = IM_COL32(255, 200, 60, 255);

float RulerHeight() {
    return Timeline::TitleHeight();
}

} // namespace

float Timeline::TitleHeight() {
    return ImGui::GetTextLineHeight() + 8.0f;
}

float Timeline::XAt(double seconds) const {
    return lanes_x_ - scroll_x_ + static_cast<float>(seconds / seconds_per_pixel_);
}

double Timeline::SecondsAt(float x) const {
    return (x - lanes_x_ + scroll_x_) * seconds_per_pixel_;
}

double Timeline::MouseSeconds() const {
    return std::max(0.0, SecondsAt(ImGui::GetIO().MousePos.x));
}

void Timeline::ExtendTo(double seconds) {
    next_length_ = std::max(next_length_, seconds);
}

void Timeline::PushLaneClip() const {
    ImGui::GetWindowDrawList()->PushClipRect(
        ImVec2(std::max(lane_min_.x, view_min_.x), std::max(lane_min_.y, view_min_.y)),
        ImVec2(std::min(lane_max_.x, view_max_.x), std::min(lane_max_.y, view_max_.y)));
}

void Timeline::PopLaneClip() const {
    ImGui::GetWindowDrawList()->PopClipRect();
}

void Timeline::Begin(const char* id, double cursor, ImVec2 size) {
    cursor_ = cursor;
    seeked_ = false;
    ruler_min_ = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::InvisibleButton("##ruler", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x),
                                             RulerHeight()));
    ruler_active_ = ImGui::IsItemActive();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild(id, size, ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    view_min_ = draw->GetClipRectMin();
    view_max_ = draw->GetClipRectMax();
    lanes_x_ = ImGui::GetWindowPos().x + kHeaderWidth;
    scroll_x_ = ImGui::GetScrollX();
    row_y_ = 0.0f;
    if (ruler_active_) {
        seeked_ = true;
        seek_ = MouseSeconds();
    }
    HandleWheel();
}

bool Timeline::Seeked(double* seconds) const {
    if (seeked_) {
        *seconds = seek_;
    }
    return seeked_;
}

void Timeline::HandleWheel() {
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsWindowHovered() || (io.MouseWheel == 0.0f && io.MouseWheelH == 0.0f)) {
        return;
    }
    if (io.MousePos.x < lanes_x_) {
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * kTrackHeight);
        return;
    }
    if (io.MouseWheel != 0.0f && !io.KeyShift) {
        const float mouse_x = io.MousePos.x - lanes_x_;
        const double seconds = (scroll_x_ + mouse_x) * seconds_per_pixel_;
        seconds_per_pixel_ = std::clamp(seconds_per_pixel_ * std::pow(0.8, io.MouseWheel),
                                        1.0 / 48000.0, 1.0);
        ImGui::SetScrollX(std::max(0.0f, static_cast<float>(seconds / seconds_per_pixel_) - mouse_x));
    } else {
        const float wheel = io.MouseWheelH != 0.0f ? io.MouseWheelH : io.MouseWheel;
        ImGui::SetScrollX(scroll_x_ - wheel * 0.1f * (view_max_.x - lanes_x_));
    }
}

void Timeline::End() {
    // Room to scroll a little past the end of everything shown.
    length_ = std::max(60.0, next_length_ + 10.0);
    next_length_ = 0.0;
    const float rows_bottom = ImGui::GetWindowPos().y - ImGui::GetScrollY() + row_y_;
    DrawCursor(view_min_.y, std::min(rows_bottom, view_max_.y));
    ImGui::SetCursorPos(ImVec2(kHeaderWidth + static_cast<float>(length_ / seconds_per_pixel_), row_y_));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::EndChild();
    DrawRuler();
}

template <typename F>
void Timeline::ForEachGridLine(F line) const {
    constexpr float kMinBeatPixels = 12.0f;
    constexpr float kMinLabelPixels = 56.0f;
    const TempoGrid& grid = song_grid();
    const double bar_beats = grid.beats_per_bar();
    const double first_beat = grid.beat_at_seconds(std::max(0.0, SecondsAt(lanes_x_)));
    const double last_beat = grid.beat_at_seconds(std::max(0.0, SecondsAt(view_max_.x)));
    // Spacing at the left edge; a tempo map can change it further on.
    const float beat_pixels = static_cast<float>(
        (grid.seconds_at_beat(first_beat + 1.0) - grid.seconds_at_beat(first_beat)) /
        seconds_per_pixel_);
    const float bar_pixels = beat_pixels * static_cast<float>(bar_beats);
    int bar_step = 1; // bars between bar lines
    while (bar_pixels * bar_step < kMinBeatPixels * 2.0f) {
        bar_step *= 2;
    }
    int label_step = bar_step; // bars between labels
    while (bar_pixels * label_step < kMinLabelPixels) {
        label_step *= 2;
    }
    const bool beats = beat_pixels >= kMinBeatPixels;
    const int division = grid.division();
    const bool divisions = beats && beat_pixels / division >= kMinBeatPixels * 0.75f;
    const bool beat_labels = beat_pixels >= kMinLabelPixels;

    const long first_bar = static_cast<long>(std::floor(first_beat / bar_beats));
    for (long bar = first_bar - first_bar % bar_step; bar * bar_beats <= last_beat; bar += bar_step) {
        const double bar_start = bar * bar_beats;
        line(grid.seconds_at_beat(bar_start), 0, bar, 0, bar % label_step == 0);
        if (!beats) {
            continue;
        }
        const int steps = static_cast<int>(std::ceil(bar_beats * division - 1e-9));
        for (int s = 1; s < steps; ++s) {
            const double beat = bar_start + static_cast<double>(s) / division;
            if (beat > last_beat) {
                break;
            }
            const bool on_beat = s % division == 0;
            if (on_beat || divisions) {
                line(grid.seconds_at_beat(beat), on_beat ? 1 : 2, bar, s / division,
                     on_beat && beat_labels);
            }
        }
    }
}

void Timeline::DrawTicks(float top, float bottom) const {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ForEachGridLine([&](double seconds, int kind, long, int, bool) {
        const float x = XAt(seconds);
        draw->AddLine(ImVec2(x, top), ImVec2(x, bottom),
                      kind == 0 ? kBarLine : kind == 1 ? kBeatLine : kDivisionLine);
    });
}

void Timeline::DrawRuler() const {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(view_max_.x, ruler_min_.y + RulerHeight());
    draw->AddRectFilled(ruler_min_, max, kRulerBackground);
    draw->PushClipRect(ImVec2(lanes_x_, ruler_min_.y), max, true);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    ForEachGridLine([&](double seconds, int kind, long bar, int beat, bool label) {
        const float x = XAt(seconds);
        const float top = ruler_min_.y + RulerHeight() * (kind == 0 ? 0.0f : kind == 1 ? 0.55f : 0.75f);
        draw->AddLine(ImVec2(x, top), ImVec2(x, max.y), kind == 0 ? kRowLine : kBeatLine);
        if (label) {
            char name[40];
            if (kind == 0) {
                std::snprintf(name, sizeof(name), "%ld", bar + 1);
            } else {
                std::snprintf(name, sizeof(name), "%ld.%d", bar + 1, beat + 1);
            }
            draw->AddText(ImVec2(x + 3.0f, ruler_min_.y + 2.0f), kind == 0 ? text : dim, name);
        }
    });
    const float x = XAt(cursor_);
    draw->AddLine(ImVec2(x, ruler_min_.y), ImVec2(x, max.y), kPlayheadColor, 2.0f);
    draw->AddTriangleFilled(ImVec2(x - 6.0f, max.y - 9.0f), ImVec2(x + 6.0f, max.y - 9.0f),
                            ImVec2(x, max.y), kPlayheadColor);
    draw->PopClipRect();
    draw->AddLine(ImVec2(ruler_min_.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), kRowLine);
}

void Timeline::DrawCursor(float top, float bottom) const {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(ImVec2(lanes_x_, view_min_.y), view_max_, true);
    const float x = XAt(cursor_);
    draw->AddLine(ImVec2(x, top), ImVec2(x, bottom), kPlayheadColor, 2.0f);
    draw->PopClipRect();
}

void Timeline::BeginTrack(const char* id, float height) {
    ImGui::PushID(id);
    const float top = ImGui::GetWindowPos().y - ImGui::GetScrollY() + row_y_;
    row_y_ += height;
    lane_min_ = ImVec2(lanes_x_, top);
    lane_max_ = ImVec2(view_max_.x, top + height);

    ImGui::SetCursorScreenPos(lane_min_);
    ImGui::InvisibleButton("##lane", ImVec2(std::max(1.0f, lane_max_.x - lane_min_.x), height));
    lane_activated_ = ImGui::IsItemActivated();
    lane_active_ = ImGui::IsItemActive();
    lane_hovered_ = ImGui::IsItemHovered();
    lane_claimed_ = false;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(lane_min_, lane_max_, kLaneBackground);
    DrawTicks(top, top + height);
    draw->AddRectFilled(ImVec2(view_min_.x, top), ImVec2(lanes_x_, top + height), kHeaderBackground);
    draw->AddLine(ImVec2(view_min_.x, top + height - 1.0f), ImVec2(view_max_.x, top + height - 1.0f),
                  kRowLine);
    draw->AddLine(ImVec2(lanes_x_ - 1.0f, top), ImVec2(lanes_x_ - 1.0f, top + height), kRowLine);

    ImGui::PushClipRect(ImVec2(view_min_.x, top), ImVec2(lanes_x_ - 4.0f, top + height), true);
    ImGui::SetCursorScreenPos(ImVec2(view_min_.x + 6.0f, top + 4.0f));
    ImGui::BeginGroup();
}

void Timeline::EndTrack() {
    if (lane_active_ && !lane_claimed_) {
        seeked_ = true;
        seek_ = MouseSeconds();
    }
    ImGui::EndGroup();
    ImGui::PopClipRect();
    ImGui::PopID();
}
