#include "piano_roll.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>

namespace audio_effect {

namespace {

float DistanceSquared(const ImVec2& a, const ImVec2& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

bool IsBlackKey(int pitch) {
    const int semitone = pitch % 12;
    return semitone == 1 || semitone == 3 || semitone == 6 ||
           semitone == 8 || semitone == 10;
}

std::vector<PianoRollNote>& Clipboard() {
    static std::vector<PianoRollNote> notes; // startBeat relative to the earliest note
    return notes;
}

const ImU32 kRulerBackground = IM_COL32(40, 43, 47, 255);
const ImU32 kPlayheadColor = IM_COL32(255, 200, 60, 255);

} // namespace

int PianoRollEditor::AddNote(int pitch, float startBeat, float duration, int velocity) {
    const int id = next_id_++;
    notes_.push_back({id, std::clamp(pitch, 0, 127), std::max(0.0f, startBeat),
                      std::max(grid_step_, duration), std::clamp(velocity, 1, 127), false});
    return id;
}

void PianoRollEditor::ClearNotes() {
    notes_.clear();
    drag_id_ = -1;
}

void PianoRollEditor::SetPitchCallback(std::function<void(int, bool)> callback) {
    pitch_callback_ = std::move(callback);
}

void PianoRollEditor::DeleteNote(int noteId) {
    notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                                [noteId](const PianoRollNote& note) { return note.id == noteId; }),
                 notes_.end());
    if (drag_id_ == noteId) {
        drag_id_ = -1;
    }
}

void PianoRollEditor::SelectAll() {
    for (PianoRollNote& note : notes_) {
        note.selected = true;
    }
}

void PianoRollEditor::ClearSelection() {
    for (PianoRollNote& note : notes_) {
        note.selected = false;
    }
}

void PianoRollEditor::DeleteSelectedNotes() {
    notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                                [](const PianoRollNote& note) { return note.selected; }),
                 notes_.end());
    drag_id_ = -1;
}

void PianoRollEditor::CopySelection() {
    std::vector<PianoRollNote> copied;
    float first = 0.0f;
    for (const PianoRollNote& note : notes_) {
        if (note.selected) {
            first = copied.empty() ? note.startBeat : std::min(first, note.startBeat);
            copied.push_back(note);
        }
    }
    if (copied.empty()) {
        return;
    }
    for (PianoRollNote& note : copied) {
        note.startBeat -= first;
    }
    Clipboard() = std::move(copied);
}

void PianoRollEditor::CutSelection() {
    CopySelection();
    DeleteSelectedNotes();
}

void PianoRollEditor::Paste(float beat) {
    if (!Clipboard().empty()) {
        AddSelected(Clipboard(), std::max(0.0f, beat));
    }
}

void PianoRollEditor::DuplicateSelection() {
    std::vector<PianoRollNote> selected;
    float first = 0.0f;
    float end = 0.0f;
    for (const PianoRollNote& note : notes_) {
        if (note.selected) {
            first = selected.empty() ? note.startBeat : std::min(first, note.startBeat);
            end = std::max(end, note.startBeat + note.duration);
            selected.push_back(note);
        }
    }
    if (!selected.empty()) {
        AddSelected(selected, std::ceil((end - first) / grid_step_ - 1e-4f) * grid_step_);
    }
}

void PianoRollEditor::AddSelected(const std::vector<PianoRollNote>& notes, float beats) {
    ClearSelection();
    for (const PianoRollNote& note : notes) {
        AddNote(note.pitch, note.startBeat + beats, note.duration, note.velocity);
        notes_.back().selected = true;
    }
}

void PianoRollEditor::SetTempo(float bpm) { bpm_ = std::clamp(bpm, 1.0f, 999.0f); }

float PianoRollEditor::SecondsAtBeat(float beat) const {
    float seconds = 0.0f;
    float from = 0.0f;
    float seconds_per_beat = 60.0f / bpm_;
    for (const TempoChange& change : tempo_map_) {
        if (change.beat >= beat) {
            break;
        }
        seconds += (change.beat - from) * seconds_per_beat;
        from = change.beat;
        seconds_per_beat = change.seconds_per_beat;
    }
    return seconds + (beat - from) * seconds_per_beat;
}

float PianoRollEditor::BeatAtSeconds(float seconds) const {
    float at = 0.0f; // seconds at beat `from`
    float from = 0.0f;
    float seconds_per_beat = 60.0f / bpm_;
    for (const TempoChange& change : tempo_map_) {
        const float at_change = at + (change.beat - from) * seconds_per_beat;
        if (at_change >= seconds) {
            break;
        }
        at = at_change;
        from = change.beat;
        seconds_per_beat = change.seconds_per_beat;
    }
    return from + (seconds - at) / seconds_per_beat;
}

