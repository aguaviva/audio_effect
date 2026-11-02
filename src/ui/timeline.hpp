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

// A note on a timeline track, in seconds.
struct TimelineNote {
    double start = 0.0;
    double duration = 0.0;
    int pitch = 60;
};

// Tracks against one time axis: a ruler in seconds on top, a header column on the left and a
// lane per track. The tracks are described every frame, between Begin() and End():
//
//     timeline.Begin("##tracks");
//     timeline.BeginTrack("lead");
//     ImGui::TextUnformatted("Lead"); // header widgets
//     timeline.Notes(notes, playhead); // lane content
//     timeline.EndTrack();
//     timeline.End();
//
// The wheel zooms about the mouse, Shift+wheel scrolls in time, and the wheel over the
// headers scrolls through the tracks.
class Timeline {
public:
    static constexpr float kHeaderWidth = 190.0f;
    static constexpr float kTrackHeight = 46.0f;

    void Begin(const char* id, ImVec2 size = ImVec2(0.0f, 0.0f));
    void End();

    // Leaves the cursor in the new track's header for its widgets.
    void BeginTrack(const char* id, float height = kTrackHeight);
    void EndTrack();

    // Content of the current track's lane.
    void Clip(const WavClip& clip, const WaveformPeaks& peaks, double start, double sample_rate,
              bool highlight = false);
    // Notes scaled to the lane's height; those sounding at playhead are lit.
    void Notes(const std::vector<TimelineNote>& notes, double playhead = -1.0);
    // A signal without a length, across the whole lane; dim while inactive.
    void Band(const char* label, bool active);
    void Playhead(double seconds);
    // Where playing starts.
    void Marker(double seconds);

    // The current track's lane as a button.
    bool LaneActivated() const { return lane_activated_; }
    bool LaneActive() const { return lane_active_; }
    // Time under the mouse, never negative.
    double MouseSeconds() const;
    double SecondsPerPixel() const { return seconds_per_pixel_; }

private:
    float XAt(double seconds) const;
    double SecondsAt(float x) const;
    void HandleWheel();
    void DrawTicks(float top, float bottom) const;
    void DrawRuler() const;
    void ExtendTo(double seconds);

    double seconds_per_pixel_ = 0.05;
    double length_ = 60.0;      // seconds the scroll range covers, from the last frame
    double next_length_ = 0.0;  // the same, gathered this frame

    // Set by Begin(), screen coordinates.
    ImVec2 ruler_min_;
    ImVec2 view_min_;   // top left of the visible tracks
    ImVec2 view_max_;
    float lanes_x_ = 0.0f; // where time 0 is when not scrolled
    float scroll_x_ = 0.0f;
    float row_y_ = 0.0f;   // top of the next track, in the scrolled content
    double tick_step_ = 1.0;

    // The current track.
    ImVec2 lane_min_;
    ImVec2 lane_max_;
    bool lane_activated_ = false;
    bool lane_active_ = false;
};
