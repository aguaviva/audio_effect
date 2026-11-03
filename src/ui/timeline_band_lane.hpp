#pragma once

class Timeline;

// A signal without a length, across the current track's whole lane; dim while inactive.
void draw_band(Timeline& timeline, const char* label, bool active);
