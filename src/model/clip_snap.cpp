#include "clip_snap.hpp"

#include "model/clip_arrangement.hpp"
#include "model/tempo_grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

int64_t ClipSnap::edge(int64_t frame, int channel, int index) const {
    const double seconds = static_cast<double>(frame) / sample_rate;
    int64_t snapped = std::llround(grid.snap_seconds(seconds) * sample_rate);
    int64_t best = std::min(std::abs(snapped - frame), tolerance + 1);
    for (int ch = 0; ch < arrangement.channel_count(); ++ch) {
        const auto& clips = arrangement.clips(ch);
        for (int i = 0; i < static_cast<int>(clips.size()); ++i) {
            if ((ch == channel && i == index) || clips[i].frames() == 0) {
                continue;
            }
            for (int64_t e : {clips[i].start, clips[i].end()}) {
                if (std::abs(e - frame) <= tolerance && std::abs(e - frame) < best) {
                    best = std::abs(e - frame);
                    snapped = e;
                }
            }
        }
    }
    return std::max<int64_t>(snapped, 0);
}

int64_t ClipSnap::start(int64_t start, int64_t length, int channel, int index) const {
    const int64_t by_start = edge(start, channel, index);
    const int64_t by_end = edge(start + length, channel, index) - length;
    if (by_end >= 0 && std::abs(by_end - start) < std::abs(by_start - start)) {
        return by_end;
    }
    return by_start;
}
