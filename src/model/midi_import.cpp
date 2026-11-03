#include "midi_import.hpp"

#include "midi.h"

#include <algorithm>
#include <array>
#include <cstdint>
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

} // namespace

MidiSongInfo read_midi_song(MidiSong& song, int max_tracks) {
    MidiSongInfo info;
    const int tracks = std::min<int>(max_tracks, static_cast<int>(song.GetTrackCount()));
    const uint32_t tpq = std::max<uint32_t>(1, song.m_midi_state.m_ticksPerQuarterNote);
    info.track_names.resize(static_cast<size_t>(tracks));
    info.notes.resize(static_cast<size_t>(tracks));

    bool have_signature = false;
    std::vector<std::pair<uint32_t, uint32_t>> tempos; // tick, microseconds per quarter note
    for (uint32_t t = 0; t < song.GetTrackCount(); t++) {
        const MidiTrack* mt = song.GetTrack(t);
        std::string name;
        for_each_meta(mt->m_pTrackIni, mt->m_pTrackFin,
                      [&](uint32_t tick, uint8_t type, const uint8_t* data, uint32_t length) {
            if (type == 0x03 && name.empty()) {
                name = printable(data, length);
            } else if (type == 0x58 && length >= 2 && !have_signature) {
                info.numerator = data[0];
                info.denominator = 1 << std::min<int>(data[1], 5);
                have_signature = true;
            } else if (type == 0x51 && length >= 3) {
                tempos.emplace_back(tick, (data[0] << 16) | (data[1] << 8) | data[2]);
            }
        });
        if (static_cast<int>(t) < tracks) {
            info.track_names[t] = name;
        }
    }
    std::stable_sort(tempos.begin(), tempos.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    // Beats last as long as the sequencer plays them: it rounds tempo down to whole
    // microseconds per tick, and plays at 110 bpm until a file sets a tempo.
    auto seconds_per_beat = [tpq](uint32_t us_per_quarter) {
        return static_cast<double>(std::max<uint32_t>(1, us_per_quarter / tpq) * tpq) / 1.0e6;
    };
    if (tempos.empty() || tempos.front().first > 0) {
        info.tempo_map.push_back({0.0, seconds_per_beat(60000000 / 110)});
    }
    for (const auto& [tick, us_per_quarter] : tempos) {
        info.tempo_map.push_back({static_cast<double>(tick) / tpq, seconds_per_beat(us_per_quarter)});
    }

    // A note-on opens a note, the next note-off of that key and channel (or note-on with
    // velocity 0) closes it; notes still open at the end last a beat.
    const double ticks_per_beat = static_cast<double>(tpq);
    for (int t = 0; t < tracks; t++) {
        MidiTrack* mt = song.GetTrack(t);
        std::vector<ImportedNote>& notes = info.notes[t];
        std::array<std::array<int64_t, 128>, 16> on_tick;
        std::array<std::array<int, 128>, 16> on_velocity{};
        for (auto& channel : on_tick) {
            channel.fill(-1);
        }
        uint32_t time = 0;
        do {
            const uint32_t now = time;
            time = mt->play(now, [&](uint8_t* pdata, size_t size) {
                const int type = pdata[0] >> 4;
                if (size < 3 || (type != 0x8 && type != 0x9)) {
                    return;
                }
                const int channel = pdata[0] & 0xf;
                const int key = pdata[1] & 0x7f;
                int64_t& start = on_tick[channel][key];
                if (start >= 0) {
                    notes.push_back({key, start / ticks_per_beat, (now - start) / ticks_per_beat,
                                     on_velocity[channel][key]});
                    start = -1;
                }
                if (type == 0x9 && pdata[2] > 0) {
                    start = now;
                    on_velocity[channel][key] = pdata[2];
                }
            });
        } while (time != NO_EVENTS);
        for (int channel = 0; channel < 16; ++channel) {
            for (int key = 0; key < 128; ++key) {
                if (on_tick[channel][key] >= 0) {
                    notes.push_back({key, on_tick[channel][key] / ticks_per_beat, 1.0,
                                     on_velocity[channel][key]});
                }
            }
        }
    }
    return info;
}
