#pragma once

#include "module_json.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

// Lightweight stereo Schroeder-style reverb (comb + allpass).
class SimpleReverb {
public:
    static std::string name() { return "reverb"; }

    struct Params {
        float room = 0.55f;
        float damping = 0.4f;
        float mix = 0.25f;

        void write(std::string& out, bool& first) const;
        bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    };

    SimpleReverb();
    void process(float* interleaved, size_t sample_count, float room_size, float damping,
                 float mix, float sample_rate);
    void reset();

private:
    struct Comb {
        std::vector<float> buffer;
        size_t index = 0;
        float filter = 0.0f;
        float process(float input, float feedback, float damp);
    };
    struct Allpass {
        std::vector<float> buffer;
        size_t index = 0;
        float process(float input);
    };

    void ensure_buffers(float sample_rate);

    std::array<Comb, 4> combs_l_{};
    std::array<Comb, 4> combs_r_{};
    std::array<Allpass, 2> allpass_l_{};
    std::array<Allpass, 2> allpass_r_{};
    float configured_sr_ = 0.0f;
};
