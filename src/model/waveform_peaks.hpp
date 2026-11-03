#pragma once

#include "dsp/wav_generator.hpp"

#include <cstddef>
#include <utility>
#include <vector>

// Min/max envelope of a clip (both channels together) at 64, 128, 256... frames per bucket,
// so drawing a waveform reads only a few values per pixel at any zoom.
struct WaveformPeaks {
    static constexpr size_t kBaseFrames = 64;
    std::vector<std::vector<std::pair<float, float>>> levels;

    explicit WaveformPeaks(const WavClip& clip);
    // Min and max over frames [begin, end), widened to whole buckets.
    std::pair<float, float> range(const WavClip& clip, size_t begin, size_t end) const;
};
