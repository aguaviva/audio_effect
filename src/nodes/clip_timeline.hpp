#pragma once

#include "nodes/multi_wav_generator.hpp"

// The first clip node: a Multi WAV Generator with six fixed channels, called tracks, kept so
// patches that use it ("timeline" nodes) still load. Patches from before a track held several
// clips (trackN_path, trackN_start) load as one whole file per track.
class ClipTimeline : public MultiWavGenerator {
public:
    static constexpr int kTracks = 6;

    ClipTimeline();

    std::string name() const override { return "timeline"; }
    bool load(const std::string& json) override;
};
