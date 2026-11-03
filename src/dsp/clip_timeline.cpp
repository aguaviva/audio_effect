#include "clip_timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <optional>

#include "dsp/transport.hpp"
#include "imgui.h"
#include "ui/message_box.hpp"

namespace {

float seconds_of(int64_t frames) {
    return static_cast<float>(frames) / static_cast<float>(kSampleRate);
}

std::string track_key(int track, const char* field) {
    return "track" + std::to_string(track + 1) + "_" + field;
}

const char* leaf_of(const std::string& path) {
    const char* slash = std::strrchr(path.c_str(), '/');
    return slash ? slash + 1 : path.c_str();
}

int64_t file_frames(const WavClip& wav) {
    return static_cast<int64_t>(wav.interleaved_stereo.size() / 2);
}

std::optional<ClipTimeline::Clip>& clipboard() {
    static std::optional<ClipTimeline::Clip> copied;
    return copied;
}

} // namespace

int64_t ClipTimeline::Clip::frames() const {
    if (!wav) {
        return 0;
    }
    const int64_t available = std::max<int64_t>(0, file_frames(*wav) - offset);
    return length < 0 ? available : std::min(length, available);
}

void ClipTimeline::get_outputs(int* count, const char* const** str) const {
    static const char* names[] = {"track 1", "track 2", "track 3", "track 4", "track 5", "track 6"};
    static_assert(std::size(names) == kTracks, "one name per track");
    *count = kTracks;
    *str = names;
}

std::string ClipTimeline::save() const {
    std::string out = "{";
    for (int t = 0; t < kTracks; ++t) {
        if (tracks_[t].clips.empty()) {
            continue;
        }
        module_json::append_comma(out);
        out += '"' + track_key(t, "clips") + "\":[";
        for (const Clip& c : tracks_[t].clips) {
            module_json::append_comma(out);
            std::string clip = "{";
            module_json::write_string(clip, "path", c.path);
            module_json::write_int(clip, "start", static_cast<int>(c.start));
            module_json::write_int(clip, "offset", static_cast<int>(c.offset));
            if (c.length >= 0) {
                module_json::write_int(clip, "length", static_cast<int>(c.length));
            }
            out += clip + '}';
        }
        out += ']';
    }
    out += '}';
    return out;
}

bool ClipTimeline::load(const std::string& json) {
    for (Track& tr : tracks_) {
        tr = Track{};
    }
    select(-1, -1);
    dragging_ = Grab::None;
    // A missing file keeps its clip, so saving the patch again does not lose it.
    const auto place = [this](int track, Clip clip) {
        const Source& s = source(clip.path);
        clip.wav = s.wav;
        clip.peaks = s.peaks;
        if (!s.wav) {
            tracks_[track].error = leaf_of(clip.path) + std::string(": ") + s.error;
        }
        tracks_[track].clips.push_back(std::move(clip));
    };

    std::array<bool, kTracks> listed{};
    module_json::for_each_member(json, [&](const std::string& key, const std::string& text,
                                           size_t& i) {
        for (int t = 0; t < kTracks; ++t) {
            if (key != track_key(t, "clips")) {
                continue;
            }
            listed[t] = true;
            return module_json::for_each_element(text, i, [&](const std::string& element) {
                Clip clip;
                int start = 0;
                int offset = 0;
                int length = -1;
                module_json::read(element, "path", clip.path);
                module_json::read(element, "start", start);
                module_json::read(element, "offset", offset);
                module_json::read(element, "length", length);
                clip.start = std::max(start, 0);
                clip.offset = std::max(offset, 0);
                clip.length = length < 0 ? -1 : std::max(length, 1);
                if (!clip.path.empty()) {
                    place(t, std::move(clip));
                }
                return true;
            });
        }
        return module_json::skip_value(text, i);
    });
    // Patches from before tracks held several clips: one whole file per track.
    for (int t = 0; t < kTracks; ++t) {
        Clip clip;
        int start = 0;
        if (!listed[t] && module_json::read(json, track_key(t, "path").c_str(), clip.path)) {
            module_json::read(json, track_key(t, "start").c_str(), start);
            clip.start = std::max(start, 0);
            place(t, std::move(clip));
        }
    }
    publish();
    return true;
}

const ClipTimeline::Source& ClipTimeline::source(const std::string& path) {
    Source& s = sources_[path];
    if (!s.wav) {
        s.error.clear();
        s.wav = WavGenerator::load(path, kSampleRate, s.error);
        s.peaks = s.wav ? std::make_shared<const WaveformPeaks>(*s.wav) : nullptr;
        if (!s.wav && s.error.empty()) {
            s.error = "could not be read";
        }
    }
    return s;
}

