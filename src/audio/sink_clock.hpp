#pragma once

#include "dsp/transport.hpp"

#include <array>
#include <cstddef>

// Follows the rendered blocks waiting for the device and moves the transport's sink position
// as the device takes their samples. Used from the one thread that feeds the device; it never
// allocates, so it is safe in the device callback.
class SinkClock {
public:
    // More blocks than the device can hold queued; past that the oldest is dropped.
    static constexpr size_t kCapacity = 64;

    void queued(Transport::Stamp stamp, size_t frames);
    void consumed(size_t frames);
    void clear();

private:
    struct Block {
        Transport::Stamp stamp;
        size_t frames = 0;
        size_t used = 0;
    };
    std::array<Block, kCapacity> blocks_{};
    size_t first_ = 0;
    size_t count_ = 0;
};
