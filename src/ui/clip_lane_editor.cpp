#include "clip_lane_editor.hpp"

#include "imgui.h"
#include "model/clip_arrangement.hpp"
#include "model/clip_clipboard.hpp"
#include "model/clip_snap.hpp"
#include "model/tempo_grid.hpp"
#include "model/wav_library.hpp"
#include "ui/message_box.hpp"
#include "ui/timeline.hpp"
#include "ui/timeline_wav_lane.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr double kEdgePixels = 6.0;
constexpr double kSnapPixels = 8.0;

const char* leaf_of(const std::string& path) {
    const char* slash = std::strrchr(path.c_str(), '/');
    return slash ? slash + 1 : path.c_str();
}

} // namespace

int64_t ClipLaneEditor::snap_edge(const ClipArrangement& arrangement, int64_t frame,
                                  double frames_per_pixel, int channel, int index) const {
    const ClipSnap snap{arrangement, song_grid(), sample_rate_,
                        static_cast<int64_t>(kSnapPixels * frames_per_pixel)};
    return snap.edge(frame, channel, index);
}

ClipLaneEditor::Hit ClipLaneEditor::hit_test(const ClipArrangement& arrangement, int channel,
                                             int64_t frame, double frames_per_pixel) const {
    const auto& clips = arrangement.clips(channel);
    // The last drawn is on top.
    for (int i = static_cast<int>(clips.size()) - 1; i >= 0; --i) {
        const ArrangedClip& c = clips[i];
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

void ClipLaneEditor::select(int channel, int index) {
    selected_channel_ = index >= 0 ? channel : -1;
    selected_ = index;
}

bool ClipLaneEditor::has_selection(const ClipArrangement& arrangement) const {
    return selected_channel_ >= 0 && selected_channel_ < arrangement.channel_count() &&
           selected_ >= 0 &&
           selected_ < static_cast<int>(arrangement.clips(selected_channel_).size());
}

void ClipLaneEditor::open_picker(const ClipArrangement& arrangement, int channel, int64_t frame) {
    load_channel_ = channel;
    load_frame_ = frame;
    std::string current;
    for (const ArrangedClip& c : arrangement.clips(channel)) {
        current = c.path;
    }
    picker_.open(current);
}

int ClipLaneEditor::paste(ClipArrangement& arrangement, int channel, int64_t frame,
                          double frames_per_pixel) {
    const ArrangedClip* copied = clip_clipboard::get();
    if (!copied) {
        return -1;
    }
    ArrangedClip clip = *copied;
    const ClipSnap snap{arrangement, song_grid(), sample_rate_,
                        static_cast<int64_t>(kSnapPixels * frames_per_pixel)};
    clip.start = snap.start(frame, clip.frames());
    return arrangement.add(channel, std::move(clip));
}

void ClipLaneEditor::poll(ClipArrangement& arrangement, WavLibrary& library) {
    const std::string picked = picker_.poll();
    if (picked.empty() || load_channel_ >= arrangement.channel_count()) {
        return;
    }
    const WavLibrary::Entry& file = library.get(picked);
    if (!file.wav) {
        arrangement.set_error(load_channel_, leaf_of(picked) + std::string(": ") + file.error);
        show_error("Could not load " + picked + ":\n" + file.error);
        return;
    }
    ArrangedClip clip;
    clip.path = picked;
    clip.start = load_frame_;
    clip.wav = file.wav;
    clip.peaks = file.peaks;
    arrangement.set_error(load_channel_, {});
    select(load_channel_, arrangement.add(load_channel_, std::move(clip)));
}

void ClipLaneEditor::draw(Timeline& timeline, ClipArrangement& arrangement,
                          const char* lane_name) {
    const double frames_per_pixel = timeline.SecondsPerPixel() * sample_rate_;
    const int64_t mouse = static_cast<int64_t>(timeline.MouseSeconds() * sample_rate_);
    if (drag_channel_ >= arrangement.channel_count()) {
        dragging_ = Grab::None;
    }
    int hovered_channel = -1;
    for (int ch = 0; ch < arrangement.channel_count(); ++ch) {
        const auto& clips = arrangement.clips(ch);
        char id[16];
        std::snprintf(id, sizeof(id), "lane%d", ch);
        timeline.BeginTrack(id);
        ImGui::Text("%s %d", lane_name, ch + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Load...")) {
            int64_t end = 0;
            for (const ArrangedClip& c : clips) {
                end = std::max(end, c.end());
            }
            open_picker(arrangement, ch, end);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Adds a WAV after the last clip; right-click the lane to add one "
                              "anywhere");
        }
        const std::string& error = arrangement.error(ch);
        if (!error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", error.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", error.c_str());
            }
        } else if (clips.size() == 1) {
            ImGui::TextDisabled("%s", leaf_of(clips[0].path));
        } else if (clips.empty()) {
            ImGui::TextDisabled("(empty)");
        } else {
            ImGui::TextDisabled("%zu clips", clips.size());
        }

        const bool hovered = timeline.LaneHovered();
        const Hit hit = hovered ? hit_test(arrangement, ch, mouse, frames_per_pixel) : Hit{};
        if (hovered) {
            hovered_channel = ch;
        }
        if (timeline.LaneActivated()) {
            select(ch, hit.index);
            if (hit.index >= 0) {
                const ArrangedClip& c = clips[hit.index];
                dragging_ = hit.part;
                drag_channel_ = ch;
                drag_index_ = hit.index;
                grab_frame_ = mouse - (hit.part == Grab::End ? c.end() : c.start);
            }
        }
        const bool dragging_here = dragging_ != Grab::None && drag_channel_ == ch &&
                                   drag_index_ < static_cast<int>(clips.size());
        if (dragging_here && !timeline.LaneActive()) {
            dragging_ = Grab::None;
        } else if (dragging_here) {
            timeline.ClaimLane();
            const int64_t at = mouse - grab_frame_;
            const ClipSnap snap{arrangement, song_grid(), sample_rate_,
                                static_cast<int64_t>(kSnapPixels * frames_per_pixel)};
            switch (dragging_) {
            case Grab::Body:
                arrangement.move(ch, drag_index_,
                                 snap.start(std::max<int64_t>(at, 0), clips[drag_index_].frames(),
                                            ch, drag_index_));
                break;
            case Grab::Start:
                arrangement.trim_start(ch, drag_index_, snap.edge(at, ch, drag_index_));
                break;
            case Grab::End:
                arrangement.trim_end(ch, drag_index_, snap.edge(at, ch, drag_index_));
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
            menu_channel_ = ch;
            menu_frame_ = mouse;
            menu_clip_ = hit.index;
            menu_frames_per_pixel_ = frames_per_pixel;
            if (hit.index >= 0) {
                select(ch, hit.index);
            }
        }

        for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
            const ArrangedClip& c = clips[i];
            if (c.frames() == 0) {
                continue;
            }
            ClipStyle style;
            style.offset = static_cast<size_t>(c.offset);
            style.length = static_cast<size_t>(c.frames());
            style.label = leaf_of(c.path);
            style.highlight = dragging_ != Grab::None && drag_channel_ == ch && drag_index_ == i;
            style.selected = selected_channel_ == ch && selected_ == i;
            draw_wav_clip(timeline, *c.wav, *c.peaks, static_cast<double>(c.start) / sample_rate_,
                          sample_rate_, style);
        }
        timeline.EndTrack();
    }
    handle_keys(timeline, arrangement, hovered_channel);
    context_menu(arrangement);
}

