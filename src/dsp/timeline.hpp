#pragma once

#include "module.hpp"
#include "wav_generator.hpp"
#include "ui/file_picker.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
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

// Six tracks of WAV clips placed in time, one output pin each. Plays from the cursor.
class Timeline : public Module {
public:
    static constexpr int kTracks = 6;
    static_assert(kTracks <= kMaxOutputs, "every track needs an output pin");
    bool playing = false;

    std::string name() const override { return "timeline"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    void get_outputs(int* count, const char* const** str) const override;
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void render_window() override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;

private:
    struct Track {
        std::string path;
        std::string error;
        std::shared_ptr<const WavClip> clip;
        std::unique_ptr<WaveformPeaks> peaks;
        int64_t start = 0; // frame where the clip begins
        FilePicker picker{"Load WAV", ".wav"};
    };
    bool load_track(int track, const std::string& path);
    void publish();
    void play_button();
    // start, moved so the clip's start or end meets the nearest edge of another clip
    // within a few pixels.
    int64_t snap(int track, int64_t start) const;

    // UI thread.
    std::array<Track, kTracks> tracks_;
    int64_t cursor_ = 0; // frame where playing starts
    bool window_open_ = false;
    float frames_per_pixel_ = 2400.0f;
    int dragging_ = -1;     // track whose clip is being dragged
    int64_t grab_frame_ = 0; // clip frame under the mouse while dragging

    // Handoff, as in WavPlayer: publish() fills next_; process() swaps it with placed_, so
    // replaced clips are freed by the next publish() on the UI thread.
    struct Placed {
        std::shared_ptr<const WavClip> clip;
        int64_t start = 0;
    };
    std::mutex placed_mutex_;
    std::array<Placed, kTracks> next_;
    bool changed_ = false;
    std::array<Placed, kTracks> placed_; // audio thread

    std::atomic<int64_t> position_{0}; // frame being played; the UI sets it to seek
};
