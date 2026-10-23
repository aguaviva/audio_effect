#include "audio_input.hpp"

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
    return false;
}
void AudioInput::process(const float* const* /*inputs*/, int /*input_count*/, float* const* outputs,
                         size_t sample_count, float /*sample_rate*/) {
    float* output = outputs[0];
    if (!output) {
        return;
    }
    const float* live = host_ ? host_->live_input() : nullptr;
    if (live) {
        std::copy(live, live + sample_count, output);
    } else {
        std::fill(output, output + sample_count, 0.0f);
    }
}
