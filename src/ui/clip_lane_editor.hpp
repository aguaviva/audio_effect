#pragma once

#include "ui/file_picker.hpp"

#include <cstdint>

class ClipArrangement;
class Timeline;
class WavLibrary;

// Edits a ClipArrangement as timeline lanes, one per channel: drag a clip to move it or its
// edges to trim it, right-click a lane to load a WAV there or paste, and Ctrl+C / X / V / D,
// Delete as in the piano roll. Every edit snaps to the song's grid (see ClipSnap).
class ClipLaneEditor {
public:
    explicit ClipLaneEditor(uint32_t sample_rate) : sample_rate_(sample_rate) {}

    // The lanes, as tracks of the timeline; each header reads lane_name and its number.
    void draw(Timeline& timeline, ClipArrangement& arrangement, const char* lane_name);
    // Adds the file picked in the dialog, which outlives the timeline window; every frame.
    void poll(ClipArrangement& arrangement, WavLibrary& library);

private:
    enum class Grab { None, Body, Start, End };
    struct Hit {
        int index = -1;
        Grab part = Grab::None;
    };

    Hit hit_test(const ClipArrangement& arrangement, int channel, int64_t frame,
                 double frames_per_pixel) const;
    void select(int channel, int index);
    bool has_selection(const ClipArrangement& arrangement) const;
    void handle_keys(const Timeline& timeline, ClipArrangement& arrangement, int hovered);
    void context_menu(ClipArrangement& arrangement);
    void open_picker(const ClipArrangement& arrangement, int channel, int64_t frame);
    // The clipboard's clip at frame (snapped); its index, or -1.
    int paste(ClipArrangement& arrangement, int channel, int64_t frame, double frames_per_pixel);
    int64_t snap_edge(const ClipArrangement& arrangement, int64_t frame, double frames_per_pixel,
                      int channel = -1, int index = -1) const;

    uint32_t sample_rate_;
    FilePicker picker_{"Load WAV", ".wav"};
    int load_channel_ = 0; // where a picked file goes
    int64_t load_frame_ = 0;
    int selected_channel_ = -1;
    int selected_ = -1;
    Grab dragging_ = Grab::None;
    int drag_channel_ = -1;
    int drag_index_ = -1;
    int64_t grab_frame_ = 0; // mouse frame less the grabbed start or edge
    bool open_menu_ = false;
    int menu_channel_ = 0;
    int64_t menu_frame_ = 0;
    int menu_clip_ = -1;
    double menu_frames_per_pixel_ = 1.0;
};
