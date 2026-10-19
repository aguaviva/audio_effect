#pragma once

#include "module_json.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>

// Pass-through WAV writer (16-bit PCM stereo). Call set_path before arming.
class WavRecorder {
public:
    struct Params {
        bool armed = false;

        static std::string name() { return "wav_recorder"; }

        void write(std::string& out, bool& first) const;
        bool read(const std::string& key, double num, bool flag, const std::string& str, int kind);
    };

    void set_path(const std::string& path);
    const std::string& path() const { return path_; }

    // When armed becomes true, (re)opens the file. When false, finalizes header.
    void set_armed(bool armed, float sample_rate);
    bool armed() const { return armed_; }

    // Append interleaved float samples; audio is not modified by the caller.
    void write(const float* interleaved, size_t sample_count);

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
