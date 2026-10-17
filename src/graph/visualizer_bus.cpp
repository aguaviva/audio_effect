#include "visualizer_bus.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

VisualizerBus g_visualizer_bus;

namespace {

constexpr float kPi = 3.14159265358979323846f;

void downmix_mono(const float* interleaved, size_t block_size, std::vector<float>& mono) {
    const size_t frames = block_size / 2;
    mono.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        mono[i] = 0.5f * (interleaved[i * 2] + interleaved[i * 2 + 1]);
    }
}

void fill_scope(const std::vector<float>& mono, float* destination, float gain) {
    const size_t frames = mono.size();
    if (frames == 0) {
        std::fill(destination, destination + kVisualizerScopePoints, 0.0f);
        return;
    }
    for (int i = 0; i < kVisualizerScopePoints; ++i) {
        size_t index = static_cast<size_t>(i) * (frames - 1) / (kVisualizerScopePoints - 1);
        destination[i] = std::clamp(mono[index] * gain, -1.5f, 1.5f);
    }
}

void fill_spectrum(const std::vector<float>& mono, float* bins, float gain) {
    constexpr size_t kN = 256;
    float windowed[kN]{};
    const size_t n = std::min(mono.size(), kN);
    if (n < 2) {
        std::fill(bins, bins + kVisualizerSpectrumBins, 0.0f);
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        const float w = 0.5f * (1.0f - std::cos(2.0f * kPi * static_cast<float>(i) /
                                                 static_cast<float>(n - 1)));
        windowed[i] = mono[i] * w;
    }

    // Cheap linear DFT bins (k = 1..32) — enough for a small in-node spectrum.
    for (int b = 0; b < kVisualizerSpectrumBins; ++b) {
        const int k = b + 1;
        float re = 0.0f;
        float im = 0.0f;
        for (size_t i = 0; i < kN; ++i) {
            const float angle = 2.0f * kPi * static_cast<float>(k * static_cast<int>(i)) /
                                static_cast<float>(kN);
            re += windowed[i] * std::cos(angle);
            im -= windowed[i] * std::sin(angle);
        }
        const float mag = std::sqrt(re * re + im * im) / static_cast<float>(kN);
        bins[b] = std::min(mag * gain * 8.0f, 1.0f);
    }
}

} // namespace

VisualizerBus::Slot& VisualizerBus::mutable_slot(int node_id) {
    auto it = slots_.find(node_id);
    if (it == slots_.end()) {
        it = slots_.emplace(node_id, std::make_unique<Slot>()).first;
    }
    return *it->second;
}

const VisualizerBus::Slot* VisualizerBus::find_slot(int node_id) const {
    auto it = slots_.find(node_id);
    return it == slots_.end() ? nullptr : it->second.get();
}

void VisualizerBus::analyze(int node_id, NodeType type, const float* interleaved_block,
                            size_t block_size, float sample_rate, float display_gain,
                            float vu_decay) {
    if (!node_is_visualizer(type) || block_size < 2 || (block_size % 2) != 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Slot& slot = mutable_slot(node_id);

    if (type == NodeType::pitch_detector) {
        if (!slot.pitch || slot.pitch_sample_rate != sample_rate) {
            slot.pitch = std::make_unique<PitchDetector>(sample_rate);
            slot.pitch_sample_rate = sample_rate;
        }
        float frequency_hz = 0.0f;
        if (slot.pitch->process(interleaved_block, block_size, 2, frequency_hz)) {
            slot.pitch_hz.store(frequency_hz, std::memory_order_relaxed);
        }
        return;
    }

    std::vector<float> mono;
    downmix_mono(interleaved_block, block_size, mono);

    float peak = 0.0f;
    double sum_sq = 0.0;
    for (float s : mono) {
        peak = std::max(peak, std::abs(s));
        sum_sq += static_cast<double>(s) * static_cast<double>(s);
    }
    const float rms = mono.empty()
                          ? 0.0f
                          : static_cast<float>(std::sqrt(sum_sq / static_cast<double>(mono.size())));

    const float decay = std::clamp(vu_decay > 0.0f ? vu_decay : 0.85f, 0.5f, 0.99f);
    slot.peak_ballistics = std::max(peak, slot.peak_ballistics * decay);
    slot.rms_ballistics = std::max(rms, slot.rms_ballistics * decay);
    slot.peak.store(slot.peak_ballistics, std::memory_order_relaxed);
    slot.rms.store(slot.rms_ballistics, std::memory_order_relaxed);

    const float gain = display_gain > 0.0f ? display_gain : 1.0f;

    if (type == NodeType::oscilloscope || type == NodeType::vu_meter) {
        // VU also keeps a tiny scope for optional sparkline.
        fill_scope(mono, slot.scope[slot.scope_write], gain);
        const int completed = slot.scope_write;
        slot.scope_write = 1 - slot.scope_write;
        slot.scope_ready.store(completed, std::memory_order_release);
    }

    if (type == NodeType::spectrum) {
        fill_spectrum(mono, slot.spectrum[slot.spectrum_write], gain);
        const int completed = slot.spectrum_write;
        slot.spectrum_write = 1 - slot.spectrum_write;
        slot.spectrum_ready.store(completed, std::memory_order_release);
    }
}

bool VisualizerBus::read_vu(int node_id, float& peak, float& rms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Slot* slot = find_slot(node_id);
    if (!slot) {
        return false;
    }
    peak = slot->peak.load(std::memory_order_relaxed);
    rms = slot->rms.load(std::memory_order_relaxed);
    return true;
}

bool VisualizerBus::read_scope(int node_id, float* destination, int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Slot* slot = find_slot(node_id);
    if (!slot || count <= 0) {
        return false;
    }
    const int src = slot->scope_ready.load(std::memory_order_acquire);
    const int n = std::min(count, kVisualizerScopePoints);
    std::memcpy(destination, slot->scope[src], static_cast<size_t>(n) * sizeof(float));
    return true;
}

bool VisualizerBus::read_spectrum(int node_id, float* destination, int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Slot* slot = find_slot(node_id);
    if (!slot || count <= 0) {
        return false;
    }
    const int src = slot->spectrum_ready.load(std::memory_order_acquire);
    const int n = std::min(count, kVisualizerSpectrumBins);
    std::memcpy(destination, slot->spectrum[src], static_cast<size_t>(n) * sizeof(float));
    return true;
}

bool VisualizerBus::read_pitch(int node_id, float& frequency_hz) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const Slot* slot = find_slot(node_id);
    if (!slot) {
        return false;
    }
    frequency_hz = slot->pitch_hz.load(std::memory_order_relaxed);
    return true;
}

void VisualizerBus::retain_only(const std::vector<int>& alive_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unordered_map<int, bool> alive;
    for (int id : alive_ids) {
        alive[id] = true;
    }
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (!alive.count(it->first)) {
            it = slots_.erase(it);
        } else {
            ++it;
        }
    }
}
