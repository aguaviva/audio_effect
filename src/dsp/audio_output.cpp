#include "audio_output.hpp"

#include <algorithm>
#include <cmath>

#include "imgui.h"

std::string AudioOutput::save() const {
    std::string out = "{";
    module_json::write_string(out, "device", device);
    module_json::write_bool(out, "muted", muted);
    module_json::write_float(out, "volume_db", volume_db);
    out += '}';
    return out;
}

bool AudioOutput::load(const std::string& json) {
    module_json::read(json, "device", device);
    if (device.empty()) {
        device = "default";
    }
    module_json::read(json, "muted", muted);
    module_json::read(json, "volume_db", volume_db);
    volume_db = std::clamp(volume_db, kMinVolumeDb, kMaxVolumeDb);
    return true;
}

bool AudioOutput::render() {
    bool changed = ImGui::Checkbox("Mute", &muted);
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##out_vol", &volume_db, kMinVolumeDb, kMaxVolumeDb, "%.1f dB");
    return changed;
}

void AudioOutput::process(const float* const* inputs, int input_count,
                          size_t sample_count, float /*sample_rate*/) {
    float* output = output_buffer(0);
    copy_input(inputs, input_count, 0, output, sample_count);

    const float target = muted ? 0.0f : std::pow(10.0f, volume_db / 20.0f);
    const size_t frames = sample_count / 2;
    if (frames == 0) {
        return;
    }
    const float step = (target - gain_) / static_cast<float>(frames);
    for (size_t f = 0; f < frames; ++f) {
        gain_ += step;
        output[f * 2] *= gain_;
        output[f * 2 + 1] *= gain_;
    }
    gain_ = target;
}
