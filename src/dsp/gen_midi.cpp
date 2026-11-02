#include "gen_midi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <type_traits>
#include "dsp/transport.hpp"
#include "imgui.h"
#include "ui/message_box.hpp"
#include "inst-piano.h"

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

void Midi::get_outputs(int* count, const char* const** str) const {
    static const char* names[] = {"track 1", "track 2", "track 3", "track 4", "track 5", "track 6", "track 7", "track 8", "track 9", "track 10"};
    static_assert(std::size(names) == kTracks, "one name per track");
    *count = kTracks;
    *str = names;
}

bool Midi::set_path(const std::string& path) {
    path_ = path;
    error_.clear();

    if (!load_file(path.c_str(), &pMidi_buffer, &midi_size)) {
        error_ = "Could not open the file";
        return false;
    }
    midi_song.Reset();
    midi_song.LoadMidi(pMidi_buffer, midi_size);

    const uint32_t tracks = std::min<uint32_t>(kTracks, midi_song.GetTrackCount());
    for (uint32_t t = 0; t < tracks; t++) {
        if (!instruments_[t]) {
            instruments_[t] = GetNewPiano();
        }
        Instrument* inst = instruments_[t];
        midi_song.pFunc[t] = [inst](uint8_t* pdata, size_t size) { inst->send_midi(pdata, size); };
    }

    // The sequencer keeps neither track names nor a song-wide time signature and tempo map,
    // so read them from the events. The first time signature, usually the first track's,
    // applies to every track.
    const uint32_t tpq = std::max<uint32_t>(1, midi_song.m_midi_state.m_ticksPerQuarterNote);
    start_us_per_tick_ = (60 * 1000000 / 110) / tpq;
    int numerator = 4;
    int denominator = 4;
    bool have_signature = false;
    std::vector<std::pair<uint32_t, uint32_t>> tempos; // tick, microseconds per quarter note
    for (uint32_t t = 0; t < midi_song.GetTrackCount(); t++) {
        const MidiTrack* mt = midi_song.GetTrack(t);
        std::string name;
        for_each_meta(mt->m_pTrackIni, mt->m_pTrackFin,
                      [&](uint32_t tick, uint8_t type, const uint8_t* data, uint32_t length) {
            if (type == 0x03 && name.empty()) {
                name = printable(data, length);
            } else if (type == 0x58 && length >= 2 && !have_signature) {
                numerator = data[0];
                denominator = 1 << std::min<int>(data[1], 5);
                have_signature = true;
            } else if (type == 0x51 && length >= 3) {
                tempos.emplace_back(tick, (data[0] << 16) | (data[1] << 8) | data[2]);
            }
        });
        if (t < static_cast<uint32_t>(kTracks)) {
            track_names_[t] = name;
        }
    }
    std::stable_sort(tempos.begin(), tempos.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    // Beats last as long as the sequencer plays them: it rounds tempo down to whole
    // microseconds per tick, and plays at 110 bpm until a file sets a tempo.
    auto seconds_per_beat = [tpq](uint32_t us_per_quarter) {
        return static_cast<float>(std::max<uint32_t>(1, us_per_quarter / tpq) * tpq / 1.0e6);
    };
    std::vector<audio_effect::TempoChange> tempo_map;
    if (tempos.empty() || tempos.front().first > 0) {
        tempo_map.push_back({0.0f, seconds_per_beat(60000000 / 110)});
    }
    for (const auto& [tick, us_per_quarter] : tempos) {
        tempo_map.push_back({static_cast<float>(tick) / tpq, seconds_per_beat(us_per_quarter)});
    }

    // Notes for the piano rolls: a note-on opens a note, the next note-off of that key and
    // channel (or note-on with velocity 0) closes it.
    const float ticks_per_beat = static_cast<float>(tpq);
    for (int t = 0; t < kTracks; t++) {
        piano_rolls_[t].ClearNotes();
        piano_rolls_[t].SetTimeSignature(numerator, denominator);
        piano_rolls_[t].SetTempoMap(tempo_map);
        if (t >= static_cast<int>(tracks)) {
            track_names_[t].clear();
            piano_roll_open_[t] = false;
        }
    }
    for (uint32_t t = 0; t < tracks; t++) {
        MidiTrack* mt = midi_song.GetTrack(t);
        audio_effect::PianoRollEditor& roll = piano_rolls_[t];
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
                    roll.AddNote(key, start / ticks_per_beat, (now - start) / ticks_per_beat,
                                 on_velocity[channel][key]);
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
                    roll.AddNote(key, on_tick[channel][key] / ticks_per_beat, 1.0f,
                                 on_velocity[channel][key]);
                }
            }
        }
        roll.FrameNotes();
    }
    rewind();
    return true;
}

