#include "graph_processor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

GraphProcessor::GraphProcessor(float sample_rate)
    : sample_rate_(sample_rate) {}

void GraphProcessor::set_block_size(size_t block_size) {
    if (block_size_ == block_size) {
        return;
    }
    block_size_ = block_size;
    source_scratch_.assign(block_size_, 0);
    for (auto& [id, state] : states_) {
        (void)id;
        state->buffer.assign(block_size_, 0.0f);
    }
}

void GraphProcessor::set_loaded_wav(std::shared_ptr<const WavClip> clip) {
    wav_clip_ = std::move(clip);
}

void GraphProcessor::set_record_path(const std::string& path) {
    record_path_ = path;
}

std::shared_ptr<const WavClip> GraphProcessor::clip_for_path(const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }
    auto it = wav_cache_.find(path);
    if (it != wav_cache_.end()) {
        return it->second;
    }
    std::string error;
    auto clip = WavGenerator::load(path, static_cast<uint32_t>(sample_rate_), error);
    if (clip) {
        wav_cache_[path] = clip;
    }
    return clip;
}

void GraphProcessor::ensure_state(const CompiledGraph& graph) {
    assert(block_size_ > 0 && "graph block size must be set before process");

    std::unordered_map<int, bool> alive;
    for (const auto& node : graph.order) {
        alive[node.id()] = true;
        auto it = states_.find(node.id());
        if (it == states_.end() || it->second->type != node.type()) {
            auto state = std::make_unique<NodeRuntime>(static_cast<size_t>(sample_rate_) + 1);
            state->type = node.type();
            state->buffer.assign(block_size_, 0.0f);
            states_[node.id()] = std::move(state);
        } else {
            assert(it->second->buffer.size() == block_size_);
            if (it->second->buffer.size() != block_size_) {
                it->second->buffer.assign(block_size_, 0.0f);
            }
        }
    }

    for (auto it = states_.begin(); it != states_.end();) {
        if (!alive.count(it->first)) {
            it = states_.erase(it);
        } else {
            ++it;
        }
    }
}

void GraphProcessor::mix_inputs(const CompiledGraph::Node& node,
                                const std::unordered_map<int, std::vector<float>*>& buffers,
                                float* destination) const {
    std::fill(destination, destination + block_size_, 0.0f);

    if (node_is_dry_wet(node.type())) {
        const float* dry = nullptr;
        const float* wet = nullptr;
        for (const auto& in : node.inputs()) {
            auto it = buffers.find(in.from_node);
            if (it == buffers.end() || !it->second || it->second->size() != block_size_) {
                continue;
            }
            if (in.to_slot == 0) {
                dry = it->second->data();
            } else if (in.to_slot == 1) {
                wet = it->second->data();
            }
        }
        DryWetMix mix;
        if (const auto* m = node.state().as<node_params::DryWet>()) {
            mix.mix = m->mix;
        }
        mix.process(dry, wet, destination, block_size_);
        return;
    }

    if (node_is_mixer(node.type())) {
        const float* sources[kMaxMixerInputs]{};
        size_t source_count = 0;
        for (const auto& in : node.inputs()) {
            if (node.state().is_input_muted(in.to_slot)) {
                continue;
            }
            auto it = buffers.find(in.from_node);
            if (it == buffers.end() || !it->second || it->second->size() != block_size_) {
                continue;
            }
            if (source_count < static_cast<size_t>(kMaxMixerInputs)) {
                sources[source_count++] = it->second->data();
            }
        }
        Mixer{}.process(sources, source_count, destination, block_size_);
        return;
    }

    // Single-input (or fan-in sum without averaging) for other node types.
    for (const auto& in : node.inputs()) {
        auto it = buffers.find(in.from_node);
        if (it == buffers.end() || !it->second) {
            continue;
        }
        assert(it->second->size() == block_size_);
        if (it->second->size() != block_size_) {
            continue;
        }
        const float* src = it->second->data();
        float input_gain = 1.0f;
        if (node_is_mixer(node.type()) && in.to_slot >= 0 &&
            in.to_slot < kMaxMixerInputs) {
            input_gain = std::clamp(
                node.state().input_gain[static_cast<size_t>(in.to_slot)], 0.0f, 2.0f);
        }
        for (size_t i = 0; i < block_size_; ++i) {
            destination[i] += src[i] * input_gain;
        }
    }
}

