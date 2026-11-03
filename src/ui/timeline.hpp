#pragma once

#include "imgui.h"

// Tracks against one time axis: a ruler in bars and beats of the song's grid (see
// song_grid()) on top, a header column on the left, a lane per track and one cursor across
// them all. The tracks are described every frame, between Begin() and End():
//
//     timeline.Begin("##tracks", cursor);
//     timeline.BeginTrack("lead");
//     ImGui::TextUnformatted("Lead"); // header widgets
//     draw_notes(timeline, notes, 0.0, song_end); // lane content, from a lane module
//     timeline.EndTrack();
//     timeline.End();
//     if (timeline.Seeked(&seconds)) { ... }
//
// Pressing the ruler or a lane moves the cursor there. The wheel zooms about the mouse,
// Shift+wheel scrolls in time, and the wheel over the headers scrolls through the tracks.
// What lanes hold is drawn by the lane modules (timeline_wav_lane, timeline_note_lane,
// timeline_band_lane) through the geometry below.
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
    static float TitleHeight();

    // The current track's lane as a button. A track that uses a press itself (to drag a clip,
    // say) claims it, and the cursor stays.
    bool LaneActivated() const { return lane_activated_; }
    bool LaneActive() const { return lane_active_; }
    bool LaneHovered() const { return lane_hovered_; }
    void ClaimLane() { lane_claimed_ = true; }
    // Time under the mouse, never negative.
    double MouseSeconds() const;
    double SecondsPerPixel() const { return seconds_per_pixel_; }
    // The span of time in view.
    double VisibleStart() const { return SecondsAt(lanes_x_); }
    double VisibleEnd() const { return SecondsAt(view_max_.x); }

    // For lane modules, in screen coordinates.
    float XAt(double seconds) const;
    double SecondsAt(float x) const;
    ImVec2 LaneMin() const { return lane_min_; }
    ImVec2 LaneMax() const { return lane_max_; }
    double Cursor() const { return cursor_; }
    // Content up to seconds: the scroll range covers it.
    void ExtendTo(double seconds);
    // Clips drawing to the part of the current lane that is in view.
    void PushLaneClip() const;
    void PopLaneClip() const;

private:
    void HandleWheel();
    void DrawTicks(float top, float bottom) const;
    void DrawRuler() const;
    void DrawCursor(float top, float bottom) const;
    // Calls line(seconds, kind, bar, beat) for the grid lines in view: kind 0 for a bar,
    // 1 a beat, 2 a grid division; labelled when there is room for a label.
    template <typename F>
    void ForEachGridLine(F line) const;

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
    bool ruler_active_ = false;

    // The current track.
    ImVec2 lane_min_;
    ImVec2 lane_max_;
    bool lane_activated_ = false;
    bool lane_active_ = false;
    bool lane_hovered_ = false;
    bool lane_claimed_ = false;
};