double Midi::playhead_beat() const {
    const MidiState& state = midi_song.m_midi_state;
    double tick = finished_ ? state.m_time : interval_start_tick_;
    if (!finished_ && state.m_microSecondsPerMidiTick > 0) {
        tick += static_cast<double>(interval_samples_) * 1.0e6 /
                (static_cast<double>(kSampleRate) * state.m_microSecondsPerMidiTick);
    }
    tick = std::min<double>(tick, state.m_time);
    return tick / std::max<uint32_t>(1, state.m_ticksPerQuarterNote);
}

void Midi::rewind() {
    midi_song.Reset();
    // Reset() leaves the tempo of the last tempo event played.
    if (start_us_per_tick_ > 0) {
        midi_song.m_midi_state.m_microSecondsPerMidiTick = start_us_per_tick_;
    }
    for (Instrument* inst : instruments_) {
        if (inst) {
            inst->reset();
        }
    }
    interval_start_tick_ = 0;
    interval_samples_ = 0;
    played_samples_ = 0;
    finished_ = false;
}

void Midi::transport() {
    transport_controls();
    if (finished_) {
        ImGui::SameLine();
        ImGui::TextDisabled("(end)");
    }
}

// Rewinds when going back, then runs the sequencer to target with its notes muted.
void Midi::seek(uint64_t target) {
    if (target < played_samples_) {
        rewind();
    }
    constexpr size_t kSendFuncs = std::extent_v<decltype(MidiSong::pFunc)>;
    const uint32_t tracks = std::min<uint32_t>(midi_song.GetTrackCount(), kSendFuncs);
    std::array<MidiSendFunc, kSendFuncs> muted;
    for (uint32_t t = 0; t < tracks; ++t) {
        std::swap(midi_song.pFunc[t], muted[t]);
    }
    while (played_samples_ < target && !finished_) {
        step(target - played_samples_);
    }
    for (uint32_t t = 0; t < tracks; ++t) {
        std::swap(midi_song.pFunc[t], muted[t]);
    }
    for (Instrument* inst : instruments_) {
        if (inst) {
            inst->reset();
        }
    }
}

size_t Midi::step(size_t samples) {
    const uint32_t tick = midi_song.m_midi_state.m_time;
    const size_t n = midi_song.RenderMidi(kSampleRate, samples);
    if (n == NO_EVENTS) {
        finished_ = true;
        return 0;
    }
    if (midi_song.m_midi_state.m_time != tick) {
        interval_start_tick_ = tick;
        interval_samples_ = 0;
    }
    interval_samples_ += n;
    played_samples_ += n;
    return n;
}

std::string Midi::save() const {
    std::string out = "{";
    module_json::write_string(out, "path", path_);
    out += '}';
    return out;
}

bool Midi::load(const std::string& json) {
    std::string path = path_;
    module_json::read(json, "path", path);
    if (!path.empty()) {
        set_path(path);
    }
    return true;
}

bool Midi::render() {
    bool changed = false;
    const std::string picked = picker_.poll();
    if (!picked.empty()) {        
        if (!set_path(picked)) {
            show_error("Could not load " + picked + ":\n" + error_);
        }
    }

    const char* slash = std::strrchr(path_.c_str(), '/');
    const char* leaf = slash ? slash + 1 : path_.c_str();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float button_width = ImGui::CalcTextSize("...").x + style.FramePadding.x * 2.0f;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                           button_width - style.ItemSpacing.x);
    ImGui::TextUnformatted(leaf[0] ? leaf : "(no file)");
    ImGui::PopTextWrapPos();
    if (ImGui::IsItemHovered() && !path_.empty()) {
        ImGui::SetTooltip("%s", path_.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("...")) {
        picker_.open(path_);
    }
    if (!error_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", error_.c_str());
        ImGui::PopTextWrapPos();
    }
    transport();
    // One button per track; the node is too narrow for more.
    for (int t = 0; t < loaded_tracks(); ++t) {
        roll_button(t);
    }
    return changed;
}

int Midi::loaded_tracks() {
    return std::min<int>(kTracks, static_cast<int>(midi_song.GetTrackCount()));
}

