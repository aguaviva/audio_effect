#include "mixer.hpp"

#include <algorithm>

#include "imgui.h"

bool Mixer::render() {
    ImGui::TextDisabled("avg mix");
    return false;
}

void Mixer::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    std::fill(ctx.buffer, ctx.buffer + ctx.num_samples, 0.0f);
    if (ctx.inputs == nullptr || ctx.input_count == 0) {
        return;
    }

    size_t connected = 0;
    for (size_t s = 0; s < ctx.input_count; ++s) {
        if (ctx.input_muted && ctx.input_muted[s]) {
            continue;
        }
        const float* src = ctx.inputs[s];
        if (src == nullptr) {
            continue;
        }
        for (size_t i = 0; i < ctx.num_samples; ++i) {
            ctx.buffer[i] += src[i];
        }
        ++connected;
    }

    if (connected > 0) {
        const float scale = 1.0f / static_cast<float>(connected);
        for (size_t i = 0; i < ctx.num_samples; ++i) {
            ctx.buffer[i] *= scale;
        }
    }
}
