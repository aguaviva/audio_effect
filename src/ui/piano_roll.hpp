#pragma once

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>
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
    int AddNote(int pitch, float startBeat, float duration, int velocity = 100) {
        const int id = next_id_++;
        notes_.push_back({id, std::clamp(pitch, 0, 127), std::max(0.0f, startBeat),
                          std::max(grid_step_, duration), std::clamp(velocity, 1, 127), false});
        return id;
    }

    const std::vector<PianoRollNote>& GetNotes() const { return notes_; }

    void SetPitchCallback(std::function<void(int, bool)> callback) {
        pitch_callback_ = std::move(callback);
    }

    void DeleteNote(int noteId) {
        notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                                    [noteId](const PianoRollNote& note) { return note.id == noteId; }),
                     notes_.end());
        if (drag_id_ == noteId) {
            drag_id_ = -1;
        }
    }

    void ClearSelection() {
        for (PianoRollNote& note : notes_) {
            note.selected = false;
        }
    }

    void DeleteSelectedNotes() {
        notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                                    [](const PianoRollNote& note) { return note.selected; }),
                     notes_.end());
        drag_id_ = -1;
    }

    void SetTempo(float bpm) { bpm_ = std::clamp(bpm, 1.0f, 999.0f); }
    float GetTempo() const { return bpm_; }

    void SetTimeSignature(int numerator, int denominator) {
        beats_per_bar_ = std::clamp(numerator, 1, 32) * 4.0f /
                         static_cast<float>(std::clamp(denominator, 1, 32));
    }

    void SetGridDivision(int divisionsPerBeat) {
        grid_step_ = 1.0f / static_cast<float>(std::clamp(divisionsPerBeat, 1, 64));
    }

    void SetPitchRange(int low, int high) {
        low_pitch_ = std::clamp(low, 0, 127);
        high_pitch_ = std::clamp(high, 0, 127);
        if (low_pitch_ > high_pitch_) {
            std::swap(low_pitch_, high_pitch_);
        }
        scroll_y_ = std::clamp(scroll_y_, 0.0f,
                               std::max(0.0f, (high_pitch_ - low_pitch_ + 1) * row_height_ - 1.0f));
    }

    void Render(const char* id = "Piano Roll", ImVec2 size = ImVec2(0.0f, 320.0f)) {
        ImGui::PushID(this);
        if (id && id[0]) {
            ImGui::PushID(id);
        }

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float height = size.y > 0.0f ? size.y : std::max(120.0f, available.y);
        const float key_width = std::clamp(size.x > 0.0f ? size.x * 0.14f : 58.0f, 48.0f, 96.0f);
        ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoScrollWithMouse |
                                       ImGuiWindowFlags_NoScrollbar;

        ImGui::BeginChild("##piano_keys", ImVec2(key_width, height), ImGuiChildFlags_Borders,
                          child_flags);
        const ImVec2 key_origin = ImGui::GetCursorScreenPos();
        const ImVec2 key_size = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##key_surface", key_size);
        const bool key_hovered = ImGui::IsItemHovered();
        HandleKeyboard(key_origin, key_hovered);
        DrawKeys(ImGui::GetWindowDrawList(), key_origin, key_size);
        ImGui::EndChild();

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::BeginChild("##piano_grid", ImVec2(0.0f, height), ImGuiChildFlags_Borders,
                          child_flags);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##grid_surface", canvas_size);
        const bool canvas_hovered = ImGui::IsItemHovered();
        HandleNavigation(origin, canvas_size, key_hovered || canvas_hovered);
        HandleEditing(origin, canvas_size, canvas_hovered);
        DrawGrid(ImGui::GetWindowDrawList(), origin, canvas_size);
        ImGui::EndChild();

        if (id && id[0]) {
            ImGui::PopID();
        }
        ImGui::PopID();
    }

