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

void DryWetMix::get_inputs(int *count, const char* const** str) const
{   
    static const char* input_names[] = {"dry", "wet"};    
    *count=2; 
    *str=input_names;
} 

bool DryWetMix::render() {
    ImGui::SetNextItemWidth(-1.0f);
    return ImGui::SliderFloat("##dw_mix", &mix, 0.0f, 1.0f, "%.2f");
}
void DryWetMix::process(const float* const* inputs, int input_count, float* const* outputs,
                        size_t sample_count, float /*sample_rate*/) {
    float* output = outputs[0];
    if (!output) {
        return;
    }
    const float* dry = input_count > 0 ? inputs[0] : nullptr;
    const float* wet = input_count > 1 ? inputs[1] : nullptr;
    const float wet_amt = std::clamp(mix, 0.0f, 1.0f);
    for (size_t i = 0; i < sample_count; ++i) {
        const float d = dry ? dry[i] : 0.0f;
        const float w = wet ? wet[i] : 0.0f;
        output[i] = d * (1.0f - wet_amt) + w * wet_amt;
    }
}
