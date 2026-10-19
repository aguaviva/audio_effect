#pragma once

#include "module_json.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct WavClip;

// Host services modules may need during process() (wav cache, record path, etc.).
class GraphHost {
public:
    virtual ~GraphHost() = default;
    virtual float sample_rate() const = 0;
    virtual std::shared_ptr<const WavClip> load_wav(const std::string& path) = 0;
    virtual std::shared_ptr<const WavClip> fallback_wav() const = 0;
    virtual const std::string& record_path() const = 0;
    virtual void analyze(int node_id, const std::string& module_name, const float* block,
                         size_t num_samples, float display_gain, float vu_decay) = 0;
};

// Per-block audio context passed to Module::process.
struct ProcessContext {
    float* buffer = nullptr; // node block (filled by sources / in-place for effects)
    size_t num_samples = 0;
    float sample_rate = 0.0f;

    // Wired inputs in slot order (nullptr = disconnected). Mixer / dry-wet read these.
    const float* const* inputs = nullptr;
    size_t input_count = 0;
    const bool* input_muted = nullptr; // size >= input slots used; may be null

    const int32_t* live_pcm = nullptr; // capture source
    int node_id = -1;
    GraphHost* host = nullptr;
};

// Common graph-module interface. Controllable fields live on the concrete class.
class Module {
public:
    enum class Role {
        source,   // fills buffer; ignores inputs
        effect,   // in-place on buffer (graph pre-sums a single input)
        multi_in, // reads ctx.inputs (mixer / dry-wet); writes buffer
        sink,     // consumes buffer (output / recorder); pass-through OK
        visualizer // pass-through analyze; buffer unchanged
    };

    virtual ~Module() = default;

    virtual std::string name() const = 0;
    virtual void save(std::string& out, bool& first) const = 0;
    virtual bool load(const std::string& key, double num, bool flag, const std::string& str,
                      int kind) = 0;
    virtual bool render() = 0;

    virtual Role role() const = 0;
    virtual void process(ProcessContext& ctx) = 0;

    // Copy controllable fields from another module of the same name (UI → runtime).
    virtual void apply_controls(const Module& from);

    virtual bool supports_bypass() const { return name() != "mixer"; }
    virtual int max_inputs() const;
    virtual bool is_source() const { return role() == Role::source; }
    virtual bool is_output_sink() const { return name() == "output"; }
};

// Runtime instance for the audio thread (may hold delay lines, file handles, …).
std::unique_ptr<Module> make_runtime_module(const std::string& module_name, float sample_rate);
