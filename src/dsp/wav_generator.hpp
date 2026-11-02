#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct WavClip {
    std::vector<float> interleaved_stereo;
};

class WavGenerator {
public:
    // Decodes to interleaved stereo float at target_sample_rate (mono is duplicated).
    static std::shared_ptr<const WavClip> load(const std::string& path,
                                               uint32_t target_sample_rate,
                                               std::string& error);
};
