#pragma once

#include "dsp/clip_player.hpp"
#include "dsp/module.hpp"
#include "model/clip_arrangement.hpp"
#include "model/wav_library.hpp"
#include "ui/clip_lane_editor.hpp"

#include <atomic>
#include <cstdint>
#include <string>

// WAV clips on channels, one output pin per channel, played by the shared transport. Each
// channel is a lane in the timeline holding any number of clips, from any files, which are
// placed, trimmed, copied and pasted there. Channels can be added and removed; links from the
// pins of removed channels are dropped (see AudioGraph::drop_stale_links()).
//
// Lock-free: the UI edits the arrangement and hands the audio thread a copy of it after each
// change, so the graph never takes this module's control mutex.
class MultiWavGenerator : public Module {
public:
    static constexpr int kDefaultChannels = 4;
    static constexpr int kMaxChannels = kMaxOutputs;

    MultiWavGenerator();

    std::string name() const override { return "multi_wav"; }
    void get_inputs(int* count, const char* const** str) const override;
    void get_outputs(int* count, const char* const** str) const override;
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void render_window() override;
    bool render_timeline(Timeline& timeline, const char* title) override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;
    bool needs_control_lock() const override { return false; }

    // UI thread.
    int channel_count() const { return arrangement_.channel_count(); }
    void set_channel_count(int channels);
    ClipArrangement& arrangement() { return arrangement_; }
    WavLibrary& library() { return library_; }
    // Hands the audio thread the arrangement if it changed since; call after editing it.
    void sync();

protected:
    // fixed: the channel count cannot be changed from the node. prefix names the channels in
    // the JSON ("<prefix>N_clips"); pin_names has kMaxChannels entries.
    MultiWavGenerator(int channels, bool fixed, const char* prefix, const char* lane_name,
                      const char* const* pin_names);

private:
    const bool fixed_;
    const char* const prefix_;
    const char* const lane_name_;
    const char* const* const pin_names_;

    // UI thread.
    ClipArrangement arrangement_;
    WavLibrary library_{kSampleRate};
    ClipLaneEditor editor_{kSampleRate};
    uint64_t synced_revision_ = UINT64_MAX;

    ClipPlayer player_;
    std::atomic<int> outputs_; // the channel count, read by the audio thread
};
