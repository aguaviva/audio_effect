#pragma once

#include <alsa/asoundlib.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "wav_generator.hpp"

enum class InputSource : int {
    live_audio_in,
    wav_file,
    chord_generator
};

inline constexpr std::array<std::array<double, 6>, 3> CHORD_NOTE_FREQUENCIES = {{
    {{82.41, 123.47, 164.81, 207.65, 246.94, 329.63}},
    {{110.00, 164.81, 220.00, 261.63, 329.63, 440.00}},
    {{98.00, 146.83, 196.00, 246.94, 293.66, 392.00}}
}};

inline size_t full_chord_period_samples(int chord, uint32_t sample_rate) {
    chord = std::clamp(chord, 0, static_cast<int>(CHORD_NOTE_FREQUENCIES.size() - 1));
    static const std::array<size_t, 3> periods = [sample_rate]() {
        std::array<size_t, 3> result{};
        constexpr size_t max_root_cycles = 8192;
        // Equal-tempered notes have no exact shared period; align every note within this phase tolerance.
        constexpr double phase_tolerance_cycles = 0.05;

        for (size_t chord_index = 0; chord_index < result.size(); ++chord_index) {
            double root_frequency = CHORD_NOTE_FREQUENCIES[chord_index][0];
            double best_error = 1.0;
            size_t best_period = static_cast<size_t>(std::llround(sample_rate / root_frequency));

            for (size_t root_cycles = 1; root_cycles <= max_root_cycles; ++root_cycles) {
                size_t candidate = static_cast<size_t>(std::llround(
                    root_cycles * static_cast<double>(sample_rate) / root_frequency));
                double worst_note_error = 0.0;
                for (double frequency : CHORD_NOTE_FREQUENCIES[chord_index]) {
                    double cycles = frequency * candidate / sample_rate;
                    double fractional_cycle = cycles - std::round(cycles);
                    worst_note_error = std::max(worst_note_error, std::abs(fractional_cycle));
                }

                if (worst_note_error < best_error) {
                    best_error = worst_note_error;
                    best_period = candidate;
                }
                if (worst_note_error <= phase_tolerance_cycles) {
                    best_period = candidate;
                    break;
                }
            }
            result[chord_index] = best_period;
        }
        return result;
    }();
    return periods[chord];
}

class LiveAudioInput {
public:
    bool read_period(snd_pcm_t* capture_handle, int32_t* destination,
                     snd_pcm_uframes_t frames, std::atomic<uint64_t>& xrun_count,
                     int& failed_read, int& recovery_error) {
        int result;
        while ((result = static_cast<int>(snd_pcm_readi(capture_handle, destination, frames))) < 0) {
            if (result == -EPIPE || result == -ESTRPIPE) {
                xrun_count.fetch_add(1, std::memory_order_relaxed);
            }

            int err = snd_pcm_recover(capture_handle, result, 0);
            if (err < 0) {
                failed_read = result;
                recovery_error = err;
                return false;
            }
        }
        return true;
    }
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
        chord = std::clamp(chord, 0, 2);
        if (chord != active_chord_) {
            phases_.fill(0.0);
            active_chord_ = chord;
        }

        constexpr double pcm_scale = 2147483647.0;
        constexpr double two_pi = 6.28318530717958647692;

        for (size_t frame = 0; frame < frames; ++frame) {
            double mixed_sample = 0.0;
            for (size_t note = 0; note < phases_.size(); ++note) {
                mixed_sample += std::sin(phases_[note]);
                phases_[note] += two_pi * CHORD_NOTE_FREQUENCIES[chord][note] / sample_rate;
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
    std::array<double, 6> phases_{};
    int active_chord_{-1};
};

class AudioInputSources {
public:
    AudioInputSources(snd_pcm_uframes_t period_frames, size_t channels)
        : channels_(channels), capture_discard_(static_cast<size_t>(period_frames) * channels) {}

    bool read_period(InputSource source, const std::shared_ptr<const WavClip>& clip,
                     bool looping, int chord, snd_pcm_t* capture_handle,
                     int32_t* destination, snd_pcm_uframes_t frames,
                     uint32_t sample_rate, std::atomic<uint64_t>& xrun_count,
                     int& failed_read, int& recovery_error) {
        bool wav_available = clip && clip->frame_count() > 0;
        if (source == InputSource::live_audio_in || (source == InputSource::wav_file && !wav_available)) {
            wav_.reset();
            chord_.reset();
            return live_.read_period(capture_handle, destination, frames,
                                     xrun_count, failed_read, recovery_error);
        }

        if (!live_.read_period(capture_handle, capture_discard_.data(), frames,
                               xrun_count, failed_read, recovery_error)) {
            return false;
        }

        if (source == InputSource::wav_file) {
            chord_.reset();
            wav_.render(clip, looping, destination, frames);
        } else {
            wav_.reset();
            chord_.render(chord, destination, frames, channels_, sample_rate);
        }
        return true;
    }

private:
    size_t channels_;
    std::vector<int32_t> capture_discard_;
    LiveAudioInput live_;
    WavFileInput wav_;
    ChordGeneratorInput chord_;
};
