#pragma once

class Timeline;

// The row that names a node's group of tracks, as the Midi Generator and the WAV generators
// draw above their lanes: the node's title, a tooltip, and an optional dim note after it.
void timeline_title_row(Timeline& timeline, const char* title, const char* tooltip,
                        const char* note = nullptr);
