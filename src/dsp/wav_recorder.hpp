#pragma once

#include "module.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

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

// Graph module. Runtime file I/O lives in a lazily created shared engine so
// NodeState copies stay light / copyable.
class WavRecorder : public Module {
public:
    bool armed = false;

    std::string name() const override { return "wav_recorder"; }
    Role role() const override { return Role::sink; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

private:
    std::shared_ptr<WavRecorderEngine> engine_;
};
