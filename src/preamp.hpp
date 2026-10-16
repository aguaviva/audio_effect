#pragma once

#include <atomic>
#include <cstddef>

class Preamp {
public:
    // samples: normalized interleaved stereo values, modified in place.
    // num_samples: number of float values, not stereo frames.
    // gain_db: 0 leaves level unchanged; positive boosts and negative attenuates.
    void process(float* samples, size_t num_samples, float gain_db);

    static void render(std::atomic<bool>& enabled, std::atomic<float>& gain_db,
                       int& selected_preset, bool& controls_changed);
};
