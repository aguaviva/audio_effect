#include "timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "imgui.h"
#include "ui/message_box.hpp"

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

namespace {

float seconds_of(int64_t frames) {
    return static_cast<float>(frames) / static_cast<float>(kSampleRate);
}

std::string track_key(int track, const char* field) {
    return "track" + std::to_string(track + 1) + "_" + field;
}

} // namespace

std::string Timeline::save() const {
    std::string out = "{";
    module_json::write_int(out, "cursor", static_cast<int>(cursor_));
    for (int t = 0; t < kTracks; ++t) {
        if (!tracks_[t].path.empty()) {
            module_json::write_string(out, track_key(t, "path").c_str(), tracks_[t].path);
            module_json::write_int(out, track_key(t, "start").c_str(),
                                   static_cast<int>(tracks_[t].start));
        }
    }
    out += '}';
    return out;
}

bool Timeline::load(const std::string& json) {
    int cursor = 0;
    module_json::read(json, "cursor", cursor);
    cursor_ = std::max(cursor, 0);
    for (int t = 0; t < kTracks; ++t) {
        std::string path;
        int start = 0;
        module_json::read(json, track_key(t, "start").c_str(), start);
        tracks_[t].start = std::max(start, 0);
        if (module_json::read(json, track_key(t, "path").c_str(), path)) {
            load_track(t, path);
        }
    }
    publish();
    return true;
}

bool Timeline::load_track(int track, const std::string& path) {
    Track& tr = tracks_[track];
    tr.path = path;
    tr.error.clear();
    tr.clip = WavGenerator::load(path, kSampleRate, tr.error);
    tr.peaks = tr.clip ? std::make_unique<WaveformPeaks>(*tr.clip) : nullptr;
    publish();
    return tr.error.empty();
}

void Timeline::publish() {
    std::array<Placed, kTracks> placed;
    for (int t = 0; t < kTracks; ++t) {
        placed[t] = {tracks_[t].clip, tracks_[t].start};
    }
    std::lock_guard<std::mutex> lock(placed_mutex_);
    std::swap(next_, placed);
    changed_ = true;
} // placed now holds the replaced clips, freed here outside the lock

// Play starts from the cursor; Stop leaves the cursor where it is.
void Timeline::play_button() {
    if (ImGui::Button(playing ? "Stop###play" : "Play###play")) {
        playing = !playing;
        position_ = cursor_;
    }
    ImGui::SameLine();
    ImGui::Text("%.2f s", seconds_of(playing ? position_.load() : cursor_));
}

bool Timeline::render() {
    play_button();
    if (ImGui::Button(window_open_ ? "Hide timeline" : "Show timeline")) {
        window_open_ = !window_open_;
    }
    return false;
}

