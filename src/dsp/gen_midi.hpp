#pragma once

#include "module.hpp"
#include "wav_generator.hpp"
#include "ui/file_picker.hpp"
#include "ui/piano_roll.hpp"
#include "ui/timeline.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "midi.h"
#include "inst-piano.h"

// Plays a MIDI file, each of its first kTracks tracks on its own piano and output pin, at the
// shared transport's position. Each track has a piano roll window that shows the playhead.
class Midi : public Module {
public:
    static constexpr int kTracks = 10;
    static_assert(kTracks <= kMaxOutputs, "every track needs an output pin");

    // Where playback is, from the samples the sequencer has rendered.
    double playhead_beat() const;
    double playhead_seconds() const { return static_cast<double>(played_samples_) / kSampleRate; }
    bool finished() const { return finished_; }
    // Back to the start of the song, silencing held notes.
    void rewind();
    const audio_effect::PianoRollEditor& piano_roll(int track) const { return piano_rolls_[track]; }
    const std::string& track_name(int track) const { return track_names_[track]; }

    std::string name() const override { return "midi"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    void get_outputs(int* count, const char* const** str) const override;
    bool set_path(const std::string& path);
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void render_window() override;
    bool render_timeline(Timeline& timeline, const char* title) override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;

private:
    void transport();
    void seek(uint64_t target);
    // Runs the sequencer for at most samples; returns how many it ran, 0 at the end.
    size_t step(size_t samples);
    int loaded_tracks(); // MidiSong::GetTrackCount() is not const
    void roll_button(int track);

    FilePicker picker_{"Load Midi", ".mid"};
    std::string path_;
    std::string error_;    
    unsigned char *pMidi_buffer = nullptr;
    size_t midi_size = 0;
    MidiSong midi_song;
    std::array<Instrument *, kTracks> instruments_{};
    std::array<audio_effect::PianoRollEditor, kTracks> piano_rolls_;
    std::array<bool, kTracks> piano_roll_open_{};
    std::array<std::string, kTracks> track_names_;
    bool follow_playhead_ = true;
    std::vector<TimelineNote> timeline_notes_; // scratch for render_timeline()

    // Playback position, advanced by process(). The sequencer jumps its tick to the next event
    // when it starts rendering the gap before it, so the playhead is the tick that gap starts
    // at plus the samples rendered into it so far.
    uint32_t interval_start_tick_ = 0;
    uint64_t interval_samples_ = 0;
    uint64_t played_samples_ = 0;
    bool finished_ = false;
    uint32_t start_us_per_tick_ = 0; // the sequencer's tempo before the song sets one
};
