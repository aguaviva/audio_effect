#include "sink_clock.hpp"

#include <algorithm>

void SinkClock::queued(Transport::Stamp stamp, size_t frames) {
    if (frames == 0) {
        return;
    }
    if (count_ == kCapacity) {
        first_ = (first_ + 1) % kCapacity;
        --count_;
    }
    blocks_[(first_ + count_) % kCapacity] = {stamp, frames, 0};
    ++count_;
}

void SinkClock::consumed(size_t frames) {
    bool moved = false;
    int64_t frame = 0;
    while (frames > 0 && count_ > 0) {
        Block& block = blocks_[first_];
        const size_t n = std::min(frames, block.frames - block.used);
        block.used += n;
        frames -= n;
        frame = block.stamp.frame + (block.stamp.playing ? static_cast<int64_t>(block.used) : 0);
        moved = true;
        if (block.used == block.frames) {
            first_ = (first_ + 1) % kCapacity;
            --count_;
        }
    }
    if (moved) {
        transport().set_sink_frame(frame);
    }
}

void SinkClock::clear() {
    first_ = 0;
    count_ = 0;
}
