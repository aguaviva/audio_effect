#pragma once

#include "module_json.hpp"

#include <cstddef>
#include <string>

// Simple downward noise gate with attack/release smoothing.
class NoiseGate {
public:
    static std::string name() { return "noise_gate"; }

    struct Params {
        float threshold_db = -40.0f;
        float attack_ms = 5.0f;
        float release_ms = 80.0f;

        void write(std::string& out, bool& first) const;
        bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    };

    void process(float* interleaved, size_t sample_count, float threshold_db,
                 float attack_ms, float release_ms, float sample_rate);
    void reset();

private:
    float envelope_ = 0.0f;
    float gain_ = 0.0f;
};
