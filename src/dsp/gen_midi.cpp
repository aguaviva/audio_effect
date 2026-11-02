#include "gen_midi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include "imgui.h"
#include "ui/message_box.hpp"
#include "inst-piano.h"

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

    // Notes for the piano rolls: a note-on opens a note, the next note-off of that key and
    // channel (or note-on with velocity 0) closes it.
    const float ticks_per_beat =
        static_cast<float>(std::max<uint32_t>(1, midi_song.m_midi_state.m_ticksPerQuarterNote));
    for (int t = 0; t < kTracks; t++) {
        piano_rolls_[t].ClearNotes();
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

// Play/Pause keeps the place; Stop goes back to the start. Play after the end starts over.
void Midi::transport() {
    const bool running = playing && !finished_;
    if (ImGui::Button(running ? "Pause###play" : "Play###play")) {
        if (finished_) {
            rewind();
        }
        playing = !running;
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop###stop")) {
        playing = false;
        rewind();
    }
    ImGui::SameLine();
    ImGui::Text("%.2f s%s", playhead_seconds(), finished_ ? " (end)" : "");
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
    for (int channel = 0; channel < kTracks; ++channel) {
        ImGui::PushID(channel);
        ImGui::Text("Channel %d", channel + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton(piano_roll_open_[channel] ? "Hide Piano Roll" : "Open Piano Roll")) {
            piano_roll_open_[channel] = !piano_roll_open_[channel];
        }
        ImGui::PopID();
    }
    return changed;
}

void Midi::render_window() {
    for (int channel = 0; channel < kTracks; ++channel) {
        if (!piano_roll_open_[channel]) {
            continue;
        }

        char title[96];
        std::snprintf(title, sizeof(title), "Piano Roll - Channel %d###midi_piano_%p_%d",
                      channel + 1, static_cast<void*>(this), channel);
        ImGui::SetNextWindowSize(ImVec2(1000.0f, 600.0f), ImGuiCond_FirstUseEver);
        if (channel < static_cast<int>(midi_song.GetTrackCount())) {
            MidiTrack* track = midi_song.GetTrack(static_cast<uint32_t>(channel));
            if (track) {
                piano_rolls_[channel].SetTimeSignature(
                    static_cast<int>(track->m_timesignatureNum),
                    static_cast<int>(track->m_timesignatureDen));
            }
        }
        if (ImGui::Begin(title, &piano_roll_open_[channel])) {
            transport();
            ImGui::SameLine();
            ImGui::Checkbox("Follow playhead", &follow_playhead_);
            piano_rolls_[channel].SetPlayhead(static_cast<float>(playhead_beat()));
            piano_rolls_[channel].SetFollowPlayhead(follow_playhead_);
            piano_rolls_[channel].Render("##piano_roll", ImVec2(0.0f, 520.0f));
        }
        ImGui::End();
    }
}

void Midi::process(const float* const* /*inputs*/, int /*input_count*/,
                       size_t sample_count, float /*sample_rate*/) {

    const uint32_t tracks = std::min<uint32_t>(kTracks, midi_song.GetTrackCount());

    for (int t = 0; t < kTracks; t++)
    {
        if (output_buffer(t))
            std::fill(output_buffer(t), output_buffer(t) + sample_count, 0.0f);
    }                        
    if (!playing || finished_) {
        return;
    }

    size_t size = sample_count/2;
    uint32_t offset = 0;
    while(size>0)
    {
        const uint32_t tick = midi_song.m_midi_state.m_time;
        size_t n_samples = midi_song.RenderMidi(kSampleRate, size);
        if (n_samples==NO_EVENTS) {
            finished_ = true;
            return;
        }
        if (midi_song.m_midi_state.m_time != tick) {
            interval_start_tick_ = tick;
            interval_samples_ = 0;
        }
        interval_samples_ += n_samples;
        played_samples_ += n_samples;

        for (uint32_t t = 0; t < tracks; t++)
        {
            float *pOut = output_buffer(t);
            if (midi_song.GetTrack(t)!=NULL && pOut)
            {
                instruments_[t]->render_samples(n_samples, pOut + (2*offset));
            }
        }                    
        size -= n_samples;                
        offset += n_samples;
    }
}
