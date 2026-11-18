#pragma once

#include "module.hpp"
#include "dsp/midi_sequencer.hpp"
#include "dsp/rt_handoff.hpp"
#include "ui/file_picker.hpp"
#include "ui/piano_roll.hpp"
#include "ui/timeline_note_lane.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// Plays a MIDI file, each of its first kTracks tracks on its own piano and output pin, at the
// shared transport's position. Each track has a piano roll window that shows the playhead.
// Loading a file sets the song grid (time signature and tempo map) from it.
//
// Lock-free: a loaded song is a MidiSequencer handed to the audio thread whole, and what the
// UI shows of playback is read from atomics, so the graph never takes the control mutex.
class Midi : public Module {
public:
    static constexpr int kTracks = MidiSequencer::kMaxTracks;
    static_assert(kTracks <= kMaxOutputs, "every track needs an output pin");

    // Where playback is, from the samples the sequencer has rendered.
    double playhead_beat() const { return playhead_beat_.load(std::memory_order_relaxed); }
    double playhead_seconds() const;
    bool finished() const { return finished_.load(std::memory_order_relaxed); }
    const audio_effect::PianoRollEditor& piano_roll(int track) const { return piano_rolls_[track]; }
    const std::string& track_name(int track) const { return track_names_[track]; }

    std::string name() const override { return "midi"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    void get_outputs(int* count, const char* const** str) const override;
    // UI thread.
    bool set_path(const std::string& path);
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void render_window() override;
    bool render_timeline(Timeline& timeline, const char* title) override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;
    bool needs_control_lock() const override { return false; }

private:
    void transport_row();
    void roll_button(int track);

    // UI thread.
    FilePicker picker_{"Load Midi", ".mid"};
    std::string path_;
    std::string error_;
    int track_count_ = 0;
    // Made here and never freed: Instrument has no virtual destructor, and a replaced
    // sequencer's tracks may still ring through them until the next one resets them.
    MidiSequencer::Instruments instruments_{};
    std::array<audio_effect::PianoRollEditor, kTracks> piano_rolls_;
    std::array<bool, kTracks> piano_roll_open_{};
    std::array<std::string, kTracks> track_names_;
    bool follow_playhead_ = true;
    std::vector<TimelineNote> timeline_notes_; // scratch for render_timeline()

    RtHandoff<MidiSequencer> sequencer_;
    MidiNoteEditQueue midi_edits_;
    uint32_t midi_edit_generation_ = 0;
    // Written by the audio thread after each block.
    std::atomic<double> playhead_beat_{0.0};
    std::atomic<uint64_t> played_frames_{0};
    std::atomic<bool> finished_{false};
    std::atomic<bool> midi_edit_failed_{false};
};