int ClipTimeline::add_clip(int track, const std::string& path, int64_t start, std::string& error) {
    const Source& s = source(path);
    if (!s.wav) {
        error = s.error;
        tracks_[track].error = leaf_of(path) + std::string(": ") + s.error;
        return -1;
    }
    Clip clip;
    clip.path = path;
    clip.start = std::max<int64_t>(start, 0);
    clip.wav = s.wav;
    clip.peaks = s.peaks;
    tracks_[track].clips.push_back(std::move(clip));
    tracks_[track].error.clear();
    publish();
    return static_cast<int>(tracks_[track].clips.size()) - 1;
}

void ClipTimeline::move_clip(int track, int index, int64_t start) {
    Clip& c = tracks_[track].clips[index];
    start = std::max<int64_t>(start, 0);
    if (start != c.start) {
        c.start = start;
        publish();
    }
}

void ClipTimeline::trim_start(int track, int index, int64_t start) {
    Clip& c = tracks_[track].clips[index];
    if (c.frames() == 0) {
        return;
    }
    const int64_t end = c.end();
    start = std::clamp(start, std::max<int64_t>(0, c.start - c.offset), end - 1);
    if (start != c.start) {
        c.offset += start - c.start;
        c.length = end - start;
        c.start = start;
        publish();
    }
}

void ClipTimeline::trim_end(int track, int index, int64_t end) {
    Clip& c = tracks_[track].clips[index];
    if (c.frames() == 0) {
        return;
    }
    end = std::clamp(end, c.start + 1, c.start + file_frames(*c.wav) - c.offset);
    if (end != c.end()) {
        c.length = end - c.start;
        publish();
    }
}

void ClipTimeline::remove_clip(int track, int index) {
    std::vector<Clip>& clips = tracks_[track].clips;
    clips.erase(clips.begin() + index);
    if (selected_track_ == track) {
        selected_ = selected_ == index ? -1 : selected_ - (selected_ > index ? 1 : 0);
    }
    if (drag_track_ == track) {
        dragging_ = Grab::None;
    }
    publish();
}

void ClipTimeline::copy_clip(int track, int index) const {
    clipboard() = tracks_[track].clips[index];
}

bool ClipTimeline::can_paste() {
    return clipboard().has_value();
}

int ClipTimeline::paste(int track, int64_t start) {
    if (!clipboard()) {
        return -1;
    }
    Clip clip = *clipboard();
    clip.start = std::max<int64_t>(start, 0);
    tracks_[track].clips.push_back(std::move(clip));
    publish();
    return static_cast<int>(tracks_[track].clips.size()) - 1;
}

void ClipTimeline::publish() {
    Placement placed;
    for (int t = 0; t < kTracks; ++t) {
        for (const Clip& c : tracks_[t].clips) {
            if (c.frames() > 0) {
                placed[t].push_back({c.wav, c.start, c.offset, c.frames()});
            }
        }
    }
    std::lock_guard<std::mutex> lock(placed_mutex_);
    std::swap(next_, placed);
    changed_ = true;
} // placed now holds the replaced clips, freed here outside the lock

ClipTimeline::Hit ClipTimeline::hit_test(int track, int64_t frame, double frames_per_pixel) const {
    constexpr double kEdgePixels = 6.0;
    const std::vector<Clip>& clips = tracks_[track].clips;
    // The last drawn is on top.
    for (int i = static_cast<int>(clips.size()) - 1; i >= 0; --i) {
        const Clip& c = clips[i];
        if (c.frames() == 0 || frame < c.start || frame >= c.end()) {
            continue;
        }
        const int64_t edge = std::min(static_cast<int64_t>(kEdgePixels * frames_per_pixel),
                                      c.frames() / 3);
        if (frame - c.start < edge) {
            return {i, Grab::Start};
        }
        if (c.end() - frame <= edge) {
            return {i, Grab::End};
        }
        return {i, Grab::Body};
    }
    return {};
}

int64_t ClipTimeline::snap_edge(int64_t frame, int track, int index, double frames_per_pixel) const {
    constexpr double kSnapPixels = 8.0;
    int64_t best = static_cast<int64_t>(kSnapPixels * frames_per_pixel);
    int64_t snapped = frame;
    for (int t = 0; t < kTracks; ++t) {
        const std::vector<Clip>& clips = tracks_[t].clips;
        for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
            if ((t == track && i == index) || clips[i].frames() == 0) {
                continue;
            }
            for (int64_t edge : {clips[i].start, clips[i].end()}) {
                if (std::abs(edge - frame) < best) {
                    best = std::abs(edge - frame);
                    snapped = edge;
                }
            }
        }
    }
    return snapped;
}

int64_t ClipTimeline::snap_start(int track, int index, int64_t start, double frames_per_pixel) const {
    const int64_t length = tracks_[track].clips[index].frames();
    const int64_t by_start = snap_edge(start, track, index, frames_per_pixel);
    const int64_t by_end = snap_edge(start + length, track, index, frames_per_pixel) - length;
    const int64_t snapped =
        std::abs(by_end - start) < std::abs(by_start - start) ? by_end : by_start;
    return snapped >= 0 ? snapped : by_start;
}

