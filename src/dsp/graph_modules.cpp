#include "graph_modules.hpp"

#include "chord_catalog.hpp"

#include <algorithm>
#include <cstring>

#include "imgui.h"

std::string WavPlayer::save() const {
    std::string out = "{";
    module_json::write_bool(out, "loop", loop);
    module_json::write_string(out, "path", path);
    out += '}';
    return out;
}

bool WavPlayer::load(const std::string& json) {
    module_json::read(json, "loop", loop);
    module_json::read(json, "path", path);
    return true;
}

bool WavPlayer::render() {
    return ImGui::Checkbox("Loop", &loop);
}
void WavPlayer::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    if (scratch_.size() != ctx.num_samples) {
        scratch_.assign(ctx.num_samples, 0);
    }
    if (path != loaded_path_) {
        loaded_path_ = path;
        clip_.reset();
        if (ctx.host) {
            clip_ = ctx.host->load_wav(path);
            if (!clip_) {
                clip_ = ctx.host->fallback_wav();
            }
        }
        wav_.reset();
    }
    if (clip_ && clip_->frame_count() > 0) {
        wav_.render(clip_, loop, scratch_.data(), ctx.num_samples / 2);
    } else {
        std::fill(scratch_.begin(), scratch_.end(), 0);
    }
    for (size_t i = 0; i < ctx.num_samples; ++i) {
        ctx.buffer[i] = static_cast<float>(scratch_[i]) / 2147483648.0f;
    }
}

std::string AudioInput::save() const {
    std::string out = "{";
    module_json::write_string(out, "device", device);
    out += '}';
    return out;
}

bool AudioInput::load(const std::string& json) {
    module_json::read(json, "device", device);
    if (device.empty()) {
        device = "default";
    }
    return true;
}

bool AudioInput::render() {
    ImGui::TextUnformatted(device.c_str());
    return false;
}
void AudioInput::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.live_pcm == nullptr) {
        return;
    }
    for (size_t i = 0; i < ctx.num_samples; ++i) {
        ctx.buffer[i] = static_cast<float>(ctx.live_pcm[i]) / 2147483648.0f;
    }
}

std::string AudioOutput::save() const {
    std::string out = "{";
    module_json::write_string(out, "device", device);
    out += '}';
    return out;
}

bool AudioOutput::load(const std::string& json) {
    module_json::read(json, "device", device);
    if (device.empty()) {
        device = "default";
    }
    return true;
}

bool AudioOutput::render() {
    ImGui::TextUnformatted(device.c_str());
    return false;
}

std::string ChordGenerator::save() const {
    std::string out = "{";
    module_json::write_int(out, "chord_index", index);
    out += '}';
    return out;
}

bool ChordGenerator::load(const std::string& json) {
    module_json::read(json, "chord_index", index);
    index = std::clamp(index, 0, kChordCount - 1);
    return true;
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
void ChordGenerator::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    if (scratch_.size() != ctx.num_samples) {
        scratch_.assign(ctx.num_samples, 0);
    }
    chord_.render(index, scratch_.data(), ctx.num_samples / 2, 2,
                  static_cast<uint32_t>(ctx.sample_rate));
    for (size_t i = 0; i < ctx.num_samples; ++i) {
        ctx.buffer[i] = static_cast<float>(scratch_[i]) / 2147483648.0f;
    }
}

std::string WhiteNoise::save() const {
    std::string out = "{";
    module_json::write_float(out, "level", level);
    out += '}';
    return out;
}

bool WhiteNoise::load(const std::string& json) {
    module_json::read(json, "level", level);
    level = std::clamp(level, 0.0f, 1.0f);
    return true;
}

bool WhiteNoise::render() {
    ImGui::TextUnformatted("Level");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##wn_level", &level, 0.0f, 1.0f, "%.2f");
}
void WhiteNoise::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr) {
        return;
    }
    const float amp = std::clamp(level, 0.0f, 1.0f);
    for (size_t i = 0; i < ctx.num_samples; ++i) {
        rng_ = rng_ * 1664525u + 1013904223u;
        const float unit = static_cast<float>((rng_ >> 8) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
        ctx.buffer[i] = (unit * 2.0f - 1.0f) * amp;
    }
}

std::string DryWetMix::save() const {
    std::string out = "{";
    module_json::write_float(out, "mix", mix);
    out += '}';
    return out;
}

bool DryWetMix::load(const std::string& json) {
    module_json::read(json, "mix", mix);
    return true;
}

bool DryWetMix::render() {
    ImGui::TextUnformatted("Wet");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##dw_mix", &mix, 0.0f, 1.0f, "%.2f");
}
void DryWetMix::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr) {
        return;
    }
    const float* dry = (ctx.input_count > 0 && ctx.inputs) ? ctx.inputs[0] : nullptr;
    const float* wet = (ctx.input_count > 1 && ctx.inputs) ? ctx.inputs[1] : nullptr;
    const float wet_amt = std::clamp(mix, 0.0f, 1.0f);
    for (size_t i = 0; i < ctx.num_samples; ++i) {
        const float d = dry ? dry[i] : 0.0f;
        const float w = wet ? wet[i] : 0.0f;
        ctx.buffer[i] = d * (1.0f - wet_amt) + w * wet_amt;
    }
}

std::string VuMeter::save() const {
    std::string out = "{";
    module_json::write_float(out, "display_gain", display_gain);
    module_json::write_float(out, "decay", decay);
    out += '}';
    return out;
}

bool VuMeter::load(const std::string& json) {
    module_json::read(json, "display_gain", display_gain);
    module_json::read(json, "decay", decay);
    return true;
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
void VuMeter::process(ProcessContext& ctx) {
    if (ctx.host) {
        ctx.host->analyze(ctx.node_id, name(), ctx.buffer, ctx.num_samples, display_gain, decay);
    }
}

std::string Oscilloscope::save() const {
    std::string out = "{";
    module_json::write_float(out, "display_gain", display_gain);
    out += '}';
    return out;
}

bool Oscilloscope::load(const std::string& json) {
    module_json::read(json, "display_gain", display_gain);
    return true;
}

bool Oscilloscope::render() {
    ImGui::TextUnformatted("Gain");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##scope_gain", &display_gain, 0.25f, 4.0f, "%.2f");
}
void Oscilloscope::process(ProcessContext& ctx) {
    if (ctx.host) {
        ctx.host->analyze(ctx.node_id, name(), ctx.buffer, ctx.num_samples, display_gain, 0.85f);
    }
}

std::string Spectrum::save() const {
    std::string out = "{";
    module_json::write_float(out, "display_gain", display_gain);
    out += '}';
    return out;
}

bool Spectrum::load(const std::string& json) {
    module_json::read(json, "display_gain", display_gain);
    return true;
}

bool Spectrum::render() {
    ImGui::TextUnformatted("Gain");
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##spec_gain", &display_gain, 0.25f, 4.0f, "%.2f");
}
void Spectrum::process(ProcessContext& ctx) {
    if (ctx.host) {
        ctx.host->analyze(ctx.node_id, name(), ctx.buffer, ctx.num_samples, display_gain, 0.85f);
    }
}

void PitchDetectorModule::process(ProcessContext& ctx) {
    if (ctx.host) {
        ctx.host->analyze(ctx.node_id, name(), ctx.buffer, ctx.num_samples, 1.0f, 0.85f);
    }
}
