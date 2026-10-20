#include "oscilloscope.hpp"

#include "imgui.h"

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
    //ImGui::TextUnformatted("Gain");
    //ImGui::SetNextItemWidth(-1.0f);
    //return ImGui::SliderFloat("##scope_gain", &display_gain, 0.25f, 4.0f, "%.2f");
    return true;
}
void Oscilloscope::process(ProcessContext& ctx) {
    if (ctx.host) {
        ctx.host->analyze(ctx.node_id, name(), ctx.buffer, ctx.num_samples, display_gain, 0.85f);
    }
}
