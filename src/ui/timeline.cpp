#include "timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace {

const ImU32 kRulerBackground = IM_COL32(40, 43, 47, 255);
const ImU32 kHeaderBackground = IM_COL32(36, 39, 42, 255);
const ImU32 kLaneBackground = IM_COL32(31, 34, 37, 255);
const ImU32 kRowLine = IM_COL32(61, 65, 68, 255);
const ImU32 kTickLine = IM_COL32(52, 56, 59, 255);
const ImU32 kPlayheadColor = IM_COL32(255, 200, 60, 255);

float RulerHeight() {
    return ImGui::GetTextLineHeight() + 8.0f;
}

// Draws into the part of a rect inside the visible tracks, whatever clip rect is pushed.
class ScopedClip {
public:
    ScopedClip(ImDrawList* draw, ImVec2 min, ImVec2 max, ImVec2 view_min, ImVec2 view_max)
        : draw_(draw) {
        draw_->PushClipRect(ImVec2(std::max(min.x, view_min.x), std::max(min.y, view_min.y)),
                            ImVec2(std::min(max.x, view_max.x), std::min(max.y, view_max.y)));
    }
    ~ScopedClip() { draw_->PopClipRect(); }

private:
    ImDrawList* draw_;
};

} // namespace

WaveformPeaks::WaveformPeaks(const WavClip& clip) {
    const std::vector<float>& s = clip.interleaved_stereo;
    const size_t frames = s.size() / 2;
    std::vector<std::pair<float, float>> level;
    for (size_t f = 0; f < frames; f += kBaseFrames) {
        const size_t end = std::min(frames, f + kBaseFrames);
        auto [lo, hi] = std::minmax_element(s.begin() + 2 * f, s.begin() + 2 * end);
        level.emplace_back(*lo, *hi);
    }
    while (!level.empty()) {
        levels.push_back(level);
        if (level.size() == 1) {
            break;
        }
        std::vector<std::pair<float, float>> coarser;
        for (size_t i = 0; i < level.size(); i += 2) {
            const auto& b = level[std::min(i + 1, level.size() - 1)];
            coarser.emplace_back(std::min(level[i].first, b.first),
                                 std::max(level[i].second, b.second));
        }
        level = std::move(coarser);
    }
}

std::pair<float, float> WaveformPeaks::range(const WavClip& clip, size_t begin, size_t end) const {
    const std::vector<float>& s = clip.interleaved_stereo;
    end = std::min(end, s.size() / 2);
    if (begin >= end) {
        return {0.0f, 0.0f};
    }
    if (end - begin < kBaseFrames || levels.empty()) {
        auto [lo, hi] = std::minmax_element(s.begin() + 2 * begin, s.begin() + 2 * end);
        return {*lo, *hi};
    }
    // The coarsest level whose buckets still fit in the range: at most a few buckets to read.
    size_t level = 0;
    while (level + 1 < levels.size() && (kBaseFrames << (level + 1)) <= end - begin) {
        ++level;
    }
    const size_t bucket = kBaseFrames << level;
    const auto& peaks = levels[level];
    std::pair<float, float> out = peaks[begin / bucket];
    for (size_t i = begin / bucket + 1; i <= (end - 1) / bucket && i < peaks.size(); ++i) {
        out.first = std::min(out.first, peaks[i].first);
        out.second = std::max(out.second, peaks[i].second);
    }
    return out;
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

void Timeline::Begin(const char* id, ImVec2 size) {
    ruler_min_ = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, RulerHeight()));
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
    HandleWheel();

    // Ruler ticks at least 80 px apart.
    static const double kSteps[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0,
                                    10.0, 15.0, 30.0, 60.0, 120.0, 300.0, 600.0};
    tick_step_ = kSteps[std::size(kSteps) - 1];
    for (double s : kSteps) {
        if (s / seconds_per_pixel_ >= 80.0) {
            tick_step_ = s;
            break;
        }
    }
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
    ImGui::SetCursorPos(ImVec2(kHeaderWidth + static_cast<float>(length_ / seconds_per_pixel_), row_y_));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::EndChild();
    DrawRuler();
}

void Timeline::DrawTicks(float top, float bottom) const {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const long first = std::max(0L, static_cast<long>(std::floor(SecondsAt(lanes_x_) / tick_step_)));
    for (long i = first;; ++i) {
        const float x = XAt(i * tick_step_);
        if (x > view_max_.x) {
            break;
        }
        draw->AddLine(ImVec2(x, top), ImVec2(x, bottom), kTickLine);
    }
}

void Timeline::DrawRuler() const {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(view_max_.x, ruler_min_.y + RulerHeight());
    draw->AddRectFilled(ruler_min_, max, kRulerBackground);
    draw->PushClipRect(ImVec2(lanes_x_, ruler_min_.y), max, true);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const long first = std::max(0L, static_cast<long>(std::floor(SecondsAt(lanes_x_) / tick_step_)));
    for (long i = first;; ++i) {
        const double seconds = i * tick_step_;
        const float x = XAt(seconds);
        if (x > max.x) {
            break;
        }
        char label[16];
        if (tick_step_ >= 60.0) {
            const int whole = static_cast<int>(std::lround(seconds));
            std::snprintf(label, sizeof(label), "%d:%02d", whole / 60, whole % 60);
        } else {
            std::snprintf(label, sizeof(label), "%g s", seconds);
        }
        draw->AddLine(ImVec2(x, ruler_min_.y + RulerHeight() * 0.5f), ImVec2(x, max.y), kRowLine);
        draw->AddText(ImVec2(x + 3.0f, ruler_min_.y + 2.0f), text, label);
    }
    draw->PopClipRect();
    draw->AddLine(ImVec2(ruler_min_.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), kRowLine);
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
    ImGui::EndGroup();
    ImGui::PopClipRect();
    ImGui::PopID();
}

