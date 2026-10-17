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
        float mix = 0.5f;
        if (const auto* p = node.state().as<node_params::DryWet>()) {
            mix = std::clamp(p->mix, 0.0f, 1.0f);
        }
        for (size_t i = 0; i < block_size_; ++i) {
            const float d = dry ? dry[i] : 0.0f;
            const float w = wet ? wet[i] : 0.0f;
            destination[i] = d * (1.0f - mix) + w * mix;
        }
        return;
    }

    int connected = 0;
    for (const auto& in : node.inputs()) {
        // Per-slot mute is Mixer-only UI; ignore mute flags on other node types.
        if (node_is_mixer(node.type()) && node.state().is_input_muted(in.to_slot)) {
            continue;
        }
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
        ++connected;
    }

    if (node_is_mixer(node.type()) && connected > 0) {
        const float scale = 1.0f / static_cast<float>(connected);
        for (size_t i = 0; i < block_size_; ++i) {
            destination[i] *= scale;
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
        if (const auto* p = ns.as<node_params::WavPlayer>()) {
            loop = p->loop;
            path = p->path;
        }
        if (path != state.wav_path) {
            state.wav_path = path;
            state.wav_clip = clip_for_path(path);
            if (!state.wav_clip) {
                state.wav_clip = wav_clip_; // legacy global fallback
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
        if (const auto* p = ns.as<node_params::Chord>()) {
            chord = p->index;
        }
        state.chord.render(chord, source_scratch_.data(), block_size_ / 2, 2,
                           static_cast<uint32_t>(sample_rate_));
        for (size_t i = 0; i < block_size_; ++i) {
            block[i] = static_cast<float>(source_scratch_[i]) / 2147483648.0f;
        }
        break;
    }
    case NodeType::white_noise: {
        float level = 0.2f;
        if (const auto* p = ns.as<node_params::WhiteNoise>()) {
            level = std::clamp(p->level, 0.0f, 1.0f);
        }
        // LCG white noise, independent per interleaved sample.
        for (size_t i = 0; i < block_size_; ++i) {
            state.noise_rng = state.noise_rng * 1664525u + 1013904223u;
            const float unit = static_cast<float>((state.noise_rng >> 8) & 0x00FFFFFFu) *
                               (1.0f / 16777216.0f);
            block[i] = (unit * 2.0f - 1.0f) * level;
        }
        break;
    }
    case NodeType::echo:
        if (const auto* p = ns.as<node_params::Echo>()) {
            state.echo.process(block, block_size_, p->delay_ms, p->feedback, p->mix, sample_rate_);
        }
        break;
    case NodeType::preamp:
        if (const auto* p = ns.as<node_params::Preamp>()) {
            state.preamp.process(block, block_size_, p->gain_db);
        }
        break;
    case NodeType::high_pass:
    case NodeType::low_pass:
        if (const auto* p = ns.as<node_params::CutoffFilter>()) {
            if (node.type() == NodeType::high_pass) {
                state.high_pass.process(block, block_size_, p->cutoff_hz, sample_rate_);
            } else {
                state.low_pass.process(block, block_size_, p->cutoff_hz, sample_rate_);
            }
        }
        break;
    case NodeType::drive_pedal:
        if (const auto* p = ns.as<node_params::Drive>()) {
            state.drive.process(block, block_size_,
                                static_cast<guitar_pedals::DriveModel>(p->model), p->amount,
                                p->tone, sample_rate_);
        }
        break;
    case NodeType::compressor:
        if (const auto* p = ns.as<node_params::Compressor>()) {
            state.compressor.process(block, block_size_, p->threshold_db, p->ratio, p->attack_ms,
                                     p->release_ms, p->drive, p->makeup_db, sample_rate_);
        }
        break;
    case NodeType::noise_gate:
        if (const auto* p = ns.as<node_params::NoiseGate>()) {
            state.gate.process(block, block_size_, p->threshold_db, p->attack_ms, p->release_ms,
                               sample_rate_);
        }
        break;
    case NodeType::eq_three_band:
        if (const auto* p = ns.as<node_params::EqThreeBand>()) {
            state.eq.process(block, block_size_, p->low_db, p->mid_db, p->high_db, sample_rate_);
        }
        break;
    case NodeType::reverb:
        if (const auto* p = ns.as<node_params::Reverb>()) {
            state.reverb.process(block, block_size_, p->room, p->damping, p->mix, sample_rate_);
        }
        break;
    case NodeType::vu_meter:
    case NodeType::oscilloscope:
    case NodeType::spectrum:
    case NodeType::pitch_detector: {
        float display_gain = 1.0f;
        float vu_decay = 0.85f;
        if (const auto* p = ns.as<node_params::VuMeter>()) {
            display_gain = p->display_gain;
            vu_decay = p->decay;
        } else if (const auto* p = ns.as<node_params::DisplayGain>()) {
            display_gain = p->display_gain;
        }
        g_visualizer_bus.analyze(node.id(), node.type(), block, block_size_, sample_rate_,
                                 display_gain, vu_decay);
        break;
    }
    case NodeType::mixer:
    case NodeType::dry_wet_mix:
        break;
    case NodeType::wav_recorder: {
        bool armed = false;
        if (const auto* p = ns.as<node_params::WavRecorder>()) {
            armed = p->armed;
        }
        state.recorder.set_path(record_path_);
        state.recorder.set_armed(armed, sample_rate_);
        state.recorder.write(block, block_size_);
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