void ClipTimeline::select(int track, int index) {
    selected_track_ = track;
    selected_ = index;
}

void ClipTimeline::open_picker(int track, int64_t frame) {
    load_track_ = track;
    load_frame_ = frame;
    std::string current;
    for (const Clip& c : tracks_[track].clips) {
        current = c.path;
    }
    picker_.open(current);
}

bool ClipTimeline::render() {
    transport_controls();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextDisabled("Place, trim and copy the clips in View > Timeline");
    ImGui::PopTextWrapPos();
    return false;
}

void ClipTimeline::render_window() {
    // The picker's dialog stays up while the timeline is hidden.
    const std::string picked = picker_.poll();
    if (!picked.empty()) {
        std::string error;
        const int index = add_clip(load_track_, picked, load_frame_, error);
        if (index < 0) {
            show_error("Could not load " + picked + ":\n" + error);
        } else {
            select(load_track_, index);
        }
    }
}

bool ClipTimeline::render_timeline(Timeline& timeline, const char* title) {
    const double frames_per_pixel = timeline.SecondsPerPixel() * kSampleRate;
    const int64_t mouse = static_cast<int64_t>(timeline.MouseSeconds() * kSampleRate);
    timeline.BeginTrack("title", Timeline::TitleHeight());
    ImGui::TextUnformatted(title);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Drag a waveform to move it (it snaps to the other clips) or its edges "
                          "to trim it.\nRight-click a track to load a WAV there or paste.\n"
                          "Ctrl+C / X / V / D copy, cut, paste at the mouse and duplicate; "
                          "Delete removes.");
    }
    timeline.EndTrack();

    int hovered_track = -1;
    for (int t = 0; t < kTracks; ++t) {
        Track& tr = tracks_[t];
        char id[16];
        std::snprintf(id, sizeof(id), "track%d", t);
        timeline.BeginTrack(id);
        ImGui::Text("Track %d", t + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Load...")) {
            int64_t end = 0;
            for (const Clip& c : tr.clips) {
                end = std::max(end, c.end());
            }
            open_picker(t, end);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Adds a WAV after the track's last clip");
        }
        if (!tr.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tr.error.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", tr.error.c_str());
            }
        } else if (tr.clips.size() == 1) {
            ImGui::TextDisabled("%s", leaf_of(tr.clips[0].path));
        } else if (tr.clips.empty()) {
            ImGui::TextDisabled("(empty)");
        } else {
            ImGui::TextDisabled("%zu clips", tr.clips.size());
        }

        const bool hovered = timeline.LaneHovered();
        const Hit hit = hovered ? hit_test(t, mouse, frames_per_pixel) : Hit{};
        if (hovered) {
            hovered_track = t;
        }
        if (timeline.LaneActivated()) {
            select(hit.index >= 0 ? t : -1, hit.index);
            if (hit.index >= 0) {
                const Clip& c = tr.clips[hit.index];
                dragging_ = hit.part;
                drag_track_ = t;
                drag_index_ = hit.index;
                grab_frame_ = mouse - (hit.part == Grab::End ? c.end() : c.start);
            }
        }
        const bool dragging_here = dragging_ != Grab::None && drag_track_ == t;
        if (dragging_here && !timeline.LaneActive()) {
            dragging_ = Grab::None;
        } else if (dragging_here) {
            timeline.ClaimLane();
            const int64_t at = mouse - grab_frame_;
            switch (dragging_) {
            case Grab::Body:
                move_clip(t, drag_index_,
                          snap_start(t, drag_index_, std::max<int64_t>(at, 0), frames_per_pixel));
                break;
            case Grab::Start:
                trim_start(t, drag_index_, snap_edge(at, t, drag_index_, frames_per_pixel));
                break;
            case Grab::End:
                trim_end(t, drag_index_, snap_edge(at, t, drag_index_, frames_per_pixel));
                break;
            case Grab::None:
                break;
            }
        }
        const Grab edge = dragging_here ? dragging_ : hit.part;
        if ((hovered || dragging_here) && (edge == Grab::Start || edge == Grab::End)) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            open_menu_ = true;
            menu_track_ = t;
            menu_frame_ = mouse;
            menu_clip_ = hit.index;
            if (hit.index >= 0) {
                select(t, hit.index);
            }
        }

        for (int i = 0; i < static_cast<int>(tr.clips.size()); ++i) {
            const Clip& c = tr.clips[i];
            if (c.frames() == 0) {
                continue;
            }
            ClipStyle style;
            style.offset = static_cast<size_t>(c.offset);
            style.length = static_cast<size_t>(c.frames());
            style.label = leaf_of(c.path);
            style.highlight = dragging_ != Grab::None && drag_track_ == t && drag_index_ == i;
            style.selected = selected_track_ == t && selected_ == i;
            timeline.Clip(*c.wav, *c.peaks, seconds_of(c.start), kSampleRate, style);
        }
        timeline.EndTrack();
    }
    handle_keys(timeline, hovered_track);
    context_menu();
    return true;
}

