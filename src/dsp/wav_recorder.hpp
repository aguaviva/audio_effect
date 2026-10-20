#pragma once

#include "module.hpp"
#include "paths.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

class WavRecorderEngine {
public:
    WavRecorderEngine() = default;
    WavRecorderEngine(const WavRecorderEngine&) = delete;
    WavRecorderEngine& operator=(const WavRecorderEngine&) = delete;
    ~WavRecorderEngine();

    // Opens path when arming; the path is ignored while already armed.
    void set_armed(bool armed, const std::string& path, float sample_rate);
    void write_samples(const float* interleaved, size_t sample_count);

private:
    void close_file();
    bool open_file(const std::string& path);
    void patch_header();

    std::ofstream file_;
    bool armed_ = false;
    uint32_t sample_rate_ = 48000;
    uint64_t data_bytes_ = 0;
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
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;

private:
    WavRecorderEngine engine_;
};
