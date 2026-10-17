#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "chord_catalog.hpp"
#include "wav_generator.hpp"

enum class InputSource : int {
    live_audio_in,
    wav_file,
    chord_generator
};

class WavFileInput {
public:
    void render(const std::shared_ptr<const WavClip>& clip, bool looping,
                int32_t* destination, size_t frames) {
        if (clip != active_clip_) {
            active_clip_ = clip;
            frame_cursor_ = 0;
        }

        size_t frames_written = 0;
        while (frames_written < frames) {
            if (frame_cursor_ >= clip->frame_count()) {
                if (looping) {
                    frame_cursor_ = 0;
                } else {
                    break;
                }
            }

            size_t available = clip->frame_count() - frame_cursor_;
            size_t frames_to_copy = std::min(frames - frames_written, available);
            std::memcpy(destination + frames_written * 2,
                        clip->interleaved_stereo.data() + frame_cursor_ * 2,
                        frames_to_copy * 2 * sizeof(int32_t));
            frames_written += frames_to_copy;
            frame_cursor_ += frames_to_copy;
        }

        if (frames_written < frames) {
            std::fill(destination + frames_written * 2, destination + frames * 2, 0);
        }
    }

    void reset() {
        active_clip_.reset();
        frame_cursor_ = 0;
    }

private:
    std::shared_ptr<const WavClip> active_clip_;
    size_t frame_cursor_{0};
};

class ChordGeneratorInput {
public:
    void render(int chord, int32_t* destination, size_t frames, size_t channels,
                uint32_t sample_rate) {
        chord = std::clamp(chord, 0, kChordCount - 1);
        if (chord != active_chord_) {
            phases_.fill(0.0);
            active_chord_ = chord;
            frequencies_ = chord_note_frequencies(chord);
        }

        constexpr double pcm_scale = 2147483647.0;
        constexpr double two_pi = 6.28318530717958647692;

        for (size_t frame = 0; frame < frames; ++frame) {
            double mixed_sample = 0.0;
            for (size_t note = 0; note < phases_.size(); ++note) {
                mixed_sample += std::sin(phases_[note]);
                phases_[note] += two_pi * frequencies_[note] / sample_rate;
                if (phases_[note] >= two_pi) {
                    phases_[note] -= two_pi;
                }
            }
            int32_t sample = static_cast<int32_t>(mixed_sample * (0.90 / phases_.size()) * pcm_scale);
            for (size_t channel = 0; channel < channels; ++channel) {
                destination[frame * channels + channel] = sample;
            }
        }
    }

    void reset() {
        phases_.fill(0.0);
        active_chord_ = -1;
    }

private:
    std::array<double, kChordVoices> phases_{};
    std::array<double, kChordVoices> frequencies_{};
    int active_chord_{-1};
};
