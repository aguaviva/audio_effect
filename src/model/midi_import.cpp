#include "midi_import.hpp"

#include "instrument.h"
#include "midi.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <utility>

namespace {

// Calls meta(tick, type, data, length) for each meta event in a track chunk's events.
template <typename F>
void for_each_meta(const uint8_t* p, const uint8_t* end, F meta) {
    auto read_var = [&](uint32_t& out) {
        out = 0;
        for (int i = 0; i < 4 && p < end; ++i) {
            const uint8_t byte = *p++;
            out = (out << 7) | (byte & 0x7f);
            if (!(byte & 0x80)) {
                return true;
            }
        }
        return false;
    };
    uint32_t tick = 0;
    uint8_t running = 0;
    while (p < end) {
        uint32_t delta = 0;
        if (!read_var(delta) || p >= end) {
            return;
        }
        tick += delta;
        const uint8_t status = *p;
        if (status == 0xFF || status == 0xF0 || status == 0xF7) {
            ++p;
            uint8_t type = 0;
            if (status == 0xFF) {
                if (p >= end) {
                    return;
                }
                type = *p++;
            }
            uint32_t length = 0;
            if (!read_var(length) || length > static_cast<size_t>(end - p)) {
                return;
            }
            if (status == 0xFF) {
                meta(tick, type, p, length);
            }
            p += length;
            continue;
        }
        if (status & 0x80) {
            running = status;
            ++p;
        } else if (!running) {
            return;
        }
        const int type = running >> 4;
        p += (type == 0xC || type == 0xD) ? 1 : 2;
    }
}

std::string printable(const uint8_t* data, uint32_t length) {
    std::string out;
    for (uint32_t i = 0; i < length; ++i) {
        if (data[i] >= 0x20 && data[i] != 0x7f) {
            out += static_cast<char>(data[i]);
        }
    }
    const size_t first = out.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : out.substr(first, out.find_last_not_of(' ') - first + 1);
}

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

    bool have_signature = false;
    for (uint32_t t = 0; t < song.GetTrackCount(); t++) {
        const MidiTrack* mt = song.GetTrack(t);
        std::string name;
        for_each_meta(mt->m_pTrackIni, mt->m_pTrackFin,
                      [&](uint32_t, uint8_t type, const uint8_t* data, uint32_t length) {
            if (type == 0x03 && name.empty()) {
                name = printable(data, length);
            } else if (type == 0x58 && length >= 2 && !have_signature) {
                info.numerator = data[0];
                info.denominator = 1 << std::min<int>(data[1], 5);
                have_signature = true;
            }
        });
        if (static_cast<int>(t) < tracks) {
            info.track_names[t] = name;
        }
    }
    // Use the tempo map already computed by SimpleMIDI's midi.cpp. It includes the
    // default 120 BPM tempo and the absolute ticks for all tempo meta events.
    for (const ::TempoChange& tempo : song.getTempoChanges()) {
        if (tempo.usPerQuarter == 0) {
            continue;
        }
        const uint32_t us_per_tick =
            std::max<uint32_t>(1, tempo.usPerQuarter / tpq);
        info.tempo_map.push_back({
            static_cast<double>(tempo.absolute_tick) / tpq,
            static_cast<double>(us_per_tick * tpq) / 1.0e6
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
    for (int t = 0; t < tracks; ++t) {
        instruments[t]->finish();
    }
    song.Reset();
    return info;
}
