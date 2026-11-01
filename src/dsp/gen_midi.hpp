#pragma once

#include "module.hpp"
#include "wav_generator.hpp"
#include "ui/file_picker.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "midi.h"

// Six tracks of WAV clips placed in time, one output pin each. Plays from the cursor.
class Midi : public Module {
public:
    static constexpr int kTracks = 10;
    static_assert(kTracks <= kMaxOutputs, "every track needs an output pin");
    bool playing = false;

    std::string name() const override { return "midi"; }
    void get_inputs(int *count, const char* const** str) const override { *count=0; *str=nullptr; }
    void get_outputs(int* count, const char* const** str) const override;
    bool set_path(const std::string& path);
    std::string save() const override;
    bool load(const std::string& json) override;
    bool render() override;
    void render_window() override;
    void process(const float* const* inputs, int input_count, size_t sample_count,
                 float sample_rate) override;

private:
    FilePicker picker_{"Load Midi", ".mid"};
    std::string path_;
    std::string error_;    
    unsigned char *pMidi_buffer;
    size_t midi_size;
    MidiSong midi_song;
};
