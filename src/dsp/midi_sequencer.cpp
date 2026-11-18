#include "midi_sequencer.hpp"

#include "dsp/module.hpp"
#include "instrument.h"
#include "misc.h"

#include <algorithm>
#include <type_traits>
#include <utility>

bool MidiSequencer::load(const std::string& path, const Instruments& instruments,
                         MidiNoteEditQueue& edits, uint32_t generation,
                         std::string& error) {
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!load_file(path.c_str(), &data, &size)) {
        error = "Could not open the file";
        return false;
    }
    buffer_.reset(data);
    if (!song_.LoadMidi(buffer_.get(), size, true)) {
        error = "Not a MIDI file";
        return false;
    }
    instruments_ = instruments;
    edits_ = &edits;
    generation_ = generation;
    tracks_ = static_cast<int>(std::min<uint32_t>(kMaxTracks, song_.GetTrackCount()));
    for (int t = 0; t < tracks_; ++t) {
        Instrument* inst = instruments_[t];
        midi_callbacks_[t] = [inst](uint8_t* pdata, size_t n) { inst->send_midi(pdata, n); };
    }
    rewind_song();
    return true;
}

void MidiSequencer::apply_pending_edits() {
    if (!edits_) {
        return;
    }
    MidiNoteEdit edit;
    bool changed = false;
    while (edits_->pop(edit)) {
        if (edit.generation != generation_) {
            continue;
        }
        if (edit.track >= tracks_) {
            edit_error_.store(true, std::memory_order_relaxed);
            continue;
        }
        MidiTrackEditable* track = song_.GetEditableTrack(edit.track);
        if (!track) {
            edit_error_.store(true, std::memory_order_relaxed);
            continue;
        }
        if (edit.insert) {
            const bool inserted = track->insertNote(edit.start_tick, edit.pitch, edit.velocity,
                                                    edit.duration_ticks, edit.channel);
            changed |= inserted;
            if (!inserted) {
                edit_error_.store(true, std::memory_order_relaxed);
            }
            continue;
        }
        const uint8_t on_status = static_cast<uint8_t>(0x90 | edit.channel);
        const uint8_t off_status = edit.end_status;
        const uint32_t end_tick = edit.start_tick + edit.duration_ticks;
        bool removed = track->deleteEventAtTick(
            edit.start_tick, {on_status, edit.pitch, edit.velocity});
        if (!removed) {
            removed = track->deleteEventAtTick(edit.start_tick, {edit.pitch, edit.velocity});
        }
        bool released = false;
        if (off_status != 0) {
            released = track->deleteEventAtTick(
                end_tick, {off_status, edit.pitch, edit.end_velocity});
            if (!released) {
                released = track->deleteEventAtTick(
                    end_tick, {edit.pitch, edit.end_velocity});
            }
        }
        changed |= removed || released;
        if (!removed || (off_status != 0 && !released)) {
            edit_error_.store(true, std::memory_order_relaxed);
        }
    }
    if (changed) {
        song_.Reset();
        interval_remaining_frames_ = 0;
        played_frames_ = 0;
        finished_ = false;
        for (int t = 0; t < tracks_; ++t) {
            instruments_[t]->reset();
        }
    }
}

void MidiSequencer::rewind_song() {
    song_.Reset();
    interval_remaining_frames_ = 0;
    played_frames_ = 0;
    finished_ = false;
}

void MidiSequencer::rewind() {
    rewind_song();
    for (int t = 0; t < tracks_; ++t) {
        instruments_[t]->reset();
    }
}

double MidiSequencer::playhead_beat() const {
    const MidiState& state = song_.m_midi_state;
    if (finished_) {
        return static_cast<double>(state.m_time) /
               std::max<uint32_t>(1, state.m_ticksPerQuarterNote);
    }
    MidiSong& mutable_song = const_cast<MidiSong&>(song_);
    const uint64_t microseconds =
        played_frames_ * 1000000 / std::max<uint32_t>(1, kSampleRate);
    const double tick =
        static_cast<double>(mutable_song.microseconds_to_ticks(
            static_cast<uint32_t>(std::min<uint64_t>(microseconds, UINT32_MAX))));
    return tick / std::max<uint32_t>(1, state.m_ticksPerQuarterNote);
}

// Rewinds when going back, then runs the sequencer to target with its notes muted.
void MidiSequencer::seek(uint64_t target) {
    if (target < played_frames_) {
        rewind();
    }
    std::array<MidiSendFunc, kMaxTracks> muted;
    const uint64_t target_us =
        target * 1000000 / std::max<uint32_t>(1, kSampleRate);
    song_.seek(target_us, muted.data());
    interval_remaining_frames_ = 0;
    played_frames_ = target;
    finished_ = target_us >= song_.get_total_us();
    for (int t = 0; t < tracks_; ++t) {
        instruments_[t]->reset();
    }
}

size_t MidiSequencer::step(size_t frames) {
    if (interval_remaining_frames_ == 0) {
        const size_t microseconds = song_.step(midi_callbacks_.data());
        if (microseconds == NO_EVENTS) {
            finished_ = true;
            return 0;
        }
        const uint64_t interval_frames = std::max<uint64_t>(1,
            (static_cast<uint64_t>(microseconds) * kSampleRate + 500000) / 1000000);
        interval_remaining_frames_ = interval_frames;
    }
    const size_t rendered = std::min<uint64_t>(frames, interval_remaining_frames_);
    interval_remaining_frames_ -= rendered;
    played_frames_ += rendered;
    return rendered;
}

void MidiSequencer::render(float* const* outputs, int output_count, uint64_t target,
                           size_t frames) {
    for (int t = 0; t < output_count; ++t) {
        if (outputs[t]) {
            std::fill(outputs[t], outputs[t] + 2 * frames, 0.0f);
        }
    }
    if (!started_) {
        // The instruments may still ring from the song this one replaced.
        rewind();
        started_ = true;
    }
    // In step with the transport unless it moved (or a block was missed): then seek.
    if (target != played_frames_ && !(finished_ && target > played_frames_)) {
        seek(target);
    }
    const int tracks = std::min(tracks_, output_count);
    size_t offset = 0;
    while (frames > 0 && !finished_) {
        const size_t n = step(frames);
        for (int t = 0; t < tracks; ++t) {
            if (outputs[t]) {
                instruments_[t]->render_samples(static_cast<uint32_t>(n), outputs[t] + 2 * offset);
            }
        }
        frames -= n;
        offset += n;
    }
}
