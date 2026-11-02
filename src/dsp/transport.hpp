#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

// The one clock the timed sources share, and the timeline's cursor. The UI plays, pauses and
// seeks it; the audio thread applies that at the start of a block, so every source sees the
// same position for the whole block, then moves it on.
class Transport {
public:
    // UI thread.
    void play() { playing_request_ = true; }
    void pause() { playing_request_ = false; }
    void seek(int64_t frame) { seek_request_ = std::max<int64_t>(frame, 0); }
    bool playing() const { return playing_request_; }
    // A seek not applied yet counts as done.
    int64_t position() const {
        const int64_t seek = seek_request_;
        return seek >= 0 ? seek : frame_.load();
    }

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
    void end_block(int64_t frames) {
        if (playing_) {
            frame_ += frames;
        }
    }

private:
    std::atomic<bool> playing_request_{true};
    std::atomic<int64_t> seek_request_{-1};
    std::atomic<int64_t> frame_{0};
    bool playing_ = false;
};

inline Transport& transport() {
    static Transport instance;
    return instance;
}
