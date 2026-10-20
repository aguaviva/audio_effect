#pragma once

#include "module.hpp"
#include "paths.hpp"
#include "wav_generator.hpp"

#include <memory>
#include <mutex>
#include <string>

class WavPlayer : public Module {
public:
    bool loop = true;

    // Written by the UI thread, read by the audio thread; set_path() is the only writer.
    const std::string& path() const { return path_; }
    void set_path(const std::string& path);

    std::string name() const override { return "wav_player"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void process(const float* const* inputs, int input_count, float* output, size_t sample_count,
                 float sample_rate) override;

private:
    std::mutex path_mutex_;
    std::string path_ = kDefaultWavPath;
    std::shared_ptr<const WavClip> clip_;
    std::string loaded_path_;
    size_t cursor_ = 0; // next sample in clip_
};
