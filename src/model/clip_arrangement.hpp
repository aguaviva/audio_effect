#pragma once

#include "dsp/wav_generator.hpp"
#include "model/waveform_peaks.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class WavLibrary;

// Frames [offset, offset + length) of a WAV file, played from start on (all in frames).
// Trimming only moves those bounds; the file is untouched.
struct ArrangedClip {
    std::string path;
    int64_t start = 0;
    int64_t offset = 0;
    int64_t length = -1; // to the end of the file
    std::shared_ptr<const WavClip> wav;
    std::shared_ptr<const WaveformPeaks> peaks;

    // What it plays: nothing while its file is missing.
    int64_t frames() const;
    int64_t end() const { return start + frames(); }
};

// Channels of clips placed in time; a channel holds any number of clips from any files, and
// where they overlap they mix. Indices are into clips(channel), in the order clips were added
// (later ones are on top). UI thread only: players get a copy (see ClipPlayer).
class ClipArrangement {
public:
    explicit ClipArrangement(int channels);

    int channel_count() const { return static_cast<int>(channels_.size()); }
    // Removing channels drops their clips.
    void set_channel_count(int channels);
    const std::vector<ArrangedClip>& clips(int channel) const { return channels_[channel].clips; }
    // Why the last file loaded into the channel failed, or empty.
    const std::string& error(int channel) const { return channels_[channel].error; }
    void set_error(int channel, std::string error);

    int add(int channel, ArrangedClip clip);
    void move(int channel, int index, int64_t start);
    // Moves one edge, within the file and leaving at least a frame.
    void trim_start(int channel, int index, int64_t start);
    void trim_end(int channel, int index, int64_t end);
    void remove(int channel, int index);
    // A copy straight after the clip; its index.
    int duplicate(int channel, int index);
    void clear();

    // Changes on every edit, so owners know when to hand players a new copy.
    uint64_t revision() const { return revision_; }

    // "<prefix>N_clips":[...] members for each channel with clips, appended to an object.
    void save_members(std::string& out, const char* prefix) const;
    // Reads those members; a missing file keeps its clip (with the channel's error set), so
    // saving again does not lose it. Leaves channels not in json empty.
    void load_members(const std::string& json, const char* prefix, WavLibrary& library);
    static std::string channel_key(const char* prefix, int channel, const char* field);

private:
    struct Channel {
        std::vector<ArrangedClip> clips;
        std::string error;
    };
    std::vector<Channel> channels_;
    uint64_t revision_ = 0;
};
