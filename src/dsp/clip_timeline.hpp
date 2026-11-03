#pragma once

#include "module.hpp"
#include "wav_generator.hpp"
#include "ui/file_picker.hpp"
#include "ui/timeline.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Six tracks of WAV clips placed in time, one output pin each, played by the shared transport.
// A track holds any number of clips, from any files; where they overlap they mix. The clips
// are placed, trimmed, copied and pasted in the app's timeline.
class ClipTimeline : public Module {
public:
    static constexpr int kTracks = 6;
    static_assert(kTracks <= kMaxOutputs, "every track needs an output pin");

    // Frames [offset, offset + length) of a WAV file, played from start on. Trimming only
    // moves those bounds; the file is untouched.
    struct Clip {
        std::string path;
        int64_t start = 0;
        int64_t offset = 0;
        int64_t length = -1; // to the end of the file
        std::shared_ptr<const WavClip> wav;
        std::shared_ptr<const WaveformPeaks> peaks;

        // What it plays: nothing while its file is missing.
        int64_t frames() const;
        int64_t end() const { return start + frames(); }
    };

    std::string name() const override { return "timeline"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    void get_outputs(int* count, const char* const** str) const override;
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void render_window() override;
    bool render_timeline(Timeline& timeline, const char* title) override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;

    // UI thread. Indices are into clips(track), which keeps the order clips were added in.
    const std::vector<Clip>& clips(int track) const { return tracks_[track].clips; }
    // The whole of path's file at start; -1, with the reason in error, if it does not load.
    int add_clip(int track, const std::string& path, int64_t start, std::string& error);
    void move_clip(int track, int index, int64_t start);
    // Moves one edge of the clip, within the file and leaving at least a frame.
    void trim_start(int track, int index, int64_t start);
    void trim_end(int track, int index, int64_t end);
    void remove_clip(int track, int index);
    // One clipboard for every Clip Timeline.
    void copy_clip(int track, int index) const;
    static bool can_paste();
    // The clipboard's clip at start; its index, or -1 with nothing copied.
    int paste(int track, int64_t start);

private:
    struct Track {
        std::vector<Clip> clips;
        std::string error; // of the last file that failed to load here
    };
    struct Source {
        std::shared_ptr<const WavClip> wav;
        std::shared_ptr<const WaveformPeaks> peaks;
        std::string error;
    };
    enum class Grab { None, Body, Start, End };
    struct Hit {
        int index = -1;
        Grab part = Grab::None;
    };

    const Source& source(const std::string& path);
    void publish();
    Hit hit_test(int track, int64_t frame, double frames_per_pixel) const;
    // frame, moved onto the nearest clip edge within a few pixels, ignoring the given clip.
    int64_t snap_edge(int64_t frame, int track, int index, double frames_per_pixel) const;
    // A start for the clip that puts its start or end on the nearest edge.
    int64_t snap_start(int track, int index, int64_t start, double frames_per_pixel) const;
    void select(int track, int index);
    void handle_keys(Timeline& timeline, int hovered_track);
    void context_menu();
    void open_picker(int track, int64_t frame);

    // UI thread.
    std::array<Track, kTracks> tracks_;
    std::map<std::string, Source> sources_; // loaded files, shared by their clips
    FilePicker picker_{"Load WAV", ".wav"};
    int load_track_ = 0;      // where a picked file goes
    int64_t load_frame_ = 0;
    int selected_track_ = -1;
    int selected_ = -1;
    Grab dragging_ = Grab::None;
    int drag_track_ = -1;
    int drag_index_ = -1;
    int64_t grab_frame_ = 0;  // mouse frame less the grabbed start or edge
    bool open_menu_ = false;
    int menu_track_ = 0;
    int64_t menu_frame_ = 0;
    int menu_clip_ = -1;

    // Handoff, as in WavPlayer: publish() fills next_; process() swaps it with placed_, so
    // replaced clips are freed by the next publish() on the UI thread.
    struct Placed {
        std::shared_ptr<const WavClip> wav;
        int64_t start = 0;
        int64_t offset = 0;
        int64_t frames = 0;
    };
    using Placement = std::array<std::vector<Placed>, kTracks>;
    std::mutex placed_mutex_;
    Placement next_;
    bool changed_ = false;
    Placement placed_; // audio thread
};
