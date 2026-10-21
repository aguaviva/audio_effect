#pragma once

#include "module_json.hpp"

#include <algorithm>
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

    // inputs[pin] is the buffer of the link wired into that pin, or nullptr when the pin is
    // unconnected. output is never null and must be fully written.
    virtual void process(const float* const* inputs, int input_count, float* output,
                         size_t sample_count, float sample_rate) = 0;

    static constexpr int kMaxInputs = 8;

    // Pin names. Sources have no inputs, sinks no outputs; at most kMaxInputs inputs.
    virtual void get_inputs(int *count, const char* const** str) const { static const char* input_names[] = {"in"}; *count=1; *str=input_names; }
    virtual void get_outputs(int *count, const char* const** str) const { static const char* output_names[] = {"out"}; *count=1; *str=output_names; }

    int input_count() const { int n = 0; const char* const* s = nullptr; get_inputs(&n, &s); return n; }
    int output_count() const { int n = 0; const char* const* s = nullptr; get_outputs(&n, &s); return n; }

    virtual bool supports_bypass() const { return true; }
    virtual bool is_output_sink() const { return false; }

protected:
    GraphHost* host_ = nullptr;
    friend class GraphProcessor;
};
