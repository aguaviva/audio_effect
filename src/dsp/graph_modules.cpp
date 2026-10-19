#include "graph_modules.hpp"

#include "chord_catalog.hpp"

#include <algorithm>
#include <cstring>

#include "imgui.h"

void WavPlayer::save(std::string& out, bool& first) const {
    module_json::write_bool(out, first, "loop", loop);
    module_json::write_string(out, first, "path", path);
}
bool WavPlayer::load(const std::string& key, double, bool flag, const std::string& str, int kind) {
    if (key == "loop" && kind == module_json::kKindBool) {
        loop = flag;
        return true;
    }
    if (key == "path" && kind == module_json::kKindString) {
        path = str;
        return true;
    }
    return false;
}
bool WavPlayer::render() {
    bool changed = false;
    if (ImGui::Checkbox("Loop", &loop)) {
        changed = true;
    }
    return changed;
}

void AudioInput::save(std::string& out, bool& first) const {
    module_json::write_string(out, first, "device", device);
}
bool AudioInput::load(const std::string& key, double, bool, const std::string& str, int kind) {
    if (key == "device" && kind == module_json::kKindString) {
        device = str.empty() ? "default" : str;
        return true;
    }
    return false;
}
bool AudioInput::render() {
    ImGui::TextUnformatted(device.c_str());
    return false;
}

void AudioOutput::save(std::string& out, bool& first) const {
    module_json::write_string(out, first, "device", device);
}
bool AudioOutput::load(const std::string& key, double, bool, const std::string& str, int kind) {
    if (key == "device" && kind == module_json::kKindString) {
        device = str.empty() ? "default" : str;
        return true;
    }
    return false;
}
bool AudioOutput::render() {
    ImGui::TextUnformatted(device.c_str());
    return false;
}

void ChordGenerator::save(std::string& out, bool& first) const {
    module_json::write_int(out, first, "chord_index", index);
}
bool ChordGenerator::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "chord_index" && kind == module_json::kKindNumber) {
        index = std::clamp(static_cast<int>(num), 0, kChordCount - 1);
        return true;
    }
    return false;
}
bool ChordGenerator::render() {
    ImGui::TextUnformatted("Chord");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##chord", &index, chord_combo_items(), kChordCount)) {
        index = std::clamp(index, 0, kChordCount - 1);
        return true;
    }
    index = std::clamp(index, 0, kChordCount - 1);
    return false;
}

void WhiteNoise::save(std::string& out, bool& first) const {
    module_json::write_float(out, first, "level", level);
}
bool WhiteNoise::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "level" && kind == module_json::kKindNumber) {
        level = std::clamp(static_cast<float>(num), 0.0f, 1.0f);
        return true;
    }
    return false;
}
void WhiteNoise::process(float* samples, size_t num_samples) {
    const float amp = std::clamp(level, 0.0f, 1.0f);
    for (size_t i = 0; i < num_samples; ++i) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const float unit = static_cast<float>((rng_ >> 8) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
        samples[i] = (unit * 2.0f - 1.0f) * amp;
    }
}
bool WhiteNoise::render() {
    ImGui::TextUnformatted("Level");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##wn_level", &level, 0.0f, 1.0f, "%.2f");
}

void DryWetMix::save(std::string& out, bool& first) const {
    module_json::write_float(out, first, "mix", mix);
}
bool DryWetMix::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "mix" && kind == module_json::kKindNumber) {
        mix = static_cast<float>(num);
        return true;
    }
    return false;
}
void DryWetMix::process(const float* dry, const float* wet, float* destination,
                        size_t num_samples) const {
    const float wet_amt = std::clamp(mix, 0.0f, 1.0f);
    for (size_t i = 0; i < num_samples; ++i) {
        const float d = dry ? dry[i] : 0.0f;
        const float w = wet ? wet[i] : 0.0f;
        destination[i] = d * (1.0f - wet_amt) + w * wet_amt;
    }
}
bool DryWetMix::render() {
    ImGui::TextUnformatted("Wet");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##dw_mix", &mix, 0.0f, 1.0f, "%.2f");
}

void VuMeter::save(std::string& out, bool& first) const {
    module_json::write_float(out, first, "display_gain", display_gain);
    module_json::write_float(out, first, "decay", decay);
}
bool VuMeter::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "display_gain") {
        display_gain = static_cast<float>(num);
        return true;
    }
    if (key == "decay") {
        decay = static_cast<float>(num);
        return true;
    }
    return false;
}
bool VuMeter::render() {
    bool changed = false;
    ImGui::TextUnformatted("Gain");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##vu_gain", &display_gain, 0.25f, 4.0f, "%.2f");
    ImGui::TextUnformatted("Decay");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##vu_decay", &decay, 0.5f, 0.99f, "%.2f");
    return changed;
}

void Oscilloscope::save(std::string& out, bool& first) const {
    module_json::write_float(out, first, "display_gain", display_gain);
}
bool Oscilloscope::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "display_gain" && kind == module_json::kKindNumber) {
        display_gain = static_cast<float>(num);
        return true;
    }
    return false;
}
bool Oscilloscope::render() {
    ImGui::TextUnformatted("Gain");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##scope_gain", &display_gain, 0.25f, 4.0f, "%.2f");
}

void Spectrum::save(std::string& out, bool& first) const {
    module_json::write_float(out, first, "display_gain", display_gain);
}
bool Spectrum::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "display_gain" && kind == module_json::kKindNumber) {
        display_gain = static_cast<float>(num);
        return true;
    }
    return false;
}
bool Spectrum::render() {
    ImGui::TextUnformatted("Gain");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##spec_gain", &display_gain, 0.25f, 4.0f, "%.2f");
}
