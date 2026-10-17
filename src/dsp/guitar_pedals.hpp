#pragma once

#include <atomic>
#include <cstddef>

namespace guitar_pedals {

// Selects the broad clipping character; these are simplified pedal-style effects,
// not exact circuit models.
enum class DriveModel : int {
    tube_screamer,
    rat,
    big_muff
};

class DrivePedal {
public:
    // samples: interleaved stereo audio in normalized floats, modified in place.
    // sample_count: number of float values (left and right samples), not frames.
    // model: soft overdrive, hard-clipping distortion, or cascaded fuzz character.
    // drive: clipping intensity; higher values push more of the waveform into distortion.
    // tone: normalized 0..1 control; lower is darker, higher is brighter.
    // sample_rate: audio rate in samples per second; used for the tone filter.
    void process(float* samples, size_t sample_count, DriveModel model,
                 float drive, float tone, float sample_rate);
    void reset();
    static void render(std::atomic<bool>& enabled, std::atomic<int>& model,
                       std::atomic<float>& drive, std::atomic<float>& tone,
                       int& selected_preset, bool& controls_changed);

private:
    static float shape(float input, DriveModel model, float drive, float bias,
                       float bias_offset, float positive_scale, float negative_scale,
                       float fuzz_normalization);
    float tone_state_l_{0.0f};
    float tone_state_r_{0.0f};
};

} // namespace guitar_pedals
