#include "waveform_peaks.hpp"

#include <algorithm>

WaveformPeaks::WaveformPeaks(const WavClip& clip) {
    const std::vector<float>& s = clip.interleaved_stereo;
    const size_t frames = s.size() / 2;
    std::vector<std::pair<float, float>> level;
    for (size_t f = 0; f < frames; f += kBaseFrames) {
        const size_t end = std::min(frames, f + kBaseFrames);
        auto [lo, hi] = std::minmax_element(s.begin() + 2 * f, s.begin() + 2 * end);
        level.emplace_back(*lo, *hi);
    }
    while (!level.empty()) {
        levels.push_back(level);
        if (level.size() == 1) {
            break;
        }
        std::vector<std::pair<float, float>> coarser;
        for (size_t i = 0; i < level.size(); i += 2) {
            const auto& b = level[std::min(i + 1, level.size() - 1)];
            coarser.emplace_back(std::min(level[i].first, b.first),
                                 std::max(level[i].second, b.second));
        }
        level = std::move(coarser);
    }
}

std::pair<float, float> WaveformPeaks::range(const WavClip& clip, size_t begin, size_t end) const {
    const std::vector<float>& s = clip.interleaved_stereo;
    end = std::min(end, s.size() / 2);
    if (begin >= end) {
        return {0.0f, 0.0f};
    }
    if (end - begin < kBaseFrames || levels.empty()) {
        auto [lo, hi] = std::minmax_element(s.begin() + 2 * begin, s.begin() + 2 * end);
        return {*lo, *hi};
    }
    // The coarsest level whose buckets still fit in the range: at most a few buckets to read.
    size_t level = 0;
    while (level + 1 < levels.size() && (kBaseFrames << (level + 1)) <= end - begin) {
        ++level;
    }
    const size_t bucket = kBaseFrames << level;
    const auto& peaks = levels[level];
    std::pair<float, float> out = peaks[begin / bucket];
    for (size_t i = begin / bucket + 1; i <= (end - 1) / bucket && i < peaks.size(); ++i) {
        out.first = std::min(out.first, peaks[i].first);
        out.second = std::max(out.second, peaks[i].second);
    }
    return out;
}