private:
    static float DistanceSquared(const ImVec2& a, const ImVec2& b) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        return dx * dx + dy * dy;
    }

    static bool IsBlackKey(int pitch) {
        const int semitone = pitch % 12;
        return semitone == 1 || semitone == 3 || semitone == 6 ||
               semitone == 8 || semitone == 10;
    }

    float MaxBeat() const {
        float end = 64.0f;
        for (const PianoRollNote& note : notes_) {
            end = std::max(end, note.startBeat + note.duration + 16.0f);
        }
        return end;
    }

    float BeatAt(float x, const ImVec2& origin) const {
        return (x - origin.x + scroll_x_) / pixels_per_beat_;
    }

    int PitchAt(float y, const ImVec2& origin) const {
        const int row = static_cast<int>(std::floor((y - origin.y + scroll_y_) / row_height_));
        return high_pitch_ - row;
    }

    float Snap(float beat) const {
        return std::max(0.0f, std::round(beat / grid_step_) * grid_step_);
    }

    PianoRollNote* FindNote(int id) {
        const auto found = std::find_if(notes_.begin(), notes_.end(),
                                        [id](const PianoRollNote& note) { return note.id == id; });
        return found == notes_.end() ? nullptr : &*found;
    }

    PianoRollNote* HitNote(float x, float y, const ImVec2& origin) {
        for (auto note = notes_.rbegin(); note != notes_.rend(); ++note) {
            const float left = origin.x + note->startBeat * pixels_per_beat_ - scroll_x_;
            const float right = left + note->duration * pixels_per_beat_;
            const float top = origin.y + (high_pitch_ - note->pitch) * row_height_ - scroll_y_;
            if (x >= left && x <= right && y >= top && y <= top + row_height_) {
                return &*note;
            }
        }
        return nullptr;
    }

    void HandleKeyboard(const ImVec2& origin, bool hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const int pitch = PitchAt(io.MousePos.y, origin);
            if (pitch >= low_pitch_ && pitch <= high_pitch_) {
                pressed_pitch_ = pitch;
                if (pitch_callback_) {
                    pitch_callback_(pitch, true);
                }
            }
        }
        if (pressed_pitch_ >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (pitch_callback_) {
                pitch_callback_(pressed_pitch_, false);
            }
            pressed_pitch_ = -1;
        }
    }

    void HandleNavigation(const ImVec2& origin, const ImVec2& size, bool hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        if (hovered && io.MouseWheel != 0.0f) {
            if (io.KeyCtrl || io.KeySuper) {
                const float anchor_beat = BeatAt(io.MousePos.x, origin);
                pixels_per_beat_ = std::clamp(pixels_per_beat_ * std::pow(1.15f, io.MouseWheel),
                                              24.0f, 500.0f);
                scroll_x_ = std::max(0.0f, anchor_beat * pixels_per_beat_ -
                                               (io.MousePos.x - origin.x));
            } else if (io.KeyShift) {
                scroll_x_ = std::max(0.0f, scroll_x_ - io.MouseWheel * 48.0f);
            } else {
                scroll_y_ = std::clamp(scroll_y_ - io.MouseWheel * row_height_ * 3.0f, 0.0f,
                                       std::max(0.0f, (high_pitch_ - low_pitch_ + 1) * row_height_ - size.y));
            }
        }
        if (hovered && io.MouseWheelH != 0.0f) {
            scroll_x_ = std::max(0.0f, scroll_x_ - io.MouseWheelH * 48.0f);
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            panning_ = true;
            pan_mouse_ = io.MousePos;
            pan_scroll_x_ = scroll_x_;
            pan_scroll_y_ = scroll_y_;
        }
        if (panning_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                scroll_x_ = std::max(0.0f, pan_scroll_x_ - (io.MousePos.x - pan_mouse_.x));
                scroll_y_ = std::clamp(pan_scroll_y_ - (io.MousePos.y - pan_mouse_.y), 0.0f,
                                       std::max(0.0f, (high_pitch_ - low_pitch_ + 1) * row_height_ - size.y));
            } else {
                panning_ = false;
            }
        }
    }

    void HandleEditing(const ImVec2& origin, const ImVec2& size, bool hovered) {
        const ImGuiIO& io = ImGui::GetIO();
        if (hovered && !ImGui::GetIO().WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
            DeleteSelectedNotes();
        }

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const float mouse_x = io.MousePos.x;
            const float mouse_y = io.MousePos.y;
            PianoRollNote* note = HitNote(mouse_x, mouse_y, origin);
            const bool additive = io.KeyCtrl || io.KeySuper;
            if (note && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                DeleteNote(note->id);
                return;
            }

            if (note) {
                if (additive) {
                    note->selected = !note->selected;
                } else {
                    if (!note->selected) {
                        ClearSelection();
                        note->selected = true;
                    }
                }
                if (note->selected) {
                    drag_id_ = note->id;
                    resize_drag_ = mouse_x >= origin.x + (note->startBeat + note->duration) * pixels_per_beat_ -
                                                   scroll_x_ - 7.0f;
                    grab_beat_ = BeatAt(mouse_x, origin) - note->startBeat;
                    grab_pitch_ = note->pitch - PitchAt(mouse_y, origin);
                    drag_start_mouse_ = io.MousePos;
                    dragged_ = false;
                }
            } else if (mouse_y >= origin.y && mouse_y < origin.y + size.y) {
                marquee_ = true;
                marquee_start_ = io.MousePos;
                dragged_ = false;
                if (!additive) {
                    ClearSelection();
                }
            }
        }

        if (marquee_ && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (DistanceSquared(io.MousePos, marquee_start_) > 16.0f) {
                dragged_ = true;
            }
        }

        if (drag_id_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            PianoRollNote* note = FindNote(drag_id_);
            if (note) {
                if (DistanceSquared(io.MousePos, drag_start_mouse_) > 16.0f) {
                    dragged_ = true;
                }
                if (resize_drag_) {
                    note->duration = std::max(grid_step_, Snap(BeatAt(io.MousePos.x, origin) - note->startBeat));
                } else {
                    note->startBeat = Snap(BeatAt(io.MousePos.x, origin) - grab_beat_);
                    note->pitch = std::clamp(PitchAt(io.MousePos.y, origin) + grab_pitch_,
                                             low_pitch_, high_pitch_);
                }
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (marquee_) {
                const ImVec2 a(std::min(marquee_start_.x, io.MousePos.x),
                               std::min(marquee_start_.y, io.MousePos.y));
                const ImVec2 b(std::max(marquee_start_.x, io.MousePos.x),
                               std::max(marquee_start_.y, io.MousePos.y));
                if (dragged_) {
                    for (PianoRollNote& note : notes_) {
                        const float left = origin.x + note.startBeat * pixels_per_beat_ - scroll_x_;
                        const float right = left + note.duration * pixels_per_beat_;
                        const float top = origin.y + (high_pitch_ - note.pitch) * row_height_ - scroll_y_;
                        if (right >= a.x && left <= b.x && top + row_height_ >= a.y && top <= b.y) {
                            note.selected = true;
                        }
                    }
                } else if (a.x >= origin.x && a.x < origin.x + size.x &&
                           a.y >= origin.y && a.y < origin.y + size.y) {
                    const int pitch = PitchAt(a.y, origin);
                    if (pitch >= low_pitch_ && pitch <= high_pitch_) {
                        AddNote(pitch, Snap(BeatAt(a.x, origin)), 1.0f, 100);
                    }
                }
                marquee_ = false;
            }
            drag_id_ = -1;
            dragged_ = false;
        }
    }

    void DrawKeys(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        const ImU32 white = IM_COL32(224, 225, 219, 255);
        const ImU32 black = IM_COL32(45, 48, 50, 255);
        const ImU32 line = IM_COL32(95, 98, 99, 255);
        const int first = std::max(low_pitch_, high_pitch_ -
                                               static_cast<int>((scroll_y_ + size.y) / row_height_));
        const int last = std::min(high_pitch_, high_pitch_ -
                                              static_cast<int>(scroll_y_ / row_height_));
        for (int pitch = first; pitch <= last; ++pitch) {
            const float top = origin.y + (high_pitch_ - pitch) * row_height_ - scroll_y_;
            const float bottom = top + row_height_;
            const ImU32 fill = pitch == pressed_pitch_ ? IM_COL32(241, 183, 87, 255)
                                                       : (IsBlackKey(pitch) ? black : white);
            draw->AddRectFilled(ImVec2(origin.x, top), ImVec2(origin.x + size.x, bottom), fill);
            draw->AddLine(ImVec2(origin.x, bottom), ImVec2(origin.x + size.x, bottom), line);
            if (!IsBlackKey(pitch)) {
                static const char* names[] = {"C", "C#", "D", "D#", "E", "F",
                                              "F#", "G", "G#", "A", "A#", "B"};
                char label[12];
                std::snprintf(label, sizeof(label), "%s%d", names[pitch % 12], pitch / 12 - 1);
                const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
                draw->AddText(ImVec2(origin.x + 5.0f, top + 2.0f), text, label);
            }
        }
    }

    void DrawGrid(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
        const ImU32 background = IM_COL32(31, 34, 37, 255);
        const ImU32 row_line = IM_COL32(61, 65, 68, 255);
        const ImU32 beat_line = IM_COL32(57, 61, 64, 255);
        const ImU32 bar_line = IM_COL32(104, 110, 113, 255);
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), background);

        const int first_row = std::max(0, static_cast<int>(scroll_y_ / row_height_));
        const int last_row = std::min(high_pitch_ - low_pitch_,
                                      static_cast<int>((scroll_y_ + size.y) / row_height_));
        for (int row = first_row; row <= last_row; ++row) {
            const float y = origin.y + row * row_height_ - scroll_y_;
            draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y), row_line);
        }

        const int first_beat = std::max(0, static_cast<int>(scroll_x_ / pixels_per_beat_));
        const int last_beat = static_cast<int>((scroll_x_ + size.x) / pixels_per_beat_) + 1;
        for (int beat = first_beat; beat <= last_beat && beat < MaxBeat(); ++beat) {
            const float x = origin.x + beat * pixels_per_beat_ - scroll_x_;
            const bool bar = std::fmod(static_cast<float>(beat), beats_per_bar_) < 0.001f;
            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y),
                          bar ? bar_line : beat_line, bar ? 1.5f : 1.0f);
            if (bar) {
                char label[16];
                std::snprintf(label, sizeof(label), "%d", static_cast<int>(beat / beats_per_bar_) + 1);
                draw->AddText(ImVec2(x + 3.0f, origin.y + 2.0f),
                              ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
            }
        }

        for (const PianoRollNote& note : notes_) {
            const float left = origin.x + note.startBeat * pixels_per_beat_ - scroll_x_;
            const float right = left + note.duration * pixels_per_beat_;
            const float top = origin.y + (high_pitch_ - note.pitch) * row_height_ - scroll_y_;
            const ImU32 fill = note.selected ? IM_COL32(237, 164, 71, 255)
                                             : IM_COL32(73, 156, 173, 255);
            draw->AddRectFilled(ImVec2(left + 1.0f, top + 1.0f),
                                ImVec2(std::max(left + 3.0f, right - 1.0f), top + row_height_ - 1.0f),
                                fill, 2.0f);
            draw->AddRect(ImVec2(left + 1.0f, top + 1.0f),
                          ImVec2(std::max(left + 3.0f, right - 1.0f), top + row_height_ - 1.0f),
                          note.selected ? IM_COL32(255, 222, 164, 255) : IM_COL32(124, 198, 208, 255));
        }

        if (marquee_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) && dragged_) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            draw->AddRectFilled(marquee_start_, mouse, IM_COL32(92, 161, 206, 45));
            draw->AddRect(marquee_start_, mouse, IM_COL32(125, 190, 229, 210));
        }
    }

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
