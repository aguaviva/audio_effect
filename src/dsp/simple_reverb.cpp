#include "simple_reverb.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

namespace {
constexpr int kCombMs[4] = {31, 37, 43, 47};
constexpr int kAllpassMs[2] = {5, 7};
}

float SimpleReverb::Comb::process(float input, float feedback, float damp) {
    if (buffer.empty()) {
        return input;
    }
    const float delayed = buffer[index];
    filter = delayed * (1.0f - damp) + filter * damp;
    buffer[index] = input + filter * feedback;
    index = (index + 1) % buffer.size();
    return delayed;
}

float SimpleReverb::Allpass::process(float input) {
    if (buffer.empty()) {
        return input;
    }
    const float buffered = buffer[index];
    const float output = -input + buffered;
    buffer[index] = input + buffered * 0.5f;
    index = (index + 1) % buffer.size();
    return output;
}

SimpleReverb::SimpleReverb() = default;

void SimpleReverb::reset() {
    for (auto& c : combs_l_) {
        std::fill(c.buffer.begin(), c.buffer.end(), 0.0f);
        c.index = 0;
        c.filter = 0.0f;
    }
    for (auto& c : combs_r_) {
        std::fill(c.buffer.begin(), c.buffer.end(), 0.0f);
        c.index = 0;
        c.filter = 0.0f;
    }
    for (auto& a : allpass_l_) {
        std::fill(a.buffer.begin(), a.buffer.end(), 0.0f);
        a.index = 0;
    }
    for (auto& a : allpass_r_) {
        std::fill(a.buffer.begin(), a.buffer.end(), 0.0f);
        a.index = 0;
    }
}

void SimpleReverb::ensure_buffers(float sample_rate) {
    if (configured_sr_ == sample_rate) {
        return;
    }
    configured_sr_ = sample_rate;
    for (size_t i = 0; i < combs_l_.size(); ++i) {
        const size_t n = std::max<size_t>(
            8, static_cast<size_t>(sample_rate * static_cast<float>(kCombMs[i]) * 0.001f));
        combs_l_[i].buffer.assign(n, 0.0f);
        combs_l_[i].index = 0;
        combs_l_[i].filter = 0.0f;
        // Slightly longer right combs for stereo width.
        combs_r_[i].buffer.assign(n + 23, 0.0f);
        combs_r_[i].index = 0;
        combs_r_[i].filter = 0.0f;
    }
    for (size_t i = 0; i < allpass_l_.size(); ++i) {
        const size_t n = std::max<size_t>(
            4, static_cast<size_t>(sample_rate * static_cast<float>(kAllpassMs[i]) * 0.001f));
        allpass_l_[i].buffer.assign(n, 0.0f);
        allpass_l_[i].index = 0;
        allpass_r_[i].buffer.assign(n + 11, 0.0f);
        allpass_r_[i].index = 0;
    }
}

void SimpleReverb::process(float* interleaved, size_t sample_count, float sample_rate) {
    if (interleaved == nullptr || sample_count < 2 || sample_rate <= 0.0f) {
        return;
    }
    ensure_buffers(sample_rate);
    float room_size = std::clamp(room, 0.0f, 1.0f);
    float damp_amt = std::clamp(damping, 0.0f, 1.0f);
    float wet = std::clamp(mix, 0.0f, 1.0f);
    const float feedback = 0.28f + room_size * 0.70f;
    const float damp = 0.15f + damp_amt * 0.75f;

    for (size_t i = 0; i + 1 < sample_count; i += 2) {
        const float dry_l = interleaved[i];
        const float dry_r = interleaved[i + 1];
        float wet_l = 0.0f;
        float wet_r = 0.0f;
        for (size_t c = 0; c < combs_l_.size(); ++c) {
            wet_l += combs_l_[c].process(dry_l, feedback, damp);
            wet_r += combs_r_[c].process(dry_r, feedback, damp);
        }
        wet_l *= 0.25f;
        wet_r *= 0.25f;
        for (auto& ap : allpass_l_) {
            wet_l = ap.process(wet_l);
        }
        for (auto& ap : allpass_r_) {
            wet_r = ap.process(wet_r);
        }
        interleaved[i] = dry_l * (1.0f - wet) + wet_l * wet;
        interleaved[i + 1] = dry_r * (1.0f - wet) + wet_r * wet;
    }
}

bool SimpleReverb::render() {
    bool changed = false;
    ImGui::TextUnformatted("Room");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##rev_room", &room, 0.0f, 1.0f, "%.2f");
    ImGui::TextUnformatted("Damping");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##rev_damp", &damping, 0.0f, 1.0f, "%.2f");
    ImGui::TextUnformatted("Mix");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##rev_mix", &mix, 0.0f, 1.0f, "%.2f");
    return changed;
}

void SimpleReverb::save(std::string& out, bool& first) const {
    module_json::write_float(out, first, "room", room);
    module_json::write_float(out, first, "damping", damping);
    module_json::write_float(out, first, "mix", mix);
}

bool SimpleReverb::load(const std::string& key, double num, bool, const std::string&, int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "room") { room = static_cast<float>(num); return true; }
    if (key == "damping") { damping = static_cast<float>(num); return true; }
    if (key == "mix") { mix = static_cast<float>(num); return true; }
    return false;
}

void SimpleReverb::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    process(ctx.buffer, ctx.num_samples, ctx.sample_rate);
}
