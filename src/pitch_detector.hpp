#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

class PitchDetector {
public:
    explicit PitchDetector(float sample_rate, float min_frequency = 70.0f,
                           float max_frequency = 1000.0f)
        : sample_rate_(sample_rate), min_frequency_(min_frequency), max_frequency_(max_frequency) {}

    void reset() {
        write_index_ = 0;
        filled_ = 0;
        frames_since_analysis_ = 0;
    }

    bool process(const float* interleaved, size_t sample_count, size_t channels,
                 float& frequency_hz) {
        if (interleaved == nullptr || channels == 0) {
            return false;
        }

        bool updated = false;
        for (size_t offset = 0; offset + channels <= sample_count; offset += channels) {
            float mono_sample = 0.0f;
            for (size_t channel = 0; channel < channels; ++channel) {
                mono_sample += interleaved[offset + channel];
            }
            mono_sample /= static_cast<float>(channels);

            ring_[write_index_] = mono_sample;
            write_index_ = (write_index_ + 1) % window_size;
            if (filled_ < window_size) {
                ++filled_;
            }
            ++frames_since_analysis_;

            if (filled_ == window_size && frames_since_analysis_ >= hop_size) {
                frequency_hz = estimate_pitch();
                frames_since_analysis_ = 0;
                updated = true;
            }
        }
        return updated;
    }

private:
    static constexpr size_t window_size = 2048;
    static constexpr size_t hop_size = 1024;
    static constexpr size_t maximum_lag = window_size / 2;

    float estimate_pitch() {
        double mean = 0.0;
        size_t source_index = write_index_;
        for (size_t i = 0; i < window_size; ++i) {
            window_[i] = ring_[source_index];
            mean += window_[i];
            source_index = (source_index + 1) % window_size;
        }
        mean /= static_cast<double>(window_size);

        double energy = 0.0;
        for (float& sample : window_) {
            sample -= static_cast<float>(mean);
            energy += static_cast<double>(sample) * sample;
        }
        double rms = std::sqrt(energy / static_cast<double>(window_size));
        if (rms < 0.003) {
            return 0.0f;
        }

        size_t min_lag = std::max<size_t>(2, static_cast<size_t>(sample_rate_ / max_frequency_));
        size_t max_lag = std::min(maximum_lag,
                                  static_cast<size_t>(sample_rate_ / min_frequency_));
        if (min_lag >= max_lag) {
            return 0.0f;
        }

        double cumulative_difference = 0.0;
        for (size_t lag = 1; lag <= max_lag; ++lag) {
            double difference = 0.0;
            for (size_t i = 0; i < window_size - lag; ++i) {
                double delta = static_cast<double>(window_[i]) - window_[i + lag];
                difference += delta * delta;
            }
            cumulative_difference += difference;
            cmnd_[lag] = cumulative_difference > 0.0
                ? difference * static_cast<double>(lag) / cumulative_difference
                : 1.0;
        }

        size_t best_lag = 0;
        for (size_t lag = min_lag; lag < max_lag; ++lag) {
            if (cmnd_[lag] < 0.15) {
                best_lag = lag;
                while (best_lag < max_lag && cmnd_[best_lag + 1] < cmnd_[best_lag]) {
                    ++best_lag;
                }
                break;
            }
        }

        if (best_lag == 0) {
            best_lag = min_lag;
            for (size_t lag = min_lag + 1; lag <= max_lag; ++lag) {
                if (cmnd_[lag] < cmnd_[best_lag]) {
                    best_lag = lag;
                }
            }
            if (cmnd_[best_lag] >= 0.25) {
                return 0.0f;
            }
        }

        double refined_lag = static_cast<double>(best_lag);
        if (best_lag > min_lag && best_lag < max_lag) {
            double left = cmnd_[best_lag - 1];
            double center = cmnd_[best_lag];
            double right = cmnd_[best_lag + 1];
            double curvature = left - 2.0 * center + right;
            if (std::abs(curvature) > 1.0e-12) {
                double offset = 0.5 * (left - right) / curvature;
                refined_lag += std::clamp(offset, -0.5, 0.5);
            }
        }

        return static_cast<float>(sample_rate_ / refined_lag);
    }

    static constexpr size_t window_size_for_array = 2048;
    static constexpr size_t maximum_lag_for_array = window_size_for_array / 2;

    float sample_rate_;
    float min_frequency_;
    float max_frequency_;
    std::array<float, window_size_for_array> ring_{};
    std::array<float, window_size_for_array> window_{};
    std::array<double, maximum_lag_for_array + 1> cmnd_{};
    size_t write_index_{0};
    size_t filled_{0};
    size_t frames_since_analysis_{0};
};
