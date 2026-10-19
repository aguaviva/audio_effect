#pragma once

#include "module.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

// Graph module: arm flag only (name / save / load / render).
class WavRecorder : public Module {
public:
    bool armed = false;

    std::string name() const override { return "wav_recorder"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
};

// Runtime pass-through WAV writer (16-bit PCM stereo). Owned by GraphProcessor.
class WavRecorderEngine {
public:
    void set_path(const std::string& path);
    const std::string& path() const { return path_; }

    void set_armed(bool armed, float sample_rate);
    bool armed() const { return armed_; }

    void write_samples(const float* interleaved, size_t sample_count);

    std::string status() const;

private:
    void close_file();
    bool open_file(float sample_rate);
    void patch_header();

    mutable std::mutex mutex_;
    std::string path_{"assets/recordings/capture.wav"};
    std::ofstream file_;
    bool armed_ = false;
    uint32_t sample_rate_ = 48000;
    uint64_t data_bytes_ = 0;
    std::string status_{"Idle"};
};