void PianoRollEditor::SetTimeSignature(int numerator, int denominator) {
    beats_per_bar_ = std::clamp(numerator, 1, 32) * 4.0f /
                     static_cast<float>(std::clamp(denominator, 1, 32));
}

void PianoRollEditor::SetGridDivision(int divisionsPerBeat) {
    grid_step_ = 1.0f / static_cast<float>(std::clamp(divisionsPerBeat, 1, 64));
}

void PianoRollEditor::SetPitchRange(int low, int high) {
    low_pitch_ = std::clamp(low, 0, 127);
    high_pitch_ = std::clamp(high, 0, 127);
    if (low_pitch_ > high_pitch_) {
        std::swap(low_pitch_, high_pitch_);
    }
    scroll_y_ = std::clamp(scroll_y_, 0.0f,
                           std::max(0.0f, (high_pitch_ - low_pitch_ + 1) * row_height_ - 1.0f));
}

void PianoRollEditor::Render(const char* id, ImVec2 size) {
    ImGui::PushID(this);
    if (id && id[0]) {
        ImGui::PushID(id);
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float height = size.y > 0.0f ? size.y : std::max(120.0f, available.y);
    const float key_width = std::clamp(size.x > 0.0f ? size.x * 0.14f : 58.0f, 48.0f, 96.0f);
    const float ruler_height = 2.0f * (ImGui::GetTextLineHeight() + 2.0f);
    ImGuiWindowFlags child_flags = ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoScrollbar;
    // The ruler and the surfaces under it are stacked items that must touch.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));

    ImGui::BeginChild("##piano_keys", ImVec2(key_width, height), ImGuiChildFlags_Borders,
                      child_flags);
    ImDrawList* key_draw = ImGui::GetWindowDrawList();
    const ImVec2 key_top = ImGui::GetCursorScreenPos();
    const float key_width_inner = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(key_width_inner, ruler_height));
    const ImVec2 key_origin = ImGui::GetCursorScreenPos();
    const ImVec2 key_size(key_width_inner,
                          std::max(1.0f, ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##key_surface", key_size);
    const bool key_hovered = ImGui::IsItemHovered();
    HandleKeyboard(key_origin, key_hovered);
    key_draw->PushClipRect(key_origin, ImVec2(key_origin.x + key_size.x, key_origin.y + key_size.y), true);
    DrawKeys(key_draw, key_origin, key_size);
    key_draw->PopClipRect();
    key_draw->AddRectFilled(key_top, ImVec2(key_top.x + key_width_inner, key_origin.y), kRulerBackground);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##piano_grid", ImVec2(0.0f, height), ImGuiChildFlags_Borders,
                      child_flags);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 ruler_origin = ImGui::GetCursorScreenPos();
    const ImVec2 ruler_size(std::max(1.0f, ImGui::GetContentRegionAvail().x), ruler_height);
    ImGui::InvisibleButton("##ruler_surface", ruler_size);
    const bool ruler_hovered = ImGui::IsItemHovered();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_size(ruler_size.x, std::max(1.0f, ImGui::GetContentRegionAvail().y));
    ImGui::InvisibleButton("##grid_surface", canvas_size);
    const bool canvas_hovered = ImGui::IsItemHovered();
    if (frame_pending_) {
        FrameNow(canvas_size.y);
    }
    HandleNavigation(origin, canvas_size, key_hovered || canvas_hovered || ruler_hovered);
    HandleEditing(origin, canvas_size, canvas_hovered);
    FollowPlayhead(canvas_size.x);
    draw->PushClipRect(origin, ImVec2(origin.x + canvas_size.x, origin.y + canvas_size.y), true);
    DrawGrid(draw, origin, canvas_size);
    draw->PopClipRect();
    DrawRuler(draw, ruler_origin, ruler_size);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (id && id[0]) {
        ImGui::PopID();
    }
    ImGui::PopID();
}

float PianoRollEditor::MaxBeat() const {
    float end = 64.0f;
    for (const PianoRollNote& note : notes_) {
        end = std::max(end, note.startBeat + note.duration + 16.0f);
    }
    return end;
}

float PianoRollEditor::BeatAt(float x, const ImVec2& origin) const {
    return (x - origin.x + scroll_x_) / pixels_per_beat_;
}

int PianoRollEditor::PitchAt(float y, const ImVec2& origin) const {
    const int row = static_cast<int>(std::floor((y - origin.y + scroll_y_) / row_height_));
    return high_pitch_ - row;
}

float PianoRollEditor::Snap(float beat) const {
    return std::max(0.0f, std::round(beat / grid_step_) * grid_step_);
}

void PianoRollEditor::FrameNow(float height) {
    frame_pending_ = false;
    scroll_x_ = 0.0f;
    int lowest = high_pitch_;
    int highest = low_pitch_;
    for (const PianoRollNote& note : notes_) {
        lowest = std::min(lowest, std::max(note.pitch, low_pitch_));
        highest = std::max(highest, std::min(note.pitch, high_pitch_));
    }
    if (lowest > highest) {
        return;
    }
    const float top = (high_pitch_ - highest) * row_height_;
    const float bottom = (high_pitch_ - lowest + 1) * row_height_;
    const float target = bottom - top <= height ? (top + bottom - height) * 0.5f : top - row_height_;
    scroll_y_ = std::clamp(target, 0.0f,
                           std::max(0.0f, (high_pitch_ - low_pitch_ + 1) * row_height_ - height));
}

PianoRollNote* PianoRollEditor::HitNote(float x, float y, const ImVec2& origin) {
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

void PianoRollEditor::HandleKeyboard(const ImVec2& origin, bool hovered) {
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

void PianoRollEditor::HandleNavigation(const ImVec2& origin, const ImVec2& size, bool hovered) {
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

// Only a moving playhead scrolls the view, so a paused one can be scrolled away from.
void PianoRollEditor::FollowPlayhead(float width) {
    const bool moved = playhead_beat_ != followed_beat_;
    followed_beat_ = playhead_beat_;
    if (!follow_playhead_ || !moved || playhead_beat_ < 0.0f || panning_ || drag_id_ >= 0) {
        return;
    }
    const float x = playhead_beat_ * pixels_per_beat_ - scroll_x_;
    if (x < 0.0f || x > width * 0.9f) {
        scroll_x_ = std::max(0.0f, playhead_beat_ * pixels_per_beat_ - width * 0.1f);
    }
}

void PianoRollEditor::HandleEditing(const ImVec2& origin, const ImVec2& size, bool hovered) {
    const ImGuiIO& io = ImGui::GetIO();
    if (hovered && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            DeleteSelectedNotes();
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) {
            SelectAll();
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            CopySelection();
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_X)) {
            CutSelection();
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
            Paste(Snap(BeatAt(io.MousePos.x, origin)));
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) {
            DuplicateSelection();
        }
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
                drag_anchor_ = *note;
                drag_origin_.clear();
                for (size_t i = 0; i < notes_.size(); ++i) {
                    if (notes_[i].selected) {
                        drag_origin_.emplace_back(i, notes_[i]);
                    }
                }
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

    // Every selected note moves (or resizes) by as much as the grabbed one, keeping the whole
    // selection at beat 0 or later and inside the pitch range.
    if (drag_id_ >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (DistanceSquared(io.MousePos, drag_start_mouse_) > 16.0f) {
            dragged_ = true;
        }
        if (resize_drag_) {
            const float duration =
                std::max(grid_step_, Snap(BeatAt(io.MousePos.x, origin) - drag_anchor_.startBeat));
            const float delta = duration - drag_anchor_.duration;
            for (const auto& [index, was] : drag_origin_) {
                if (index < notes_.size()) {
                    notes_[index].duration = std::max(grid_step_, was.duration + delta);
                }
            }
        } else {
            float delta = Snap(BeatAt(io.MousePos.x, origin) - grab_beat_) - drag_anchor_.startBeat;
            int pitch_delta = PitchAt(io.MousePos.y, origin) + grab_pitch_ - drag_anchor_.pitch;
            float earliest = drag_anchor_.startBeat;
            int lowest = drag_anchor_.pitch;
            int highest = drag_anchor_.pitch;
            for (const auto& [index, was] : drag_origin_) {
                earliest = std::min(earliest, was.startBeat);
                lowest = std::min(lowest, was.pitch);
                highest = std::max(highest, was.pitch);
            }
            delta = std::max(delta, -earliest);
            const int min_delta = low_pitch_ - lowest;
            const int max_delta = high_pitch_ - highest;
            pitch_delta = min_delta <= max_delta ? std::clamp(pitch_delta, min_delta, max_delta) : 0;
            for (const auto& [index, was] : drag_origin_) {
                if (index < notes_.size()) {
                    notes_[index].startBeat = was.startBeat + delta;
                    notes_[index].pitch = was.pitch + pitch_delta;
                }
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
        drag_origin_.clear();
        dragged_ = false;
    }
}

void PianoRollEditor::DrawKeys(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
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

void PianoRollEditor::DrawGrid(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
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
    const int last_beat = std::min(static_cast<int>((scroll_x_ + size.x) / pixels_per_beat_) + 1,
                                   static_cast<int>(std::ceil(MaxBeat())) - 1);
    for (int beat = first_beat; beat <= last_beat; ++beat) {
        const float x = origin.x + beat * pixels_per_beat_ - scroll_x_;
        const bool bar = std::fmod(static_cast<float>(beat), beats_per_bar_) < 0.001f;
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y),
                      bar ? bar_line : beat_line, bar ? 1.5f : 1.0f);
    }

    for (const PianoRollNote& note : notes_) {
        const float left = origin.x + note.startBeat * pixels_per_beat_ - scroll_x_;
        const float right = left + note.duration * pixels_per_beat_;
        const float top = origin.y + (high_pitch_ - note.pitch) * row_height_ - scroll_y_;
        if (right < origin.x || left > origin.x + size.x ||
            top + row_height_ < origin.y || top > origin.y + size.y) {
            continue;
        }
        const bool sounding = playhead_beat_ >= note.startBeat &&
                              playhead_beat_ < note.startBeat + note.duration;
        const ImU32 fill = note.selected ? IM_COL32(237, 164, 71, 255)
                           : sounding    ? IM_COL32(150, 225, 240, 255)
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

    if (playhead_beat_ >= 0.0f) {
        const float x = origin.x + playhead_beat_ * pixels_per_beat_ - scroll_x_;
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), kPlayheadColor, 2.0f);
    }
}

// Bar numbers on the top half, time on the bottom half, ticks at least 64 px apart.
void PianoRollEditor::DrawRuler(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const {
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    const float mid = origin.y + size.y * 0.5f;
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 tick = IM_COL32(104, 110, 113, 255);
    draw->PushClipRect(origin, end, true);
    draw->AddRectFilled(origin, end, kRulerBackground);

    const int first_bar = static_cast<int>(scroll_x_ / pixels_per_beat_ / beats_per_bar_);
    for (int bar = std::max(first_bar, 0);; ++bar) {
        const float x = origin.x + bar * beats_per_bar_ * pixels_per_beat_ - scroll_x_;
        if (x > end.x || bar * beats_per_bar_ >= MaxBeat()) {
            break;
        }
        char label[16];
        std::snprintf(label, sizeof(label), "%d", bar + 1);
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, mid), tick);
        draw->AddText(ImVec2(x + 3.0f, origin.y + 1.0f), text, label);
    }

    const float first_seconds = SecondsAtBeat(scroll_x_ / pixels_per_beat_);
    const float last_seconds = SecondsAtBeat((scroll_x_ + size.x) / pixels_per_beat_);
    const float pixels_per_second = size.x / std::max(1e-3f, last_seconds - first_seconds);
    static const float kSteps[] = {0.1f, 0.2f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f,
                                   15.0f, 30.0f, 60.0f, 120.0f, 300.0f, 600.0f};
    float step = kSteps[std::size(kSteps) - 1];
    for (float s : kSteps) {
        if (s * pixels_per_second >= 64.0f) {
            step = s;
            break;
        }
    }
    for (int i = static_cast<int>(first_seconds / step); i * step <= last_seconds; ++i) {
        const float seconds = i * step;
        const float x = origin.x + BeatAtSeconds(seconds) * pixels_per_beat_ - scroll_x_;
        char label[16];
        if (step < 1.0f) {
            std::snprintf(label, sizeof(label), "%.1f s", seconds);
        } else {
            const int whole = static_cast<int>(std::lround(seconds));
            std::snprintf(label, sizeof(label), "%d:%02d", whole / 60, whole % 60);
        }
        draw->AddLine(ImVec2(x, mid), ImVec2(x, end.y), tick);
        draw->AddText(ImVec2(x + 3.0f, mid + 1.0f), dim, label);
    }
    draw->AddLine(ImVec2(origin.x, mid), ImVec2(end.x, mid), IM_COL32(61, 65, 68, 255));
    draw->AddLine(ImVec2(origin.x, end.y - 1.0f), ImVec2(end.x, end.y - 1.0f), tick);

    if (playhead_beat_ >= 0.0f) {
        const float x = origin.x + playhead_beat_ * pixels_per_beat_ - scroll_x_;
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), kPlayheadColor, 2.0f);
        draw->AddTriangleFilled(ImVec2(x - 6.0f, end.y - 9.0f), ImVec2(x + 6.0f, end.y - 9.0f),
                                ImVec2(x, end.y), kPlayheadColor);
    }
    draw->PopClipRect();
}

} // namespace audio_effect
