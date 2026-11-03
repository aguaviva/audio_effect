#pragma once

#include "dsp/rt_handoff.hpp"
#include "dsp/wav_generator.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// What a ClipPlayer plays: per channel, stretches of WAV files placed in time (in frames).
struct ClipSchedule {
    struct Clip {
        std::shared_ptr<const WavClip> wav;
        int64_t start = 0;
        int64_t offset = 0;
        int64_t frames = 0;
    };
    std::vector<std::vector<Clip>> channels;
};

// Plays a ClipSchedule on the audio thread. The UI publishes a new schedule after each edit;
// the audio thread picks it up without waiting, and the replaced one (and any WAV only it
// used) is freed on the UI thread.
class ClipPlayer {
public:
    // UI thread.
    void publish(ClipSchedule schedule);
    void collect();

    // Audio thread. Clears every output that has a buffer, then mixes channel c's clips over
    // [frame, frame + frames) into outputs[c].
    void render(float* const* outputs, int output_count, int64_t frame, size_t frames);

private:
    RtHandoff<const ClipSchedule> handoff_;
};
