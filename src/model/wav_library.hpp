#pragma once

#include "dsp/wav_generator.hpp"
#include "model/waveform_peaks.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>

// WAV files decoded once and shared by every clip that uses them, with their waveform
// envelopes. UI thread only.
class WavLibrary {
public:
    struct Entry {
        std::shared_ptr<const WavClip> wav;
        std::shared_ptr<const WaveformPeaks> peaks;
        std::string error; // why wav is null
    };

    explicit WavLibrary(uint32_t sample_rate) : sample_rate_(sample_rate) {}
    // Decodes path the first time, and again after a failure.
    const Entry& get(const std::string& path);

private:
    uint32_t sample_rate_;
    std::map<std::string, Entry> entries_;
};
