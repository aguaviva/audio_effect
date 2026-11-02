#pragma once

#include "dsp/wav_generator.hpp"
#include "imgui.h"

#include <cstddef>
#include <utility>
#include <vector>

// Min/max envelope of a clip (both channels together) at 64, 128, 256... frames per bucket,
// so drawing a waveform reads only a few values per pixel at any zoom.
struct WaveformPeaks {
    static constexpr size_t kBaseFrames = 64;
    std::vector<std::vector<std::pair<float, float>>> levels;

    explicit WaveformPeaks(const WavClip& clip);
    // Min and max over frames [begin, end), widened to whole buckets.
    std::pair<float, float> range(const WavClip& clip, size_t begin, size_t end) const;
};

// Play/Pause, Stop (back to the start) and the time, for the shared transport.
void transport_controls(bool small = false);

// A note on a timeline track, in seconds.
struct TimelineNote {
    double start = 0.0;
    double duration = 0.0;
    int pitch = 60;
};

// Tracks against one time axis: a ruler in seconds on top, a header column on the left, a lane
// per track and one cursor across them all. The tracks are described every frame, between
// Begin() and End():
//
//     timeline.Begin("##tracks", cursor);
//     timeline.BeginTrack("lead");
//     ImGui::TextUnformatted("Lead"); // header widgets
//     timeline.Notes(notes, 0.0, song_end); // lane content
//     timeline.EndTrack();
//     timeline.End();
//     if (timeline.Seeked(&seconds)) { ... }
//
// Pressing the ruler or a lane moves the cursor there. The wheel zooms about the mouse,
// Shift+wheel scrolls in time, and the wheel over the headers scrolls through the tracks.
class Timeline {
public:
    static constexpr float kHeaderWidth = 190.0f;
    static constexpr float kTrackHeight = 46.0f;

    void Begin(const char* id, double cursor, ImVec2 size = ImVec2(0.0f, 0.0f));
    void End();
    // Where the ruler or a lane was pressed or dragged to this frame.
    bool Seeked(double* seconds) const;

    // Leaves the ImGui cursor in the new track's header for its widgets.
    void BeginTrack(const char* id, float height = kTrackHeight);
    void EndTrack();
    // One line of text: a row naming the tracks under it.
    static float TitleHeight() { return ImGui::GetTextLineHeight() + 8.0f; }

    // Content of the current track's lane. A repeat does not lengthen the scroll range, so a
    // looping clip can be drawn over whatever is visible.
    void Clip(const WavClip& clip, const WaveformPeaks& peaks, double start, double sample_rate,
              bool highlight = false, bool repeat = false);
    // Notes on a block from start to end, scaled to the lane's height; those under the cursor
    // are lit.
    void Notes(const std::vector<TimelineNote>& notes, double start, double end);
    // A signal without a length, across the whole lane; dim while inactive.
    void Band(const char* label, bool active);

    // The current track's lane as a button. A track that uses a press itself (to drag a clip,
    // say) claims it, and the cursor stays.
    bool LaneActivated() const { return lane_activated_; }
    bool LaneActive() const { return lane_active_; }
    void ClaimLane() { lane_claimed_ = true; }
    // Time under the mouse, never negative.
    double MouseSeconds() const;
    double SecondsPerPixel() const { return seconds_per_pixel_; }
    // The span of time in view.
    double VisibleStart() const { return SecondsAt(lanes_x_); }
    double VisibleEnd() const { return SecondsAt(view_max_.x); }

private:
    float XAt(double seconds) const;
    double SecondsAt(float x) const;
    void HandleWheel();
    void DrawTicks(float top, float bottom) const;
    void DrawRuler() const;
    void DrawCursor(float top, float bottom) const;
    void ExtendTo(double seconds);

    double seconds_per_pixel_ = 0.05;
    double length_ = 60.0;      // seconds the scroll range covers, from the last frame
    double next_length_ = 0.0;  // the same, gathered this frame
    double cursor_ = 0.0;
    bool seeked_ = false;
    double seek_ = 0.0;

    // Set by Begin(), screen coordinates.
    ImVec2 ruler_min_;
    ImVec2 view_min_;   // top left of the visible tracks
    ImVec2 view_max_;
    float lanes_x_ = 0.0f; // where time 0 is when not scrolled
    float scroll_x_ = 0.0f;
    float row_y_ = 0.0f;   // top of the next track, in the scrolled content
    double tick_step_ = 1.0;
    bool ruler_active_ = false;

    // The current track.
    ImVec2 lane_min_;
    ImVec2 lane_max_;
    bool lane_activated_ = false;
    bool lane_active_ = false;
    bool lane_claimed_ = false;
};
