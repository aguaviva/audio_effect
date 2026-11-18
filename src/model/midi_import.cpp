#include "midi_import.hpp"

#include "instrument.h"
#include "midi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace {

class NoteDrawInstrument : public Instrument {
public:
    NoteDrawInstrument(std::vector<ImportedNote>& notes, const MidiState& state)
        : notes_(notes), state_(state),
          ticks_per_beat_(std::max<uint32_t>(1, state.m_ticksPerQuarterNote)) {
        for (auto& channel : starts_) {
            channel.fill(-1);
        }
    }

    void send_midi(uint8_t* data, size_t size) override {
        if (size < 3) {
            return;
        }
        const uint8_t type = data[0] >> 4;
        if (type != 0x8 && type != 0x9) {
            return;
        }
        const uint8_t channel = data[0] & 0x0f;
        const uint8_t key = data[1] & 0x7f;
        const uint32_t tick = state_.m_time;
        int64_t& start = starts_[channel][key];
        if (start >= 0) {
            notes_.push_back({key, static_cast<double>(start) / ticks_per_beat_,
                              static_cast<double>(tick - start) / ticks_per_beat_,
                              velocities_[channel][key], channel, data[0], data[2]});
            start = -1;
        }
        if (type == 0x9 && data[2] > 0) {
            start = tick;
            velocities_[channel][key] = data[2];
        }
    }

    void render_samples(uint32_t, float*) override {}

    void finish() {
        for (size_t channel = 0; channel < starts_.size(); ++channel) {
            for (size_t key = 0; key < starts_[channel].size(); ++key) {
                if (starts_[channel][key] >= 0) {
                    notes_.push_back({static_cast<int>(key),
                                      static_cast<double>(starts_[channel][key]) /
                                          ticks_per_beat_,
                                      1.0, velocities_[channel][key],
                                      static_cast<int>(channel), 0, 0});
                }
            }
        }
    }

private:
    std::vector<ImportedNote>& notes_;
    const MidiState& state_;
    uint32_t ticks_per_beat_;
    std::array<std::array<int64_t, 128>, 16> starts_{};
    std::array<std::array<int, 128>, 16> velocities_{};
};

} // namespace

MidiSongInfo read_midi_song(MidiSong& song, int max_tracks) {
    MidiSongInfo info;
    const int tracks = std::min<int>(max_tracks, static_cast<int>(song.GetTrackCount()));
    const uint32_t tpq = std::max<uint32_t>(1, song.m_midi_state.m_ticksPerQuarterNote);
    info.track_names.resize(static_cast<size_t>(tracks));
    info.notes.resize(static_cast<size_t>(tracks));

    // Use the tempo map already computed by SimpleMIDI's midi.cpp. It includes the
    // default 120 BPM tempo and the absolute ticks for all tempo meta events.
    for (const ::TempoChange& tempo : song.getTempoChanges()) {
        if (tempo.usPerQuarter == 0) {
            continue;
        }
        info.tempo_map.push_back({
            static_cast<double>(tempo.absolute_tick) / tpq,
            static_cast<double>(tempo.usPerQuarter) / 1.0e6
        });
    }

    // A note-on opens a note, the next note-off of that key and channel (or note-on with
    // velocity 0) closes it; notes still open at the end last a beat.
    std::vector<std::unique_ptr<NoteDrawInstrument>> instruments(static_cast<size_t>(tracks));
    std::vector<MidiSendFunc> callbacks(song.GetTrackCount());
    for (int t = 0; t < tracks; ++t) {
        instruments[t] = std::make_unique<NoteDrawInstrument>(info.notes[t], song.m_midi_state);
        NoteDrawInstrument* instrument = instruments[t].get();
        callbacks[t] = [instrument](uint8_t* data, size_t size) {
            instrument->send_midi(data, size);
        };
    }
    song.Reset();
    while (song.step(callbacks.data()) != NO_EVENTS) {
    }
    info.numerator = song.m_midi_state.m_time_signature_num;
    info.denominator = song.m_midi_state.m_time_signature_den;
    for (int t = 0; t < tracks; ++t) {
        info.track_names[t] = song.GetTrack(static_cast<uint32_t>(t))->get_track_name();
        instruments[t]->finish();
    }
    song.Reset();
    return info;
}
