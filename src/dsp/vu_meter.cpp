#include "vu_meter.hpp"

#include "imgui.h"

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
    //ImGui::TextUnformatted("Gain");
    //ImGui::SetNextItemWidth(-1.0f);
    //changed |= ImGui::SliderFloat("##vu_gain", &display_gain, 0.25f, 4.0f, "%.2f");
    ImGui::TextUnformatted("Decay");
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##vu_decay", &decay, 0.5f, 0.99f, "%.2f");
    return changed;
}
void VuMeter::process(const float* const* inputs, int input_count, float* output,
                      size_t sample_count, float /*sample_rate*/) {
    copy_input(inputs, input_count, 0, output, sample_count);
    if (host_) {
        host_->analyze(node_id_, name(), output, sample_count, display_gain, decay);
    }
}
