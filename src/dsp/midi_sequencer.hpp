#pragma once

#include "midi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

class Instrument;

// One loaded MIDI song played at a sample position: each track drives its own instrument.
// It is loaded (and read) on the UI thread, then handed to the audio thread, which owns it
// from then on (see RtHandoff). Nothing in render() allocates or blocks.
class MidiSequencer {
public:
    static constexpr int kMaxTracks = 10;
    using Instruments = std::array<Instrument*, kMaxTracks>;

    // UI thread, before handing it over. Track t plays on instruments[t]; instruments outlive
    // the sequencer and are reset when it first plays.
    bool load(const std::string& path, const Instruments& instruments, std::string& error);
    // To read the song's events before it plays; leaves it rewound.
    MidiSong& song() { return song_; }
    void rewind_song();
    int track_count() const { return tracks_; }

    // Audio thread. Clears outputs[0, output_count), then renders frames frames of track t into
    // outputs[t], from frame target of the song: seeking, muted, when target is not where the
    // last block ended.
    void render(float* const* outputs, int output_count, uint64_t target, size_t frames);
    // Where playback is, from the samples the sequencer has rendered.
    double playhead_beat() const;
    uint64_t played_frames() const { return played_frames_; }
    bool finished() const { return finished_; }

private:
    void rewind();
    void seek(uint64_t target);
    // Runs the sequencer for at most frames; returns how many it ran, 0 at the end.
    size_t step(size_t frames);

    std::unique_ptr<uint8_t, decltype(&std::free)> buffer_{nullptr, &std::free};
    MidiSong song_;
    Instruments instruments_{};
    int tracks_ = 0;
    uint32_t start_us_per_tick_ = 0; // the sequencer's tempo before the song sets one
    bool started_ = false;

    // The sequencer jumps its tick to the next event when it starts rendering the gap before
    // it, so the playhead is the tick that gap starts at plus the samples rendered into it.
    uint32_t interval_start_tick_ = 0;
    uint64_t interval_frames_ = 0;
    uint64_t played_frames_ = 0;
    bool finished_ = false;
};
