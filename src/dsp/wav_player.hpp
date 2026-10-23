#pragma once

#include "module.hpp"
#include "paths.hpp"
#include "wav_generator.hpp"
#include "ui/file_picker.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

class WavPlayer : public Module {
public:
    bool loop = true;
    bool playing = true;

    // UI thread. set_path() decodes the file; when that fails the player is silent, it
    // returns false and error() says why.
    const std::string& path() const { return path_; }
    bool set_path(const std::string& path);
    const std::string& error() const { return error_; }

    std::string name() const override { return "wav_player"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* const* outputs, size_t sample_count,
                 float sample_rate) override;

private:
    FilePicker picker_{"Load WAV", ".wav"};
    std::string path_ = kDefaultWavPath;
    std::string error_;
    size_t length_ = 0; // samples in the clip set_path() loaded

    // Handoff: set_path() fills next_clip_; process() swaps it with clip_, so the replaced
    // clip is freed by the next set_path() on the UI thread, never on the audio thread.
    std::mutex clip_mutex_;
    std::shared_ptr<const WavClip> next_clip_;
    bool clip_changed_ = false;

    std::shared_ptr<const WavClip> clip_; // audio thread
    std::atomic<size_t> cursor_{0};       // next sample in clip_; the UI seeks by setting it
};