void GraphProcessor::run_effect_or_io(NodeRuntime& state,
                                      const CompiledGraph::Node& node,
                                      const int32_t* live_pcm,
                                      float* block) {
    assert(state.buffer.size() == block_size_);
    const NodeState& ns = node.state();
    switch (node.type()) {
    case NodeType::audio_input:
        for (size_t i = 0; i < block_size_; ++i) {
            block[i] = static_cast<float>(live_pcm[i]) / 2147483648.0f;
        }
        break;
    case NodeType::wav_player: {
        if (source_scratch_.size() != block_size_) {
            source_scratch_.assign(block_size_, 0);
        }
        bool loop = true;
        std::string path;
        if (const auto* m = ns.as<node_params::WavPlayer>()) {
            loop = m->loop;
            path = m->path;
        }
        if (path != state.wav_path) {
            state.wav_path = path;
            state.wav_clip = clip_for_path(path);
            if (!state.wav_clip) {
                state.wav_clip = wav_clip_;
            }
            state.wav.reset();
        }
        if (state.wav_clip && state.wav_clip->frame_count() > 0) {
            state.wav.render(state.wav_clip, loop, source_scratch_.data(), block_size_ / 2);
        } else {
            std::fill(source_scratch_.begin(), source_scratch_.end(), 0);
        }
        for (size_t i = 0; i < block_size_; ++i) {
            block[i] = static_cast<float>(source_scratch_[i]) / 2147483648.0f;
        }
        break;
    }
    case NodeType::chord_generator: {
        if (source_scratch_.size() != block_size_) {
            source_scratch_.assign(block_size_, 0);
        }
        int chord = 0;
        if (const auto* m = ns.as<node_params::Chord>()) {
            chord = m->index;
        }
        state.chord.render(chord, source_scratch_.data(), block_size_ / 2, 2,
                           static_cast<uint32_t>(sample_rate_));
        for (size_t i = 0; i < block_size_; ++i) {
            block[i] = static_cast<float>(source_scratch_[i]) / 2147483648.0f;
        }
        break;
    }
    case NodeType::white_noise:
        if (const auto* m = ns.as<node_params::WhiteNoise>()) {
            state.white_noise.level = m->level;
        }
        state.white_noise.process(block, block_size_);
        break;
    case NodeType::echo:
        if (const auto* m = ns.as<node_params::Echo>()) {
            state.echo.copy_controls_from(*m);
            state.echo.process(block, block_size_, sample_rate_);
        }
        break;
    case NodeType::preamp:
        if (const auto* m = ns.as<node_params::Preamp>()) {
            state.preamp.gain_db = m->gain_db;
            state.preamp.selected_preset = m->selected_preset;
            state.preamp.process(block, block_size_);
        }
        break;
    case NodeType::high_pass:
        if (const auto* m = ns.as<node_params::HighPass>()) {
            state.high_pass.cutoff_hz = m->cutoff_hz;
            state.high_pass.selected_preset = m->selected_preset;
            state.high_pass.process(block, block_size_, sample_rate_);
        }
        break;
    case NodeType::low_pass:
        if (const auto* m = ns.as<node_params::LowPass>()) {
            state.low_pass.cutoff_hz = m->cutoff_hz;
            state.low_pass.selected_preset = m->selected_preset;
            state.low_pass.process(block, block_size_, sample_rate_);
        }
        break;
    case NodeType::drive_pedal:
        if (const auto* m = ns.as<node_params::Drive>()) {
            state.drive.copy_controls_from(*m);
            state.drive.process(block, block_size_, sample_rate_);
        }
        break;
    case NodeType::compressor:
        if (const auto* m = ns.as<node_params::Compressor>()) {
            state.compressor.copy_controls_from(*m);
            state.compressor.process(block, block_size_, sample_rate_);
        }
        break;
    case NodeType::noise_gate:
        if (const auto* m = ns.as<node_params::NoiseGate>()) {
            state.gate.copy_controls_from(*m);
            state.gate.process(block, block_size_, sample_rate_);
        }
        break;
    case NodeType::eq_three_band:
        if (const auto* m = ns.as<node_params::EqThreeBand>()) {
            state.eq.copy_controls_from(*m);
            state.eq.process(block, block_size_, sample_rate_);
        }
        break;
    case NodeType::reverb:
        if (const auto* m = ns.as<node_params::Reverb>()) {
            state.reverb.copy_controls_from(*m);
            state.reverb.process(block, block_size_, sample_rate_);
        }
        break;
    case NodeType::vu_meter:
        if (const auto* m = ns.as<node_params::VuMeter>()) {
            state.vu_meter.display_gain = m->display_gain;
            state.vu_meter.decay = m->decay;
        }
        g_visualizer_bus.analyze(node.id(), node.type(), block, block_size_, sample_rate_,
                                 state.vu_meter.display_gain, state.vu_meter.decay);
        break;
    case NodeType::oscilloscope:
        if (const auto* m = ns.as<node_params::Oscilloscope>()) {
            state.oscilloscope.display_gain = m->display_gain;
        }
        g_visualizer_bus.analyze(node.id(), node.type(), block, block_size_, sample_rate_,
                                 state.oscilloscope.display_gain, 0.85f);
        break;
    case NodeType::spectrum:
        if (const auto* m = ns.as<node_params::Spectrum>()) {
            state.spectrum.display_gain = m->display_gain;
        }
        g_visualizer_bus.analyze(node.id(), node.type(), block, block_size_, sample_rate_,
                                 state.spectrum.display_gain, 0.85f);
        break;
    case NodeType::pitch_detector:
        g_visualizer_bus.analyze(node.id(), node.type(), block, block_size_, sample_rate_, 1.0f,
                                 0.85f);
        break;
    case NodeType::mixer:
    case NodeType::dry_wet_mix:
        break;
    case NodeType::wav_recorder: {
        bool armed = false;
        if (const auto* m = ns.as<node_params::WavRecorder>()) {
            armed = m->armed;
        }
        state.recorder.set_path(record_path_);
        state.recorder.set_armed(armed, sample_rate_);
        state.recorder.write_samples(block, block_size_);
        break;
    }
    case NodeType::output:
        break;
    default:
        break;
    }
}

