#include "dry_wet_mix.hpp"

#include <algorithm>

#include "imgui.h"

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

void DryWetMix::get_inputs(int *count, const char* const** str) 
{   
    static const char* input_names[] = {"dry", "wet"};    
    *count=2; 
    *str=input_names;
} 

bool DryWetMix::render() {
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
