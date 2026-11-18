#pragma once

#include "midi.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>

class Instrument;

struct MidiNoteEdit {
    bool insert = false;
    uint32_t generation = 0;
    uint8_t track = 0;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
    uint8_t channel = 0;
    uint8_t end_status = 0x80;
    uint8_t end_velocity = 0;
    uint32_t start_tick = 0;
    uint32_t duration_ticks = 0;
};

class MidiNoteEditQueue {
public:
    bool push(const MidiNoteEdit& edit) {
        const size_t write = write_.load(std::memory_order_relaxed);
        const size_t next = (write + 1) % kCapacity;
        if (next == read_.load(std::memory_order_acquire)) {
            return false;
        }
        edits_[write] = edit;
        write_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(MidiNoteEdit& edit) {
        const size_t read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) {
            return false;
        }
        edit = edits_[read];
        read_.store((read + 1) % kCapacity, std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t kCapacity = 1024;
    std::array<MidiNoteEdit, kCapacity> edits_{};
    std::atomic<size_t> read_{0};
    std::atomic<size_t> write_{0};
};

// One loaded MIDI song played at a sample position: each track drives its own instrument.
// It is loaded (and read) on the UI thread, then handed to the audio thread, which owns it
// from then on (see RtHandoff). Nothing in render() allocates or blocks.
class MidiSequencer {
public:
    static constexpr int kMaxTracks = 10;
    using Instruments = std::array<Instrument*, kMaxTracks>;

    // UI thread, before handing it over. Track t plays on instruments[t]; instruments outlive
    // the sequencer and are reset when it first plays.
    bool load(const std::string& path, const Instruments& instruments,
              MidiNoteEditQueue& edits, uint32_t generation, std::string& error);
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
    void apply_pending_edits();
    bool take_edit_error() { return edit_error_.exchange(false, std::memory_order_relaxed); }

private:
    void rewind();
    void seek(uint64_t target);
    // Runs the sequencer for at most frames; returns how many it ran, 0 at the end.
    size_t step(size_t frames);

    std::unique_ptr<uint8_t, decltype(&std::free)> buffer_{nullptr, &std::free};
    MidiSong song_;
    Instruments instruments_{};
    MidiNoteEditQueue* edits_ = nullptr;
    uint32_t generation_ = 0;
    std::atomic<bool> edit_error_{false};
    std::array<MidiSendFunc, kMaxTracks> midi_callbacks_{};
    int tracks_ = 0;
    bool started_ = false;

    uint64_t interval_remaining_frames_ = 0;
    uint64_t played_frames_ = 0;
    bool finished_ = false;
};
