#include "clip_timeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

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

} // namespace

void ClipTimeline::get_outputs(int* count, const char* const** str) const {
    static const char* names[] = {"track 1", "track 2", "track 3", "track 4", "track 5", "track 6"};
    static_assert(std::size(names) == kTracks, "one name per track");
    *count = kTracks;
    *str = names;
}

std::string ClipTimeline::save() const {
    std::string out = "{";
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

bool ClipTimeline::load(const std::string& json) {
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

bool ClipTimeline::load_track(int track, const std::string& path) {
    Track& tr = tracks_[track];
    tr.path = path;
    tr.error.clear();
    tr.clip = WavGenerator::load(path, kSampleRate, tr.error);
    tr.peaks = tr.clip ? std::make_unique<WaveformPeaks>(*tr.clip) : nullptr;
    publish();
    return tr.error.empty();
}

void ClipTimeline::publish() {
    std::array<Placed, kTracks> placed;
    for (int t = 0; t < kTracks; ++t) {
        placed[t] = {tracks_[t].clip, tracks_[t].start};
    }
    std::lock_guard<std::mutex> lock(placed_mutex_);
    std::swap(next_, placed);
    changed_ = true;
} // placed now holds the replaced clips, freed here outside the lock

int64_t ClipTimeline::snap(int track, int64_t start, double frames_per_pixel) const {
    constexpr double kSnapPixels = 8.0;
    const int64_t length = static_cast<int64_t>(tracks_[track].clip->interleaved_stereo.size() / 2);
    int64_t best = static_cast<int64_t>(kSnapPixels * frames_per_pixel);
    int64_t snapped = start;
    for (int t = 0; t < kTracks; ++t) {
        const Track& other = tracks_[t];
        if (t == track || !other.clip) {
            continue;
        }
        const int64_t other_end = other.start + static_cast<int64_t>(other.clip->interleaved_stereo.size() / 2);
        for (int64_t edge : {other.start, other_end}) {
            for (int64_t candidate : {edge, edge - length}) {
                if (candidate >= 0 && std::abs(candidate - start) < best) {
                    best = std::abs(candidate - start);
                    snapped = candidate;
                }
            }
        }
    }
    return snapped;
}

bool ClipTimeline::render() {
    transport_controls();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextDisabled("Place the clips in View > Timeline");
    ImGui::PopTextWrapPos();
    return false;
}

void ClipTimeline::render_window() {
    // The pickers' dialogs stay up while the timeline is hidden.
    for (int t = 0; t < kTracks; ++t) {
        const std::string picked = tracks_[t].picker.poll();
        if (!picked.empty() && !load_track(t, picked)) {
            show_error("Could not load " + picked + ":\n" + tracks_[t].error);
        }
    }
}

bool ClipTimeline::render_timeline(Timeline& timeline, const char* title) {
    const int64_t mouse = static_cast<int64_t>(timeline.MouseSeconds() * kSampleRate);
    timeline.BeginTrack("title", Timeline::TitleHeight());
    ImGui::TextUnformatted(title);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Drag a waveform to move it; it snaps to the other clips");
    }
    timeline.EndTrack();
    for (int t = 0; t < kTracks; ++t) {
        Track& tr = tracks_[t];
        char id[16];
        std::snprintf(id, sizeof(id), "track%d", t);
        timeline.BeginTrack(id);
        ImGui::Text("Track %d", t + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton("Load...")) {
            tr.picker.open(tr.path);
        }
        const char* slash = std::strrchr(tr.path.c_str(), '/');
        const char* leaf = slash ? slash + 1 : tr.path.c_str();
        if (!tr.error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", tr.error.c_str());
        } else {
            ImGui::TextDisabled("%s", leaf[0] ? leaf : "(empty)");
        }
        if (ImGui::IsItemHovered() && !tr.path.empty()) {
            ImGui::SetTooltip("%s", tr.path.c_str());
        }

        if (timeline.LaneActivated()) {
            const int64_t grab = mouse - tr.start;
            if (tr.clip && grab >= 0 && grab < static_cast<int64_t>(tr.clip->interleaved_stereo.size() / 2)) {
                dragging_ = t;
                grab_frame_ = grab;
            }
        }
        if (dragging_ == t && !timeline.LaneActive()) {
            dragging_ = -1;
        }
        if (dragging_ == t) {
            timeline.ClaimLane();
            const double frames_per_pixel = timeline.SecondsPerPixel() * kSampleRate;
            const int64_t start = snap(t, std::max<int64_t>(0, mouse - grab_frame_), frames_per_pixel);
            if (start != tr.start) {
                tr.start = start;
                publish();
            }
        }
        if (tr.clip) {
            timeline.Clip(*tr.clip, *tr.peaks, seconds_of(tr.start), kSampleRate, dragging_ == t);
        }
        timeline.EndTrack();
    }
    return true;
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
        const Placed& p = placed_[t];
        float* out = output_buffer(t);
        if (!p.clip || !out) {
            continue;
        }
        const float* clip = p.clip->interleaved_stereo.data();
        const int64_t clip_frames = static_cast<int64_t>(p.clip->interleaved_stereo.size() / 2);
        const int64_t begin = std::max(start, p.start);
        const int64_t end = std::min(start + frames, p.start + clip_frames);
        if (begin < end) {
            std::copy(clip + 2 * (begin - p.start), clip + 2 * (end - p.start), out + 2 * (begin - start));
        }
    }
}
