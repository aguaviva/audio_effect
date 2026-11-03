#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

// The one clock the timed sources share. The UI plays, pauses and seeks it; the audio thread
// applies that at the start of a block, so every source renders the same position for the
// whole block, then moves it on. Rendered blocks wait in a queue before the device takes them,
// so the cursor is not that render position but the sink's: the frame of the last sample
// handed to the device.
class Transport {
public:
    // Where a rendered block starts and whether the transport moved through it.
    struct Stamp {
        int64_t frame = 0;
        bool playing = false;
    };

    // UI thread.
    void play() { playing_request_ = true; }
    void pause() { playing_request_ = false; }
    void seek(int64_t frame) { seek_request_ = std::max<int64_t>(frame, 0); }
    bool playing() const { return playing_request_; }
    int64_t position() const { return sink_frame_; }

    // Audio thread.
    void begin_block() {
        playing_ = playing_request_;
        const int64_t seek = seek_request_.exchange(-1);
        if (seek >= 0) {
            frame_ = seek;
        }
    }
    bool block_playing() const { return playing_; }
    int64_t block_frame() const { return frame_; }
    Stamp block_stamp() const { return {frame_, playing_}; }
    void end_block(int64_t frames) {
        if (playing_) {
            frame_ += frames;
        }
    }

    // Whatever hands the rendered blocks to the device.
    void set_sink_frame(int64_t frame) { sink_frame_ = frame; }

private:
    std::atomic<bool> playing_request_{true};
    std::atomic<int64_t> seek_request_{-1};
    std::atomic<int64_t> frame_{0};
    std::atomic<int64_t> sink_frame_{0};
    bool playing_ = false;
};

inline Transport& transport() {
    static Transport instance;
    return instance;
}

// Follows the rendered blocks waiting for the device and moves the transport's sink position
// as the device takes their samples. Used from the one thread that feeds the device.
class SinkClock {
public:
    SinkClock() { blocks_.reserve(64); }

    void queued(Transport::Stamp stamp, size_t frames) {
        if (frames > 0) {
            blocks_.push_back({stamp, frames, 0});
        }
    }

    void consumed(size_t frames) {
        bool moved = false;
        int64_t frame = 0;
        size_t done = 0;
        while (frames > 0 && done < blocks_.size()) {
            Block& block = blocks_[done];
            const size_t n = std::min(frames, block.frames - block.used);
            block.used += n;
            frames -= n;
            frame = block.stamp.frame +
                    (block.stamp.playing ? static_cast<int64_t>(block.used) : 0);
            moved = true;
            if (block.used == block.frames) {
                ++done;
            }
        }
        blocks_.erase(blocks_.begin(), blocks_.begin() + static_cast<std::ptrdiff_t>(done));
        if (moved) {
            transport().set_sink_frame(frame);
        }
    }

    void clear() { blocks_.clear(); }

private:
    struct Block {
        Transport::Stamp stamp;
        size_t frames;
        size_t used;
    };
    std::vector<Block> blocks_;
};
