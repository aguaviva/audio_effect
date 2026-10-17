#pragma once

#include "node_types.hpp"

#include "dsp/pitch_detector.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

inline constexpr int kVisualizerScopePoints = 128;
inline constexpr int kVisualizerSpectrumBins = 32;

// Thread-safe probe store: audio thread publishes analysis; UI thread reads.
// Visualizer nodes are pass-through (same block_size in → out, audio unchanged).
class VisualizerBus {
public:
    void analyze(int node_id, NodeType type, const float* interleaved_block,
                 size_t block_size, float sample_rate, float display_gain, float vu_decay);

    bool read_vu(int node_id, float& peak, float& rms) const;
    bool read_scope(int node_id, float* destination, int count) const;
    bool read_spectrum(int node_id, float* destination, int count) const;
    bool read_pitch(int node_id, float& frequency_hz) const;

    void retain_only(const std::vector<int>& alive_ids);

private:
    struct Slot {
        std::atomic<float> peak{0.0f};
        std::atomic<float> rms{0.0f};
        float scope[2][kVisualizerScopePoints]{};
        float spectrum[2][kVisualizerSpectrumBins]{};
        std::atomic<int> scope_ready{0};
        std::atomic<int> spectrum_ready{0};
        int scope_write = 0;
        int spectrum_write = 0;
        float peak_ballistics = 0.0f;
        float rms_ballistics = 0.0f;

        std::unique_ptr<PitchDetector> pitch;
        float pitch_sample_rate = 0.0f;
        std::atomic<float> pitch_hz{0.0f};
    };

    Slot& mutable_slot(int node_id);
    const Slot* find_slot(int node_id) const;

    mutable std::mutex mutex_;
    std::unordered_map<int, std::unique_ptr<Slot>> slots_;
};

extern VisualizerBus g_visualizer_bus;
