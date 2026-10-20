#include "audio_output.hpp"

#include "imgui.h"

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
