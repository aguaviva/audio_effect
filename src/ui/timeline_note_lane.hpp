#pragma once

#include <vector>

class Timeline;

// A note on a timeline track, in seconds.
struct TimelineNote {
    double start = 0.0;
    double duration = 0.0;
    int pitch = 60;
};

// Notes in the current track's lane, on a block from start to end (seconds), scaled to the
// lane's height; those under the cursor are lit.
void draw_notes(Timeline& timeline, const std::vector<TimelineNote>& notes, double start,
                double end);
