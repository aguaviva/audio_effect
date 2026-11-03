#include "transport.hpp"

#include <algorithm>

namespace {
constexpr auto kRelaxed = std::memory_order_relaxed;
}

void Transport::play() { playing_request_.store(true, kRelaxed); }

void Transport::pause() { playing_request_.store(false, kRelaxed); }

void Transport::seek(int64_t frame) { seek_request_.store(std::max<int64_t>(frame, 0), kRelaxed); }

bool Transport::playing() const { return playing_request_.load(kRelaxed); }

int64_t Transport::position() const { return sink_frame_.load(kRelaxed); }

void Transport::begin_block() {
    playing_.store(playing_request_.load(kRelaxed), kRelaxed);
    const int64_t seek = seek_request_.exchange(-1, kRelaxed);
    if (seek >= 0) {
        frame_.store(seek, kRelaxed);
    }
}

bool Transport::block_playing() const { return playing_.load(kRelaxed); }

int64_t Transport::block_frame() const { return frame_.load(kRelaxed); }

Transport::Stamp Transport::block_stamp() const { return {block_frame(), block_playing()}; }

void Transport::end_block(int64_t frames) {
    if (block_playing()) {
        frame_.fetch_add(frames, kRelaxed);
    }
}

void Transport::set_sink_frame(int64_t frame) { sink_frame_.store(frame, kRelaxed); }

Transport& transport() {
    static Transport instance;
    return instance;
}
