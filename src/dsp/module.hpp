#pragma once

#include "module_json.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// The rate the audio engine runs every module at.
constexpr uint32_t kSampleRate = 48000;

// Host services modules may need during process().
class GraphHost {
public:
    virtual ~GraphHost() = default;
    // Captured device block (interleaved float), or nullptr.
    virtual const float* live_input() const = 0;
};

// Copies inputs[slot] into output, or silence when that pin is not connected.
inline void copy_input(const float* const* inputs, int input_count, int slot, float* output,
                       size_t sample_count) {
    const float* in = slot < input_count ? inputs[slot] : nullptr;
    if (in) {
        std::copy(in, in + sample_count, output);
    } else {
        std::fill(output, output + sample_count, 0.0f);
    }
}

// Common graph-module interface. Controllable fields live on the concrete class.
class Module {
public:
    virtual ~Module() = default;

    virtual std::string name() const = 0;
    // Serialize / parse this module's own JSON object ("{...}").
    virtual std::string save() const = 0;
    virtual bool load(const std::string& json) = 0;
    virtual bool render() = 0;
    // Windows of the module's own, drawn outside the node editor once per frame.
    virtual void render_window() {}

    // inputs[pin] is the buffer of the link wired into that pin, or nullptr when the pin is
    // unconnected. Writes sample_count samples to every output pin that has a buffer.
    virtual void process(const float* const* inputs, int input_count, size_t sample_count,
                         float sample_rate) = 0;

    static constexpr int kMaxInputs = 8;
    static constexpr int kMaxOutputs = 16;

    // Where each output pin writes: the buffer of a link from it, or nullptr when nothing is
    // linked to it. A sink (no output pins) writes pin 0's buffer, which goes to the device.
    // Set by the audio thread before process().
    float* output_buffer(int pin) const { return output_buffers_[pin]; }
    void set_output_buffer(int pin, float* buffer) { output_buffers_[pin] = buffer; }

    // Pin names. Sources have no inputs, sinks no outputs; at most kMaxInputs inputs and
    // kMaxOutputs outputs.
    virtual void get_inputs(int *count, const char* const** str) const { static const char* input_names[] = {"in"}; *count=1; *str=input_names; }
    virtual void get_outputs(int *count, const char* const** str) const { static const char* output_names[] = {"out"}; *count=1; *str=output_names; }

    int input_count() const { int n = 0; const char* const* s = nullptr; get_inputs(&n, &s); return n; }
    int output_count() const { int n = 0; const char* const* s = nullptr; get_outputs(&n, &s); return n; }

    virtual bool is_output_sink() const { return false; }

    // Smoothed time one block takes in this module, in microseconds.
    float process_us() const { return process_us_.load(std::memory_order_relaxed); }
    // Peak of an output pin, held and falling about 35 dB a second.
    float output_level(int slot) const { return output_levels_[slot].load(std::memory_order_relaxed); }

protected:
    // For modules that pass their input through and look at it: copies input pin 0 to output
    // pin 0 when that has a buffer, and returns the input (nullptr when unconnected).
    const float* pass_through(const float* const* inputs, int input_count, size_t sample_count) {
        if (float* out = output_buffer(0)) {
            copy_input(inputs, input_count, 0, out, sample_count);
        }
        return input_count > 0 ? inputs[0] : nullptr;
    }

    GraphHost* host_ = nullptr;
    // Written by GraphProcessor.
    std::atomic<float> process_us_{0.0f};
    std::array<std::atomic<float>, kMaxOutputs> output_levels_{};
    friend class GraphProcessor;

private:
    std::array<float*, kMaxOutputs> output_buffers_{};
};
