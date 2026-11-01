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

    bool result = load_file(path.c_str(), &pMidi_buffer, &midi_size);
    if (result)
    {
        midi_song.Reset();

        midi_song.LoadMidi(pMidi_buffer, midi_size);
        for (uint32_t t = 0; t < midi_song.GetTrackCount(); t++)
        {
            midi_song.GetTrack(t)->pPiano = GetNewPiano();
        }
    }
    return error_.empty(); // clip now holds the replaced one, freed here outside the lock
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
    set_path(path);
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
    if (ImGui::SmallButton("reset")) {
        midi_song.Reset();
    }
    ImGui::Text("%f",midi_song.get_elapsed_milliseconds()/1000.0f);        
    return changed;
}

void Midi::render_window() {
}

void Midi::process(const float* const* /*inputs*/, int /*input_count*/,
                       size_t sample_count, float /*sample_rate*/) {

    uint32_t tracks = MIN(kTracks, midi_song.GetTrackCount());

    for (uint32_t t = 0; t < tracks; t++)
    {
        if (output_buffer(t))
            std::fill(output_buffer(t), output_buffer(t) + sample_count, 0.0f);
    }                        

    size_t size = sample_count/2;
    uint32_t offset = 0;
    while(size>0)
    {
        size_t n_samples = midi_song.RenderMidi(kSampleRate, size);
        if (n_samples==NO_EVENTS)
            return;

        for (uint32_t t = 0; t < tracks; t++)
        {
            float *pOut = output_buffer(t);
            if (midi_song.GetTrack(t)!=NULL && pOut)
            {
                midi_song.GetTrack(t)->pPiano->render_samples(n_samples, pOut + (2*offset));
            }
        }                    
        size -= n_samples;                
        offset += n_samples;
    }
}