bool GraphProcessor::process(const CompiledGraph& graph,
                             const int32_t* live_pcm,
                             float* out_float,
                             size_t block_size) {
    if (block_size == 0) {
        return false;
    }
    if (block_size_ != block_size) {
        set_block_size(block_size);
    }

    if (!graph.valid || graph.order.empty()) {
        std::fill(out_float, out_float + block_size_, 0.0f);
        return false;
    }

    ensure_state(graph);

    std::vector<int> alive_ids;
    alive_ids.reserve(graph.order.size());
    std::unordered_map<int, std::vector<float>*> buffers;
    buffers.reserve(graph.order.size());
    for (const auto& node : graph.order) {
        assert(states_[node.id()]->buffer.size() == block_size_);
        buffers[node.id()] = &states_[node.id()]->buffer;
        alive_ids.push_back(node.id());
    }
    g_visualizer_bus.retain_only(alive_ids);

    bool wrote_output = false;
    std::fill(out_float, out_float + block_size_, 0.0f);

    for (const auto& node : graph.order) {
        NodeRuntime& state = *states_[node.id()];
        float* block = state.buffer.data();
        const bool bypassed = node_supports_bypass(node.type()) && node.state().bypassed;

        if (node_is_source(node.type())) {
            if (bypassed) {
                std::fill(block, block + block_size_, 0.0f);
            } else {
                run_effect_or_io(state, node, live_pcm, block);
            }
        } else {
            mix_inputs(node, buffers, block);
            if (!bypassed) {
                run_effect_or_io(state, node, live_pcm, block);
            }
        }

        if (node.type() == NodeType::output && !bypassed) {
            for (size_t i = 0; i < block_size_; ++i) {
                out_float[i] += block[i];
            }
            wrote_output = true;
        }
    }

    return wrote_output;
}