void ClipTimeline::handle_keys(Timeline& timeline, int hovered_track) {
    if (hovered_track < 0 || ImGui::GetIO().WantTextInput) {
        return;
    }
    const bool has_selection = selected_track_ >= 0 && selected_ >= 0 &&
                               selected_ < static_cast<int>(tracks_[selected_track_].clips.size());
    if (has_selection && (ImGui::IsKeyPressed(ImGuiKey_Delete) ||
                          ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        remove_clip(selected_track_, selected_);
        return;
    }
    if (has_selection && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
        copy_clip(selected_track_, selected_);
    }
    if (has_selection && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) {
        copy_clip(selected_track_, selected_);
        remove_clip(selected_track_, selected_);
        return;
    }
    if (has_selection && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
        Clip copy = tracks_[selected_track_].clips[selected_];
        copy.start = copy.end();
        tracks_[selected_track_].clips.push_back(std::move(copy));
        select(selected_track_, static_cast<int>(tracks_[selected_track_].clips.size()) - 1);
        publish();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
        const double frames_per_pixel = timeline.SecondsPerPixel() * kSampleRate;
        const int64_t mouse = static_cast<int64_t>(timeline.MouseSeconds() * kSampleRate);
        const int index = paste(hovered_track, mouse);
        if (index >= 0) {
            move_clip(hovered_track, index,
                      snap_start(hovered_track, index, mouse, frames_per_pixel));
            select(hovered_track, index);
        }
    }
}

void ClipTimeline::context_menu() {
    if (open_menu_) {
        ImGui::OpenPopup("##clip_menu");
        open_menu_ = false;
    }
    if (!ImGui::BeginPopup("##clip_menu")) {
        return;
    }
    const std::vector<Clip>& clips = tracks_[menu_track_].clips;
    const bool on_clip = menu_clip_ >= 0 && menu_clip_ < static_cast<int>(clips.size());
    if (on_clip) {
        ImGui::TextDisabled("%s", leaf_of(clips[menu_clip_].path));
    } else {
        ImGui::TextDisabled("Track %d at %.2f s", menu_track_ + 1, seconds_of(menu_frame_));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Load WAV...")) {
        open_picker(menu_track_, menu_frame_);
    }
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, can_paste())) {
        select(menu_track_, paste(menu_track_, menu_frame_));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, on_clip)) {
        copy_clip(menu_track_, menu_clip_);
    }
    if (ImGui::MenuItem("Cut", "Ctrl+X", false, on_clip)) {
        copy_clip(menu_track_, menu_clip_);
        remove_clip(menu_track_, menu_clip_);
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, on_clip)) {
        Clip copy = clips[menu_clip_];
        copy.start = copy.end();
        tracks_[menu_track_].clips.push_back(std::move(copy));
        select(menu_track_, static_cast<int>(tracks_[menu_track_].clips.size()) - 1);
        publish();
    }
    if (ImGui::MenuItem("Delete", "Del", false, on_clip)) {
        remove_clip(menu_track_, menu_clip_);
    }
    ImGui::EndPopup();
}

void ClipTimeline::process(const float* const* /*inputs*/, int /*input_count*/,
                           size_t sample_count, float /*sample_rate*/) {
    std::unique_lock<std::mutex> lock(placed_mutex_, std::try_to_lock);
    if (lock.owns_lock() && changed_) {
        std::swap(placed_, next_);
        changed_ = false;
    }
    for (int t = 0; t < kTracks; ++t) {
        if (output_buffer(t)) {
            std::fill(output_buffer(t), output_buffer(t) + sample_count, 0.0f);
        }
    }
    if (!transport().block_playing()) {
        return;
    }
    const int64_t start = transport().block_frame();
    const int64_t frames = static_cast<int64_t>(sample_count / 2);
    for (int t = 0; t < kTracks; ++t) {
        float* out = output_buffer(t);
        if (!out) {
            continue;
        }
        for (const Placed& p : placed_[t]) {
            const int64_t begin = std::max(start, p.start);
            const int64_t end = std::min(start + frames, p.start + p.frames);
            if (begin >= end) {
                continue;
            }
            const float* from = p.wav->interleaved_stereo.data() + 2 * (p.offset + begin - p.start);
            float* to = out + 2 * (begin - start);
            for (int64_t i = 0; i < 2 * (end - begin); ++i) {
                to[i] += from[i];
            }
        }
    }
}
