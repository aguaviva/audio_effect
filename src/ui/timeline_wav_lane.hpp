#pragma once

#include <cstddef>
#include <cstdint>

class Timeline;
struct WavClip;
struct WaveformPeaks;

// The part of a WAV a timeline clip shows, and how it looks.
struct ClipStyle {
    size_t offset = 0;           // first frame of the file shown
    size_t length = SIZE_MAX;    // frames shown, at most to the end of the file
    const char* label = nullptr; // in the clip's top left corner
    bool highlight = false;      // being dragged
    bool selected = false;       // outlined
    // A repeat of a looping clip: it does not lengthen the scroll range, so it can be drawn
    // over whatever is visible.
    bool repeat = false;
};

// A waveform in the current track's lane, from start (seconds) on.
void draw_wav_clip(Timeline& timeline, const WavClip& clip, const WaveformPeaks& peaks,
                   double start, double sample_rate, const ClipStyle& style = {});
