#include "spectrum.hpp"

#include "imgui.h"

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
    //ImGui::TextUnformatted("Gain");
    //ImGui::SetNextItemWidth(-1.0f);
    //return ImGui::SliderFloat("##spec_gain", &display_gain, 0.25f, 4.0f, "%.2f");
    
    return true;
}
void Spectrum::process(const float* const* inputs, int input_count, float* output,
                       size_t sample_count, float /*sample_rate*/) {
    copy_input(inputs, input_count, 0, output, sample_count);
    if (host_) {
        host_->analyze(node_id_, name(), output, sample_count, display_gain, 0.85f);
    }
}
