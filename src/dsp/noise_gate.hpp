#pragma once

#include <cstddef>

// Simple downward noise gate with attack/release smoothing.
class NoiseGate {
public:
    void process(float* interleaved, size_t sample_count, float threshold_db,
                 float attack_ms, float release_ms, float sample_rate);
    void reset();

private:
    float envelope_ = 0.0f;
    float gain_ = 0.0f;
};
