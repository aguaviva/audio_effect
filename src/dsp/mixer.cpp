#include "mixer.hpp"

#include <algorithm>

#include "imgui.h"

bool Mixer::render() {
    ImGui::TextDisabled("avg mix");
    return false;
}

void Mixer::process(const float* const* sources, size_t source_count, float* destination,
                    size_t num_samples) const {
    if (destination == nullptr || num_samples == 0) {
        return;
    }
    std::fill(destination, destination + num_samples, 0.0f);
    if (sources == nullptr || source_count == 0) {
        return;
    }

    size_t connected = 0;
    for (size_t s = 0; s < source_count; ++s) {
        const float* src = sources[s];
        if (src == nullptr) {
            continue;
        }
        for (size_t i = 0; i < num_samples; ++i) {
            destination[i] += src[i];
        }
        ++connected;
    }

    if (connected > 0) {
        const float scale = 1.0f / static_cast<float>(connected);
        for (size_t i = 0; i < num_samples; ++i) {
            destination[i] *= scale;
        }
    }
}
