#pragma once

#include "imgui.h"

#include <functional>
#include <vector>

namespace audio_effect {

struct Note {
    int id = 0;
    int pitch = 60;
    float startBeat = 0.0f;
    float duration = 1.0f;
    int velocity = 100;
    bool selected = false;
};

using PianoRollNote = Note;

class PianoRollEditor {
public:
    int AddNote(int pitch, float startBeat, float duration, int velocity = 100);
    const std::vector<PianoRollNote>& GetNotes() const { return notes_; }
    void ClearNotes();

    // Beat being played, drawn as a line over the grid; negative hides it.
    void SetPlayhead(float beat) { playhead_beat_ = beat; }
    float GetPlayhead() const { return playhead_beat_; }
    // While the playhead moves, scroll so it stays in view.
    void SetFollowPlayhead(bool follow) { follow_playhead_ = follow; }

    void SetPitchCallback(std::function<void(int, bool)> callback);
    void DeleteNote(int noteId);
    void ClearSelection();
    void DeleteSelectedNotes();

    void SetTempo(float bpm);
    float GetTempo() const { return bpm_; }
    void SetTimeSignature(int numerator, int denominator);
    void SetGridDivision(int divisionsPerBeat);
    void SetPitchRange(int low, int high);

    void Render(const char* id = "Piano Roll", ImVec2 size = ImVec2(0.0f, 320.0f));

private:
    float MaxBeat() const;
    float BeatAt(float x, const ImVec2& origin) const;
    int PitchAt(float y, const ImVec2& origin) const;
    float Snap(float beat) const;
    PianoRollNote* FindNote(int id);
    PianoRollNote* HitNote(float x, float y, const ImVec2& origin);

    void HandleKeyboard(const ImVec2& origin, bool hovered);
    void HandleNavigation(const ImVec2& origin, const ImVec2& size, bool hovered);
    void FollowPlayhead(float width);
    void HandleEditing(const ImVec2& origin, const ImVec2& size, bool hovered);
    void DrawKeys(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const;
    void DrawGrid(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const;

    std::vector<PianoRollNote> notes_;
    std::function<void(int, bool)> pitch_callback_;
    int next_id_ = 1;
    int low_pitch_ = 24;
    int high_pitch_ = 108;
    int drag_id_ = -1;
    int pressed_pitch_ = -1;
    float bpm_ = 120.0f;
    float beats_per_bar_ = 4.0f;
    float grid_step_ = 0.25f;
    float pixels_per_beat_ = 80.0f;
    float row_height_ = 18.0f;
    float scroll_x_ = 0.0f;
    float scroll_y_ = 0.0f;
    float playhead_beat_ = -1.0f;
    float followed_beat_ = -1.0f;
    bool follow_playhead_ = true;
    float grab_beat_ = 0.0f;
    int grab_pitch_ = 0;
    bool resize_drag_ = false;
    bool dragged_ = false;
    bool marquee_ = false;
    bool panning_ = false;
    ImVec2 marquee_start_ = ImVec2(0.0f, 0.0f);
    ImVec2 drag_start_mouse_ = ImVec2(0.0f, 0.0f);
    ImVec2 pan_mouse_ = ImVec2(0.0f, 0.0f);
    float pan_scroll_x_ = 0.0f;
    float pan_scroll_y_ = 0.0f;
};

} // namespace audio_effect
