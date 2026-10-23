#pragma once

#include "miniaudio.h"
#include "module.hpp"
#include "paths.hpp"

#include <atomic>
#include <cstddef>
#include <string>

class WavRecorderEngine {
public:
    WavRecorderEngine() = default;
    WavRecorderEngine(const WavRecorderEngine&) = delete;
    WavRecorderEngine& operator=(const WavRecorderEngine&) = delete;
    ~WavRecorderEngine();

    // Audio thread. Opens path when arming, once per arm: if that fails, failed() is set
    // and nothing is written until the next arm.
    void set_armed(bool armed, const std::string& path, float sample_rate);
    void write_samples(const float* interleaved, size_t sample_count);
    // UI thread.
    bool failed() const { return failed_.load(std::memory_order_relaxed); }
    void clear_failure() { failed_.store(false, std::memory_order_relaxed); }

private:
    void close_file();
    bool open_file(const std::string& path, float sample_rate);

    ma_encoder encoder_;
    bool requested_ = false;
    bool armed_ = false; // encoder_ is open
    std::atomic<bool> failed_{false};
};

class WavRecorder : public Module {
public:
    // Runtime-only: not saved, so opening a patch never starts overwriting a recording.
    bool armed = false;
    std::string path = kDefaultRecordPath;

    std::string name() const override { return "wav_recorder"; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* const* outputs, size_t sample_count,
                 float sample_rate) override;

private:
    WavRecorderEngine engine_;
};
