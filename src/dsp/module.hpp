#pragma once

#include "module_json.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct WavClip;

// Host services modules may need during process() (wav cache, record path, etc.).
class GraphHost {
public:
    virtual ~GraphHost() = default;
    virtual std::shared_ptr<const WavClip> load_wav(const std::string& path) = 0;
    virtual std::shared_ptr<const WavClip> fallback_wav() const = 0;
    virtual const std::string& record_path() const = 0;
    virtual const int32_t* live_input() const = 0;
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

    virtual void get_inputs(int *count, const char* const** str) { static const char* input_names[] = {"in"}; *count=1; *str=input_names; }    
    virtual void get_outputs(int *count, const char* const** str) { static const char* output_names[] = {"out"}; *count=1; *str=output_names; }    

    virtual bool supports_bypass() const { return true; }
    virtual bool is_output_sink() const { return false; }

protected:
    GraphHost* host_ = nullptr;
    friend class GraphProcessor;
};
