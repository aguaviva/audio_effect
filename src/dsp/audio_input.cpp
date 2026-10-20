#include "audio_input.hpp"

#include "imgui.h"

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