void Timeline::render_window() {
    if (!window_open_) {
        return;
    }
    char title[64];
    std::snprintf(title, sizeof(title), "Timeline###timeline_%p", static_cast<void*>(this));
    ImGui::SetNextWindowPos(ImVec2(40.0f, 260.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(1000.0f, 0.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::Begin(title, &window_open_, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }
    play_button();
    ImGui::SameLine();
    ImGui::TextDisabled("  Wheel zooms, Shift+wheel scrolls. Click the ruler to place the cursor; "
                        "drag a waveform to move it.");

    constexpr float kHeaderWidth = 150.0f;
    constexpr float kRulerHeight = 22.0f;
    constexpr float kLaneHeight = 64.0f;
    const ImGuiStyle& style = ImGui::GetStyle();

    ImGui::BeginGroup();
    const ImVec2 header_origin = ImGui::GetCursorScreenPos();
    for (int t = 0; t < kTracks; ++t) {
        Track& tr = tracks_[t];
        ImGui::PushID(t);
        const std::string picked = tr.picker.poll();
        if (!picked.empty() && !load_track(t, picked)) {
            show_error("Could not load " + picked + ":\n" + tr.error);
        }
        const ImVec2 top(header_origin.x, header_origin.y + kRulerHeight + t * kLaneHeight + 4.0f);
        ImGui::SetCursorScreenPos(top);
        ImGui::Text("Track %d", t + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Load...")) {
            tr.picker.open(tr.path);
        }
        const char* slash = std::strrchr(tr.path.c_str(), '/');
        const char* leaf = slash ? slash + 1 : tr.path.c_str();
        ImGui::PushTextWrapPos(top.x + kHeaderWidth - ImGui::GetWindowPos().x);
        if (!tr.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tr.error.c_str());
        } else {
            ImGui::TextDisabled("%s", leaf[0] ? leaf : "(empty)");
        }
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(
        ImVec2(header_origin.x, header_origin.y + kRulerHeight + kTracks * kLaneHeight));
    ImGui::Dummy(ImVec2(kHeaderWidth, 0.0f));
    ImGui::EndGroup();
    ImGui::SameLine();

    const float lanes_height = kRulerHeight + kTracks * kLaneHeight + style.ScrollbarSize;
    ImGui::BeginChild("lanes", ImVec2(0.0f, lanes_height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 view_min = ImGui::GetWindowPos();
    const float view_width = ImGui::GetWindowWidth();

    // Zoom about the mouse; scrolling is the child's horizontal scrollbar.
    if (ImGui::IsWindowHovered()) {
        const float scroll = ImGui::GetScrollX();
        if (io.MouseWheel != 0.0f && !io.KeyShift) {
            const float mouse_x = io.MousePos.x - view_min.x;
            const float frame = (scroll + mouse_x) * frames_per_pixel_;
            frames_per_pixel_ = std::clamp(frames_per_pixel_ * std::pow(0.8f, io.MouseWheel),
                                           1.0f, 48000.0f);
            ImGui::SetScrollX(std::max(0.0f, frame / frames_per_pixel_ - mouse_x));
        } else if (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) {
            const float wheel = io.MouseWheelH != 0.0f ? io.MouseWheelH : io.MouseWheel;
            ImGui::SetScrollX(scroll - wheel * 0.1f * view_width);
        }
    }
    const float fpp = frames_per_pixel_;

    int64_t length = 60 * static_cast<int64_t>(kSampleRate);
    for (const Track& tr : tracks_) {
        if (tr.clip) {
            length = std::max<int64_t>(length, tr.start + tr.clip->interleaved_stereo.size() / 2);
        }
    }
    length = std::max(length, cursor_) + 10 * static_cast<int64_t>(kSampleRate);
    const float content_width = static_cast<float>(length) / fpp;

    const ImVec2 origin = ImGui::GetCursorScreenPos(); // frame 0, top of the ruler
    auto x_of = [&](int64_t frame) { return origin.x + static_cast<float>(frame) / fpp; };
    auto frame_at = [&](float x) {
        return std::max<int64_t>(0, static_cast<int64_t>((x - origin.x) * fpp));
    };

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::InvisibleButton("ruler", ImVec2(content_width, kRulerHeight));
    if (ImGui::IsItemActive()) {
        cursor_ = frame_at(io.MousePos.x);
        position_ = cursor_;
    }
    for (int t = 0; t < kTracks; ++t) {
        Track& tr = tracks_[t];
        ImGui::PushID(t);
        ImGui::InvisibleButton("lane", ImVec2(content_width, kLaneHeight));
        if (ImGui::IsItemActivated() && tr.clip) {
            const int64_t grab = frame_at(io.MousePos.x) - tr.start;
            if (grab >= 0 && grab < static_cast<int64_t>(tr.clip->interleaved_stereo.size() / 2)) {
                dragging_ = t;
                grab_frame_ = grab;
            }
        }
        if (dragging_ == t) {
            if (ImGui::IsItemActive()) {
                const int64_t start = std::max<int64_t>(0, frame_at(io.MousePos.x) - grab_frame_);
                if (start != tr.start) {
                    tr.start = start;
                    publish();
                }
            } else {
                dragging_ = -1;
            }
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float view_max_x = view_min.x + view_width;

    // Ruler: ticks at least 80 px apart.
    const float pixels_per_second = kSampleRate / fpp;
    static const float kSteps[] = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 1.0f, 2.0f,
                                   5.0f,  10.0f, 15.0f, 30.0f, 60.0f, 120.0f, 300.0f};
    float step = kSteps[std::size(kSteps) - 1];
    for (float s : kSteps) {
        if (s * pixels_per_second >= 80.0f) {
            step = s;
            break;
        }
    }
    const ImU32 text_color = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 grid_color = ImGui::GetColorU32(ImGuiCol_Border);
    const float lanes_top = origin.y + kRulerHeight;
    const float lanes_bottom = lanes_top + kTracks * kLaneHeight;
    const int first_tick = static_cast<int>((view_min.x - origin.x) / pixels_per_second / step);
    for (int i = std::max(first_tick, 0);; ++i) {
        const float x = origin.x + i * step * pixels_per_second;
        if (x > view_max_x) {
            break;
        }
        char label[16];
        std::snprintf(label, sizeof(label), "%g s", i * step);
        draw->AddLine(ImVec2(x, origin.y + kRulerHeight * 0.5f), ImVec2(x, lanes_bottom),
                      grid_color);
        draw->AddText(ImVec2(x + 3.0f, origin.y + 2.0f), text_color, label);
    }

    // Lanes and waveforms.
    for (int t = 0; t < kTracks; ++t) {
        const Track& tr = tracks_[t];
        const float top = lanes_top + t * kLaneHeight;
        draw->AddLine(ImVec2(view_min.x, top), ImVec2(view_max_x, top), grid_color);
        if (!tr.clip) {
            continue;
        }
        const int64_t frames = static_cast<int64_t>(tr.clip->interleaved_stereo.size() / 2);
        const float x0 = x_of(tr.start);
        const float x1 = x_of(tr.start + frames);
        const ImU32 fill = dragging_ == t ? IM_COL32(70, 110, 170, 255) : IM_COL32(45, 75, 120, 255);
        draw->AddRectFilled(ImVec2(x0, top + 2.0f), ImVec2(x1, top + kLaneHeight - 2.0f), fill, 3.0f);
        const float mid = top + kLaneHeight * 0.5f;
        const float half = kLaneHeight * 0.5f - 4.0f;
        for (float x = std::floor(std::max(x0, view_min.x)); x < std::min(x1, view_max_x); x += 1.0f) {
            const int64_t begin = frame_at(x) - tr.start;
            const int64_t end = frame_at(x + 1.0f) - tr.start;
            if (end <= 0) {
                continue;
            }
            const auto [lo, hi] = tr.peaks->range(*tr.clip, static_cast<size_t>(std::max<int64_t>(begin, 0)),
                                                  static_cast<size_t>(std::max(end, begin + 1)));
            draw->AddLine(ImVec2(x + 0.5f, mid - hi * half), ImVec2(x + 0.5f, mid - lo * half + 1.0f),
                          IM_COL32(150, 200, 255, 255));
        }
    }

    // Cursor (where Play starts) and, while playing, the playhead.
    const float cursor_x = x_of(cursor_);
    draw->AddLine(ImVec2(cursor_x, origin.y), ImVec2(cursor_x, lanes_bottom),
                  IM_COL32(255, 200, 60, 255), 2.0f);
    draw->AddTriangleFilled(ImVec2(cursor_x - 6.0f, origin.y), ImVec2(cursor_x + 6.0f, origin.y),
                            ImVec2(cursor_x, origin.y + 9.0f), IM_COL32(255, 200, 60, 255));
    if (playing) {
        const float play_x = x_of(position_.load());
        draw->AddLine(ImVec2(play_x, origin.y), ImVec2(play_x, lanes_bottom),
                      IM_COL32(255, 255, 255, 200));
    }

    ImGui::EndChild();
    ImGui::End();
}

void Timeline::process(const float* const* /*inputs*/, int /*input_count*/, float* output,
                       size_t sample_count, float /*sample_rate*/) {
    std::unique_lock<std::mutex> lock(placed_mutex_, std::try_to_lock);
    if (lock.owns_lock() && changed_) {
        std::swap(placed_, next_);
        changed_ = false;
    }
    std::fill(output, output + sample_count, 0.0f);
    if (!playing) {
        return;
    }
    int64_t start = position_;
    const int64_t frames = static_cast<int64_t>(sample_count / 2);
    for (const Placed& p : placed_) {
        if (!p.clip) {
            continue;
        }
        const float* clip = p.clip->interleaved_stereo.data();
        const int64_t clip_frames = static_cast<int64_t>(p.clip->interleaved_stereo.size() / 2);
        const int64_t begin = std::max(start, p.start);
        const int64_t end = std::min(start + frames, p.start + clip_frames);
        for (int64_t f = begin; f < end; ++f) {
            output[2 * (f - start)] += clip[2 * (f - p.start)];
            output[2 * (f - start) + 1] += clip[2 * (f - p.start) + 1];
        }
    }
    position_.compare_exchange_strong(start, start + frames); // a seek made during this block wins
}