// Lit while the track's piano roll is open.
void Midi::roll_button(int track) {
    ImGui::PushID(track);
    const std::string& name = track_names_[track];
    char label[96];
    std::snprintf(label, sizeof(label), "%d %s###roll", track + 1,
                  name.empty() ? "Track" : name.c_str());
    const bool open = piano_roll_open_[track];
    if (open) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    }
    if (ImGui::SmallButton(label)) {
        piano_roll_open_[track] = !open;
    }
    if (open) {
        ImGui::PopStyleColor();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Track %d%s%s\nClick to %s its piano roll", track + 1,
                          name.empty() ? "" : ": ", name.c_str(), open ? "hide" : "show");
    }
    ImGui::PopID();
}

bool Midi::render_timeline(Timeline& timeline, const char* title) {
    const int tracks = loaded_tracks();
    timeline.BeginTrack("title", Timeline::TitleHeight());
    ImGui::TextUnformatted(title);
    if (ImGui::IsItemHovered() && !path_.empty()) {
        ImGui::SetTooltip("%s", path_.c_str());
    }
    if (tracks == 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("(no song)");
    }
    timeline.EndTrack();

    // Every roll has the song's tempo map.
    double song_end = 0.0;
    for (int t = 0; t < tracks; ++t) {
        const audio_effect::PianoRollEditor& roll = piano_rolls_[t];
        for (const audio_effect::PianoRollNote& n : roll.GetNotes()) {
            song_end = std::max<double>(song_end, roll.SecondsAtBeat(n.startBeat + n.duration));
        }
    }
    for (int t = 0; t < tracks; ++t) {
        const audio_effect::PianoRollEditor& roll = piano_rolls_[t];
        char id[16];
        std::snprintf(id, sizeof(id), "track%d", t);
        timeline.BeginTrack(id);
        roll_button(t);
        ImGui::TextDisabled("%zu notes", roll.GetNotes().size());
        timeline_notes_.clear();
        for (const audio_effect::PianoRollNote& n : roll.GetNotes()) {
            const double start = roll.SecondsAtBeat(n.startBeat);
            timeline_notes_.push_back(
                {start, roll.SecondsAtBeat(n.startBeat + n.duration) - start, n.pitch});
        }
        timeline.Notes(timeline_notes_, 0.0, song_end);
        timeline.EndTrack();
    }
    return true;
}

void Midi::render_window() {
    for (int channel = 0; channel < kTracks; ++channel) {
        if (!piano_roll_open_[channel]) {
            continue;
        }

        const std::string& name = track_names_[channel];
        char title[192];
        std::snprintf(title, sizeof(title), "Piano Roll - Track %d%s%s###midi_piano_%p_%d",
                      channel + 1, name.empty() ? "" : ": ", name.c_str(),
                      static_cast<void*>(this), channel);
        ImGui::SetNextWindowSize(ImVec2(1000.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &piano_roll_open_[channel])) {
            transport();
            ImGui::SameLine();
            ImGui::Checkbox("Follow playhead", &follow_playhead_);
            piano_rolls_[channel].SetPlayhead(piano_rolls_[channel].BeatAtSeconds(
                static_cast<float>(::transport().position()) / kSampleRate));
            piano_rolls_[channel].SetFollowPlayhead(follow_playhead_);
            piano_rolls_[channel].Render("##piano_roll", ImVec2(0.0f, 520.0f));
        }
        ImGui::End();
    }
}

void Midi::process(const float* const* /*inputs*/, int /*input_count*/,
                   size_t sample_count, float /*sample_rate*/) {
    const uint32_t tracks = std::min<uint32_t>(kTracks, midi_song.GetTrackCount());
    for (int t = 0; t < kTracks; t++) {
        if (output_buffer(t)) {
            std::fill(output_buffer(t), output_buffer(t) + sample_count, 0.0f);
        }
    }
    if (!::transport().block_playing()) {
        return;
    }
    // In step with the transport unless it moved (or this module missed a block): then seek.
    const uint64_t target = static_cast<uint64_t>(::transport().block_frame());
    if (target != played_samples_ && !(finished_ && target > played_samples_)) {
        seek(target);
    }
    size_t offset = 0;
    size_t size = sample_count / 2;
    while (size > 0 && !finished_) {
        const size_t n = step(size);
        for (uint32_t t = 0; t < tracks; t++) {
            float* out = output_buffer(t);
            if (midi_song.GetTrack(t) != NULL && out) {
                instruments_[t]->render_samples(n, out + 2 * offset);
            }
        }
        size -= n;
        offset += n;
    }
}
