#include "gen_midi.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include "dsp/transport.hpp"
#include "imgui.h"
#include "model/midi_import.hpp"
#include "model/tempo_grid.hpp"
#include "ui/message_box.hpp"
#include "ui/timeline.hpp"
#include "ui/timeline_group.hpp"
#include "ui/timeline_note_lane.hpp"
#include "ui/transport_controls.hpp"
#include "inst-piano.h"

void Midi::get_outputs(int* count, const char* const** str) const {
    static const char* names[] = {"track 1", "track 2", "track 3", "track 4", "track 5", "track 6", "track 7", "track 8", "track 9", "track 10"};
    static_assert(std::size(names) == kTracks, "one name per track");
    *count = kTracks;
    *str = names;
}

double Midi::playhead_seconds() const {
    return static_cast<double>(played_frames_.load(std::memory_order_relaxed)) / kSampleRate;
}

bool Midi::set_path(const std::string& path) {
    path_ = path;
    error_.clear();
    const uint32_t edit_generation = ++midi_edit_generation_;

    auto sequencer = std::make_unique<MidiSequencer>();
    MidiSongInfo info;
    {
        // Instruments for every track the file can have, before the sequencer binds them.
        for (Instrument*& inst : instruments_) {
            if (!inst) {
                inst = GetNewPiano();
            }
        }
        if (!sequencer->load(path, instruments_, midi_edits_, edit_generation, error_)) {
            return false;
        }
        info = read_midi_song(sequencer->song(), kTracks);
        sequencer->rewind_song();
    }
    track_count_ = sequencer->track_count();

    TempoGrid& grid = song_grid();
    grid.set_time_signature(info.numerator, info.denominator);
    grid.set_tempo_map(info.tempo_map);

    // The rolls keep the file's tempo map, as the sequencer plays it, even when the song grid's
    // tempo is edited afterwards; their bars and snapping follow the grid.
    for (int t = 0; t < kTracks; t++) {
        audio_effect::PianoRollEditor& roll = piano_rolls_[t];
        roll.SetNoteEditCallback({});
        roll.ClearNotes();
        roll.SetTimeSignature(info.numerator, info.denominator);
        roll.SetTempoMap(info.tempo_map);
        roll.SetGridDivision(grid.division());
        if (t < track_count_) {
            track_names_[t] = info.track_names[t];
            for (const ImportedNote& n : info.notes[t]) {
                roll.AddNote(n.pitch, static_cast<float>(n.start), static_cast<float>(n.duration),
                             n.velocity, n.channel, n.end_status, n.end_velocity);
            }
            const uint32_t tpq =
                std::max<uint32_t>(1, sequencer->song().m_midi_state.m_ticksPerQuarterNote);
            roll.SetNoteEditCallback([this, t, tpq, edit_generation](
                                         const audio_effect::PianoRollNote& note, bool insert) {
                const double start = std::max(0.0, static_cast<double>(note.startBeat));
                const double duration = std::max(1.0 / tpq, static_cast<double>(note.duration));
                const bool queued = midi_edits_.push({
                    insert, edit_generation, static_cast<uint8_t>(t),
                    static_cast<uint8_t>(note.pitch),
                    static_cast<uint8_t>(note.velocity), static_cast<uint8_t>(note.channel),
                    static_cast<uint8_t>(note.end_status),
                    static_cast<uint8_t>(note.end_velocity),
                    static_cast<uint32_t>(std::llround(start * tpq)),
                    static_cast<uint32_t>(
                        std::max<long long>(1, std::llround(duration * tpq)))});
                if (!queued) {
                    show_error("The MIDI edit queue is full; the edit was not saved.");
                }
            });
            roll.FrameNotes();
        } else {
            track_names_[t].clear();
            piano_roll_open_[t] = false;
        }
    }

    sequencer_.publish(std::move(sequencer));
    playhead_beat_.store(0.0, std::memory_order_relaxed);
    played_frames_.store(0, std::memory_order_relaxed);
    finished_.store(false, std::memory_order_relaxed);
    return true;
}

void Midi::transport_row() {
    transport_controls();
    if (finished()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(end)");
    }
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
    sequencer_.collect();
    if (midi_edit_failed_.exchange(false, std::memory_order_relaxed)) {
        show_error("A piano-roll edit could not be applied to the MIDI track.");
    }
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
    transport_row();
    // One button per track; the node is too narrow for more.
    for (int t = 0; t < track_count_; ++t) {
        roll_button(t);
    }
    return changed;
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
    timeline_title_row(timeline, title, path_.c_str(), track_count_ == 0 ? "(no song)" : nullptr);

    // Every roll has the song's tempo map.
    double song_end = 0.0;
    for (int t = 0; t < track_count_; ++t) {
        const audio_effect::PianoRollEditor& roll = piano_rolls_[t];
        for (const audio_effect::PianoRollNote& n : roll.GetNotes()) {
            song_end = std::max<double>(song_end, roll.SecondsAtBeat(n.startBeat + n.duration));
        }
    }
    for (int t = 0; t < track_count_; ++t) {
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
        draw_notes(timeline, timeline_notes_, 0.0, song_end);
        timeline.EndTrack();
    }
    return true;
}

void Midi::render_window() {
    sequencer_.collect();
    const TempoGrid& grid = song_grid();
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
            transport_row();
            ImGui::SameLine();
            ImGui::Checkbox("Follow playhead", &follow_playhead_);
            audio_effect::PianoRollEditor& roll = piano_rolls_[channel];
            roll.SetTimeSignature(grid.numerator(), grid.denominator());
            roll.SetGridDivision(grid.division());
            roll.SetPlayheadSeconds(static_cast<float>(transport().position()) / kSampleRate);
            roll.SetFollowPlayhead(follow_playhead_);
            roll.Render("##piano_roll", ImVec2(0.0f, 520.0f));
        }
        ImGui::End();
    }
}

void Midi::process(const float* const* /*inputs*/, int /*input_count*/,
                   size_t sample_count, float /*sample_rate*/) {
    std::array<float*, kTracks> outputs;
    for (int t = 0; t < kTracks; t++) {
        outputs[t] = output_buffer(t);
    }
    MidiSequencer* sequencer = sequencer_.acquire();
    if (sequencer) {
        sequencer->apply_pending_edits();
        if (sequencer->take_edit_error()) {
            midi_edit_failed_.store(true, std::memory_order_relaxed);
        }
    }
    if (!sequencer || !transport().block_playing()) {
        for (float* out : outputs) {
            if (out) {
                std::fill(out, out + sample_count, 0.0f);
            }
        }
        return;
    }
    sequencer->render(outputs.data(), kTracks, static_cast<uint64_t>(transport().block_frame()),
                      sample_count / 2);
    playhead_beat_.store(sequencer->playhead_beat(), std::memory_order_relaxed);
    played_frames_.store(sequencer->played_frames(), std::memory_order_relaxed);
    finished_.store(sequencer->finished(), std::memory_order_relaxed);
}