void ClipLaneEditor::handle_keys(const Timeline& timeline, ClipArrangement& arrangement,
                                 int hovered) {
    if (hovered < 0 || ImGui::GetIO().WantTextInput) {
        return;
    }
    const bool selection = has_selection(arrangement);
    if (selection && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        arrangement.remove(selected_channel_, selected_);
        select(-1, -1);
        dragging_ = Grab::None;
        return;
    }
    if (selection && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
        clip_clipboard::copy(arrangement.clips(selected_channel_)[selected_]);
    }
    if (selection && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) {
        clip_clipboard::copy(arrangement.clips(selected_channel_)[selected_]);
        arrangement.remove(selected_channel_, selected_);
        select(-1, -1);
        dragging_ = Grab::None;
        return;
    }
    if (selection && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
        select(selected_channel_, arrangement.duplicate(selected_channel_, selected_));
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
        const double frames_per_pixel = timeline.SecondsPerPixel() * sample_rate_;
        const int64_t mouse = static_cast<int64_t>(timeline.MouseSeconds() * sample_rate_);
        const int index = paste(arrangement, hovered, mouse, frames_per_pixel);
        if (index >= 0) {
            select(hovered, index);
        }
    }
}

void ClipLaneEditor::context_menu(ClipArrangement& arrangement) {
    if (open_menu_) {
        ImGui::OpenPopup("##clip_menu");
        open_menu_ = false;
    }
    if (!ImGui::BeginPopup("##clip_menu")) {
        return;
    }
    if (menu_channel_ >= arrangement.channel_count()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const auto& clips = arrangement.clips(menu_channel_);
    const bool on_clip = menu_clip_ >= 0 && menu_clip_ < static_cast<int>(clips.size());
    const int64_t at = snap_edge(arrangement, menu_frame_, menu_frames_per_pixel_);
    if (on_clip) {
        ImGui::TextDisabled("%s", leaf_of(clips[menu_clip_].path));
    } else {
        ImGui::TextDisabled("Channel %d at %.2f s", menu_channel_ + 1,
                            static_cast<double>(at) / sample_rate_);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Load WAV...")) {
        open_picker(arrangement, menu_channel_, at);
    }
    if (ImGui::MenuItem("Paste", "Ctrl+V", false, clip_clipboard::get() != nullptr)) {
        select(menu_channel_, paste(arrangement, menu_channel_, menu_frame_, menu_frames_per_pixel_));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, on_clip)) {
        clip_clipboard::copy(clips[menu_clip_]);
    }
    if (ImGui::MenuItem("Cut", "Ctrl+X", false, on_clip)) {
        clip_clipboard::copy(clips[menu_clip_]);
        arrangement.remove(menu_channel_, menu_clip_);
        select(-1, -1);
        dragging_ = Grab::None;
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, on_clip)) {
        select(menu_channel_, arrangement.duplicate(menu_channel_, menu_clip_));
    }
    if (ImGui::MenuItem("Delete", "Del", false, on_clip)) {
        arrangement.remove(menu_channel_, menu_clip_);
        select(-1, -1);
        dragging_ = Grab::None;
    }
    ImGui::EndPopup();
}
