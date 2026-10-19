#pragma once

#include "module_json.hpp"

#include <atomic>
#include <cstddef>
#include <string>

class Preamp {
public:
    struct Params {
        float gain_db = 0.0f;

        static std::string name() { return "preamp"; }

        void write(std::string& out, bool& first) const;
        bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    };

    // samples: normalized interleaved stereo values, modified in place.
    // num_samples: number of float values, not stereo frames.
    // gain_db: 0 leaves level unchanged; positive boosts and negative attenuates.
    void process(float* samples, size_t num_samples, float gain_db);

    static void render(std::atomic<bool>& enabled, std::atomic<float>& gain_db,
                       int& selected_preset, bool& controls_changed);
};
