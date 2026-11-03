#pragma once

#include <cstdint>

class ClipArrangement;
class TempoGrid;

// Snapping for clip edits, as the piano roll snaps notes: every edge goes to the nearest grid
// line. Another clip's edge wins over the grid when it is closer and within tolerance frames,
// so clips can still be butted together off the grid.
struct ClipSnap {
    const ClipArrangement& arrangement;
    const TempoGrid& grid;
    uint32_t sample_rate;
    int64_t tolerance = 0; // frames

    // The clip at (channel, index), if any, is not snapped to.
    int64_t edge(int64_t frame, int channel = -1, int index = -1) const;
    // A start for a clip of length frames that puts its start, or its end, on an edge;
    // whichever moves it least.
    int64_t start(int64_t start, int64_t length, int channel = -1, int index = -1) const;
};
