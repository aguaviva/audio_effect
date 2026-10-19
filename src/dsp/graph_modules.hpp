#pragma once

#include "module.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

class WavPlayer : public Module {
public:
    bool loop = true;
    std::string path;

    std::string name() const override { return "wav_player"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
};

class AudioInput : public Module {
public:
    std::string device = "default";

    std::string name() const override { return "audio_input"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
};

class AudioOutput : public Module {
public:
    std::string device = "default";

    std::string name() const override { return "output"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
};

class ChordGenerator : public Module {
public:
    int index = 0;

    std::string name() const override { return "chord_generator"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
};

class WhiteNoise : public Module {
public:
    float level = 0.2f;

    std::string name() const override { return "white_noise"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;

    void process(float* samples, size_t num_samples);

private:
    uint32_t rng_ = 0x12345678u;
};

class DryWetMix : public Module {
public:
    float mix = 0.5f;

    std::string name() const override { return "dry_wet_mix"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;

    void process(const float* dry, const float* wet, float* destination, size_t num_samples) const;
};

class VuMeter : public Module {
public:
    float display_gain = 1.0f;
    float decay = 0.85f;

    std::string name() const override { return "vu_meter"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
};

class Oscilloscope : public Module {
public:
    float display_gain = 1.0f;

    std::string name() const override { return "oscilloscope"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
};

class Spectrum : public Module {
public:
    float display_gain = 1.0f;

    std::string name() const override { return "spectrum"; }
    void save(std::string& out, bool& first) const override;
    bool load(const std::string& key, double num, bool flag, const std::string& str,
              int kind) override;
    bool render() override;
};

class PitchDetectorModule : public Module {
public:
    std::string name() const override { return "pitch_detector"; }
    void save(std::string& /*out*/, bool& /*first*/) const override {}
    bool load(const std::string& /*key*/, double /*num*/, bool /*flag*/, const std::string& /*str*/,
              int /*kind*/) override {
        return false;
    }
    bool render() override { return false; }
};