void Timeline::Clip(const WavClip& clip, const WaveformPeaks& peaks, double start,
                    double sample_rate, bool highlight) {
    const double frames = static_cast<double>(clip.interleaved_stereo.size() / 2);
    const double end = start + frames / sample_rate;
    ExtendTo(end);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ScopedClip scope(draw, lane_min_, lane_max_, view_min_, view_max_);
    const float x0 = XAt(start);
    const float x1 = XAt(end);
    const float top = lane_min_.y;
    const float height = lane_max_.y - lane_min_.y;
    draw->AddRectFilled(ImVec2(x0, top + 2.0f), ImVec2(x1, top + height - 3.0f),
                        highlight ? IM_COL32(70, 110, 170, 255) : IM_COL32(45, 75, 120, 255), 3.0f);
    const float mid = top + height * 0.5f;
    const float half = height * 0.5f - 4.0f;
    const auto frame_at = [&](float x) {
        return static_cast<int64_t>(std::floor((SecondsAt(x) - start) * sample_rate));
    };
    for (float x = std::floor(std::max(x0, view_min_.x)); x < std::min(x1, view_max_.x); x += 1.0f) {
        const int64_t begin = frame_at(x);
        const int64_t stop = frame_at(x + 1.0f);
        if (stop <= 0) {
            continue;
        }
        const auto [lo, hi] = peaks.range(clip, static_cast<size_t>(std::max<int64_t>(begin, 0)),
                                          static_cast<size_t>(std::max(stop, begin + 1)));
        draw->AddLine(ImVec2(x + 0.5f, mid - hi * half), ImVec2(x + 0.5f, mid - lo * half + 1.0f),
                      IM_COL32(150, 200, 255, 255));
    }
}

void Timeline::Notes(const std::vector<TimelineNote>& notes, double playhead) {
    if (notes.empty()) {
        return;
    }
    int low = notes.front().pitch;
    int high = low;
    for (const TimelineNote& n : notes) {
        low = std::min(low, n.pitch);
        high = std::max(high, n.pitch);
        ExtendTo(n.start + n.duration);
    }
    // At least an octave tall, so a few close notes are not stretched over the whole lane.
    const int span = std::max(high - low + 1, 12);
    const int base = low - (span - (high - low + 1)) / 2;
    constexpr float kPad = 4.0f;
    const float row = (lane_max_.y - lane_min_.y - 2.0f * kPad) / static_cast<float>(span);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ScopedClip scope(draw, lane_min_, lane_max_, view_min_, view_max_);
    for (const TimelineNote& n : notes) {
        const float x0 = XAt(n.start);
        const float x1 = std::max(XAt(n.start + n.duration), x0 + 1.0f);
        if (x1 < view_min_.x || x0 > view_max_.x) {
            continue;
        }
        const float y = lane_max_.y - kPad - static_cast<float>(n.pitch - base + 1) * row;
        const bool sounding = playhead >= n.start && playhead < n.start + n.duration;
        draw->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + std::max(row - 1.0f, 2.0f)),
                            sounding ? IM_COL32(150, 225, 240, 255) : IM_COL32(73, 156, 173, 255));
    }
}

void Timeline::Band(const char* label, bool active) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ScopedClip scope(draw, lane_min_, lane_max_, view_min_, view_max_);
    const float x0 = std::max(XAt(0.0), lane_min_.x);
    const ImVec2 min(x0, lane_min_.y + 6.0f);
    const ImVec2 max(lane_max_.x, lane_max_.y - 7.0f);
    draw->AddRectFilled(min, max, active ? IM_COL32(55, 110, 95, 255) : IM_COL32(45, 52, 55, 255));
    if (label && label[0]) {
        const float y = 0.5f * (min.y + max.y - ImGui::GetTextLineHeight());
        draw->AddText(ImVec2(x0 + 6.0f, y),
                      ImGui::GetColorU32(active ? ImGuiCol_Text : ImGuiCol_TextDisabled), label);
    }
}

void Timeline::Playhead(double seconds) {
    ExtendTo(seconds);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ScopedClip scope(draw, lane_min_, lane_max_, view_min_, view_max_);
    const float x = XAt(seconds);
    draw->AddLine(ImVec2(x, lane_min_.y), ImVec2(x, lane_max_.y), kPlayheadColor, 2.0f);
}

void Timeline::Marker(double seconds) {
    ExtendTo(seconds);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ScopedClip scope(draw, lane_min_, lane_max_, view_min_, view_max_);
    const float x = XAt(seconds);
    const ImU32 color = IM_COL32(235, 235, 235, 200);
    draw->AddLine(ImVec2(x, lane_min_.y), ImVec2(x, lane_max_.y), color);
    draw->AddTriangleFilled(ImVec2(x - 5.0f, lane_min_.y), ImVec2(x + 5.0f, lane_min_.y),
                            ImVec2(x, lane_min_.y + 7.0f), color);
}
