#pragma once

#include <string>

// Patchable node kinds. Sources, effects, sinks, and visualizers may be wired
// in any legal topology (fan-in / fan-out); nothing here implies a fixed chain.
//
// Buffer contract: every graph edge carries one audio block of a single global
// size. Sources write that size; effects/visualizers read/write that size in
// place (visualizers are pass-through); sinks consume that size.
//
// State: each node type owns a param struct in NodeState (named JSON serialize).
enum class NodeType : int {
    audio_input = 0,   // source: live capture (miniaudio)
    wav_player,        // source: loaded WAV playback
    chord_generator,   // source/generator: synthetic chords
    white_noise,       // source/generator: white noise
    echo,              // effect
    preamp,            // effect
    high_pass,         // effect
    drive_pedal,       // effect
    low_pass,          // effect
    compressor,        // effect
    noise_gate,        // effect
    eq_three_band,     // effect
    reverb,            // effect
    vu_meter,          // visualizer (pass-through)
    oscilloscope,      // visualizer (pass-through)
    spectrum,          // visualizer (pass-through)
    pitch_detector,    // visualizer (pass-through): frequency + note
    mixer,             // utility: N inputs -> 1 output (equal-weight average)
    dry_wet_mix,       // utility: dry(slot0) + wet(slot1) blend
    wav_recorder,      // pass-through file sink
    output,            // sink: playback (miniaudio)
    count
};

inline const char* node_type_label(NodeType type) {
    switch (type) {
    case NodeType::audio_input: return "Audio Input";
    case NodeType::wav_player: return "WAV Player";
    case NodeType::chord_generator: return "Chord Generator";
    case NodeType::white_noise: return "White Noise";
    case NodeType::echo: return "Echo";
    case NodeType::preamp: return "Preamp";
    case NodeType::high_pass: return "High-Pass Filter";
    case NodeType::drive_pedal: return "Drive Pedal";
    case NodeType::low_pass: return "Low-Pass Filter";
    case NodeType::compressor: return "Valve Compressor";
    case NodeType::noise_gate: return "Noise Gate";
    case NodeType::eq_three_band: return "3-Band EQ";
    case NodeType::reverb: return "Reverb";
    case NodeType::vu_meter: return "VU Meter";
    case NodeType::oscilloscope: return "Oscilloscope";
    case NodeType::spectrum: return "Spectrum";
    case NodeType::pitch_detector: return "Pitch Detector";
    case NodeType::mixer: return "Mixer";
    case NodeType::dry_wet_mix: return "Dry/Wet Mix";
    case NodeType::wav_recorder: return "WAV Recorder";
    case NodeType::output: return "Output";
    default: return "Unknown";
    }
}

// Stable JSON ids — must match node_params::<Module>::name() for each type.
inline const char* node_type_key(NodeType type) {
    switch (type) {
    case NodeType::audio_input: return "audio_input";
    case NodeType::wav_player: return "wav_player";
    case NodeType::chord_generator: return "chord_generator";
    case NodeType::white_noise: return "white_noise";
    case NodeType::echo: return "echo";
    case NodeType::preamp: return "preamp";
    case NodeType::high_pass: return "high_pass";
    case NodeType::drive_pedal: return "drive_pedal";
    case NodeType::low_pass: return "low_pass";
    case NodeType::compressor: return "compressor";
    case NodeType::noise_gate: return "noise_gate";
    case NodeType::eq_three_band: return "eq_three_band";
    case NodeType::reverb: return "reverb";
    case NodeType::vu_meter: return "vu_meter";
    case NodeType::oscilloscope: return "oscilloscope";
    case NodeType::spectrum: return "spectrum";
    case NodeType::pitch_detector: return "pitch_detector";
    case NodeType::mixer: return "mixer";
    case NodeType::dry_wet_mix: return "dry_wet_mix";
    case NodeType::wav_recorder: return "wav_recorder";
    case NodeType::output: return "output";
    default: return "unknown";
    }
}

// Keys match each module's node_params::*::name(). Prefer
// node_params::try_resolve_name / NodeState::load_by_module_name for load paths.
inline bool try_parse_node_type(const std::string& key, NodeType& out) {
    for (int i = 0; i < static_cast<int>(NodeType::count); ++i) {
        auto type = static_cast<NodeType>(i);
        if (key == node_type_key(type)) {
            out = type;
            return true;
        }
    }
    return false;
}

inline bool node_is_visualizer(NodeType type) {
    return type == NodeType::vu_meter ||
           type == NodeType::oscilloscope ||
           type == NodeType::spectrum ||
           type == NodeType::pitch_detector;
}

inline bool node_has_input_pin(NodeType type) {
    return type != NodeType::audio_input &&
           type != NodeType::wav_player &&
           type != NodeType::chord_generator &&
           type != NodeType::white_noise;
}

inline bool node_has_output_pin(NodeType type) {
    return type != NodeType::output;
}

inline bool node_is_source(NodeType type) {
    return type == NodeType::audio_input ||
           type == NodeType::wav_player ||
           type == NodeType::chord_generator ||
           type == NodeType::white_noise;
}

inline bool node_is_mixer(NodeType type) {
    return type == NodeType::mixer;
}

// Mixer uses per-input mute instead of a whole-node bypass.
inline bool node_supports_bypass(NodeType type) {
    return !node_is_mixer(type);
}

inline bool node_is_dry_wet(NodeType type) {
    return type == NodeType::dry_wet_mix;
}

inline bool node_uses_exclusive_input_slots(NodeType type) {
    return node_is_mixer(type) || node_is_dry_wet(type);
}

// Pin IDs: node_id * 100 + code. code 0 = output; code 1+N = input slot N.
inline constexpr int kMaxMixerInputs = 8;

inline int node_input_slot_limit(NodeType type) {
    if (node_is_mixer(type)) {
        return kMaxMixerInputs;
    }
    if (node_is_dry_wet(type)) {
        return 2;
    }
    return node_has_input_pin(type) ? 1 : 0;
}

inline int output_pin_id(int node_id) { return node_id * 100; }
inline int input_pin_id(int node_id, int slot = 0) { return node_id * 100 + 1 + slot; }
inline int node_id_from_pin(int pin_id) { return pin_id / 100; }
inline int pin_code_from_pin(int pin_id) { return pin_id % 100; }
inline bool is_output_pin(int pin_id) { return pin_code_from_pin(pin_id) == 0; }
inline int input_slot_from_pin(int pin_id) { return pin_code_from_pin(pin_id) - 1; }
