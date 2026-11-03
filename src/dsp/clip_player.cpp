#include "clip_player.hpp"

#include <algorithm>

void ClipPlayer::publish(ClipSchedule schedule) {
    handoff_.publish(std::make_unique<const ClipSchedule>(std::move(schedule)));
}

void ClipPlayer::collect() { handoff_.collect(); }

void ClipPlayer::render(float* const* outputs, int output_count, int64_t frame, size_t frames) {
    for (int c = 0; c < output_count; ++c) {
        if (outputs[c]) {
            std::fill(outputs[c], outputs[c] + 2 * frames, 0.0f);
        }
    }
    const ClipSchedule* schedule = handoff_.acquire();
    if (!schedule) {
        return;
    }
    const int64_t block_end = frame + static_cast<int64_t>(frames);
    const int channels = std::min(output_count, static_cast<int>(schedule->channels.size()));
    for (int c = 0; c < channels; ++c) {
        float* out = outputs[c];
        if (!out) {
            continue;
        }
        for (const ClipSchedule::Clip& clip : schedule->channels[c]) {
            const int64_t begin = std::max(frame, clip.start);
            const int64_t end = std::min(block_end, clip.start + clip.frames);
            if (begin >= end) {
                continue;
            }
            const float* from =
                clip.wav->interleaved_stereo.data() + 2 * (clip.offset + begin - clip.start);
            float* to = out + 2 * (begin - frame);
            for (int64_t i = 0; i < 2 * (end - begin); ++i) {
                to[i] += from[i];
            }
        }
    }
}
