#pragma once

#include "audio_input_sources.hpp"
#include "module.hpp"
#include "wav_recorder.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class WavPlayer : public Module {
public:
    bool loop = true;

    // Written by the UI thread, read by the audio thread; set_path() is the only writer.
    const std::string& path() const { return path_; }
    void set_path(const std::string& path);

    std::string name() const override { return "wav_player"; }
    Role role() const override { return Role::source; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

private:
    std::mutex path_mutex_;
    std::string path_;
    WavFileInput wav_;
    std::shared_ptr<const WavClip> clip_;
    std::string loaded_path_;
    std::vector<int32_t> scratch_;
};

class AudioInput : public Module {
public:
    std::string device = "default";

    std::string name() const override { return "audio_input"; }
    Role role() const override { return Role::source; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;
};

class AudioOutput : public Module {
public:
    std::string device = "default";

    std::string name() const override { return "output"; }
    Role role() const override { return Role::sink; }
    bool is_output_sink() const override { return true; }
    bool supports_bypass() const override { return false; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& /*ctx*/) override {}
};

class ChordGenerator : public Module {
public:
    int index = 0;

    std::string name() const override { return "chord_generator"; }
    Role role() const override { return Role::source; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

private:
    ChordGeneratorInput chord_;
    std::vector<int32_t> scratch_;
};

class WhiteNoise : public Module {
public:
    float level = 0.2f;

    std::string name() const override { return "white_noise"; }
    Role role() const override { return Role::source; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;

private:
    uint32_t rng_ = 0x12345678u;
};

class DryWetMix : public Module {
public:
    float mix = 0.5f;

    std::string name() const override { return "dry_wet_mix"; }
    Role role() const override { return Role::multi_in; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;
};

class VuMeter : public Module {
public:
    float display_gain = 1.0f;
    float decay = 0.85f;

    std::string name() const override { return "vu_meter"; }
    Role role() const override { return Role::visualizer; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;
};

class Oscilloscope : public Module {
public:
    float display_gain = 1.0f;

    std::string name() const override { return "oscilloscope"; }
    Role role() const override { return Role::visualizer; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;
};

class Spectrum : public Module {
public:
    float display_gain = 1.0f;

    std::string name() const override { return "spectrum"; }
    Role role() const override { return Role::visualizer; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(ProcessContext& ctx) override;
};

class PitchDetectorModule : public Module {
public:
    std::string name() const override { return "pitch_detector"; }
    Role role() const override { return Role::visualizer; }
    std::string save() const override { return "{}"; }
    bool load(const std::string& /*json*/) override { return true; }
    bool render() override { return false; }
    void process(ProcessContext& ctx) override;
};
