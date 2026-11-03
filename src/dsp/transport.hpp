#pragma once

#include <atomic>
#include <cstdint>

// The one clock the timed sources share. The UI plays, pauses and seeks it; the audio thread
// applies that at the start of a block, so every source renders the same position for the
// whole block, then moves it on. Rendered blocks wait in a queue before the device takes them,
// so the cursor is not that render position but the sink's (see SinkClock): the frame of the
// last sample handed to the device. Every member is lock-free.
class Transport {
public:
    // Where a rendered block starts and whether the transport moved through it.
    struct Stamp {
        int64_t frame = 0;
        bool playing = false;
    };

    // UI thread.
    void play();
    void pause();
    void seek(int64_t frame);
    bool playing() const;
    int64_t position() const;

    // Audio thread.
    void begin_block();
    bool block_playing() const;
    int64_t block_frame() const;
    Stamp block_stamp() const;
    void end_block(int64_t frames);

    // Whatever hands the rendered blocks to the device.
    void set_sink_frame(int64_t frame);

private:
    std::atomic<bool> playing_request_{true};
    std::atomic<int64_t> seek_request_{-1};
    std::atomic<int64_t> frame_{0};
    std::atomic<int64_t> sink_frame_{0};
    std::atomic<bool> playing_{false};
};

Transport& transport();
