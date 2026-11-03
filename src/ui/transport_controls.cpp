#include "transport_controls.hpp"

#include "dsp/module.hpp"
#include "dsp/transport.hpp"
#include "imgui.h"
#include "model/tempo_grid.hpp"

#include <cmath>

void transport_controls(bool small) {
    Transport& t = transport();
    const auto button = [small](const char* label) {
        return small ? ImGui::SmallButton(label) : ImGui::Button(label);
    };
    if (button(t.playing() ? "Pause###play" : "Play###play")) {
        if (t.playing()) {
            t.pause();
        } else {
            t.play();
        }
    }
    ImGui::SameLine();
    if (button("Stop###stop")) {
        t.pause();
        t.seek(0);
    }
    ImGui::SameLine();
    const double seconds = static_cast<double>(t.position()) / kSampleRate;
    const TempoGrid& grid = song_grid();
    const double beat = grid.beat_at_seconds(seconds);
    const double bar = std::floor(beat / grid.beats_per_bar());
    ImGui::Text("%.2f s  %d.%d", seconds, static_cast<int>(bar) + 1,
                static_cast<int>(std::floor(beat - bar * grid.beats_per_bar())) + 1);
}
