#include "midi_sequencer.hpp"

#include "dsp/module.hpp"
#include "instrument.h"
#include "misc.h"

#include <algorithm>
#include <type_traits>
#include <utility>

bool MidiSequencer::load(const std::string& path, const Instruments& instruments,
                         std::string& error) {
    uint8_t* data = nullptr;
    size_t size = 0;
    if (!load_file(path.c_str(), &data, &size)) {
        error = "Could not open the file";
        return false;
    }
    buffer_.reset(data);
    if (!song_.LoadMidi(buffer_.get(), size)) {
        error = "Not a MIDI file";
        return false;
    }
    instruments_ = instruments;
    tracks_ = static_cast<int>(std::min<uint32_t>(kMaxTracks, song_.GetTrackCount()));
    for (int t = 0; t < tracks_; ++t) {
        Instrument* inst = instruments_[t];
        song_.pFunc[t] = [inst](uint8_t* pdata, size_t n) { inst->send_midi(pdata, n); };
    }
    const uint32_t tpq = std::max<uint32_t>(1, song_.m_midi_state.m_ticksPerQuarterNote);
    start_us_per_tick_ = (60 * 1000000 / 110) / tpq;
    rewind_song();
    return true;
}

void MidiSequencer::rewind_song() {
    song_.Reset();
    // Reset() leaves the tempo of the last tempo event played.
    if (start_us_per_tick_ > 0) {
        song_.m_midi_state.m_microSecondsPerMidiTick = start_us_per_tick_;
    }
    interval_start_tick_ = 0;
    interval_frames_ = 0;
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
    double tick = finished_ ? state.m_time : interval_start_tick_;
    if (!finished_ && state.m_microSecondsPerMidiTick > 0) {
        tick += static_cast<double>(interval_frames_) * 1.0e6 /
                (static_cast<double>(kSampleRate) * state.m_microSecondsPerMidiTick);
    }
    tick = std::min<double>(tick, state.m_time);
    return tick / std::max<uint32_t>(1, state.m_ticksPerQuarterNote);
}

// Rewinds when going back, then runs the sequencer to target with its notes muted.
void MidiSequencer::seek(uint64_t target) {
    if (target < played_frames_) {
        rewind();
    }
    constexpr size_t kSendFuncs = std::extent_v<decltype(MidiSong::pFunc)>;
    std::array<MidiSendFunc, kSendFuncs> muted;
    for (int t = 0; t < tracks_; ++t) {
        std::swap(song_.pFunc[t], muted[t]);
    }
    while (played_frames_ < target && !finished_) {
        step(target - played_frames_);
    }
    for (int t = 0; t < tracks_; ++t) {
        std::swap(song_.pFunc[t], muted[t]);
        instruments_[t]->reset();
    }
}

size_t MidiSequencer::step(size_t frames) {
    const uint32_t tick = song_.m_midi_state.m_time;
    const size_t n = song_.RenderMidi(kSampleRate, frames);
    if (n == NO_EVENTS) {
        finished_ = true;
        return 0;
    }
    if (song_.m_midi_state.m_time != tick) {
        interval_start_tick_ = tick;
        interval_frames_ = 0;
    }
    interval_frames_ += n;
    played_frames_ += n;
    return n;
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
