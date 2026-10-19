#include "node_state.hpp"

#include "paths.hpp"

#include <algorithm>

namespace node_params {

bool try_resolve_name(const std::string& module_name, NodeType& out) {
    if (module_name == WavPlayer{}.name()) {
        out = NodeType::wav_player;
        return true;
    }
    if (module_name == AudioInput{}.name()) {
        out = NodeType::audio_input;
        return true;
    }
    if (module_name == AudioOutput{}.name()) {
        out = NodeType::output;
        return true;
    }
    if (module_name == Chord{}.name()) {
        out = NodeType::chord_generator;
        return true;
    }
    if (module_name == WhiteNoise{}.name()) {
        out = NodeType::white_noise;
        return true;
    }
    if (module_name == Echo{}.name()) {
        out = NodeType::echo;
        return true;
    }
    if (module_name == Preamp{}.name()) {
        out = NodeType::preamp;
        return true;
    }
    if (module_name == HighPass{}.name()) {
        out = NodeType::high_pass;
        return true;
    }
    if (module_name == LowPass{}.name()) {
        out = NodeType::low_pass;
        return true;
    }
    if (module_name == Drive{}.name()) {
        out = NodeType::drive_pedal;
        return true;
    }
    if (module_name == Compressor{}.name()) {
        out = NodeType::compressor;
        return true;
    }
    if (module_name == NoiseGate{}.name()) {
        out = NodeType::noise_gate;
        return true;
    }
    if (module_name == EqThreeBand{}.name()) {
        out = NodeType::eq_three_band;
        return true;
    }
    if (module_name == Reverb{}.name()) {
        out = NodeType::reverb;
        return true;
    }
    if (module_name == DryWet{}.name()) {
        out = NodeType::dry_wet_mix;
        return true;
    }
    if (module_name == WavRecorder{}.name()) {
        out = NodeType::wav_recorder;
        return true;
    }
    if (module_name == VuMeter{}.name()) {
        out = NodeType::vu_meter;
        return true;
    }
    if (module_name == Oscilloscope{}.name()) {
        out = NodeType::oscilloscope;
        return true;
    }
    if (module_name == Spectrum{}.name()) {
        out = NodeType::spectrum;
        return true;
    }
    if (module_name == PitchDetector{}.name()) {
        out = NodeType::pitch_detector;
        return true;
    }
    if (module_name == Mixer{}.name()) {
        out = NodeType::mixer;
        return true;
    }
    return false;
}

std::string name_for_type(NodeType type) {
    return NodeState::make(type).module_name();
}

} // namespace node_params

NodeState NodeState::make(NodeType type) {
    NodeState s;
    switch (type) {
    case NodeType::wav_player: {
        node_params::WavPlayer m;
        m.loop = true;
        m.path = kDefaultWavPath;
        s.params = std::move(m);
        break;
    }
    case NodeType::audio_input:
        s.params = node_params::AudioInput{};
        break;
    case NodeType::output:
        s.params = node_params::AudioOutput{};
        break;
    case NodeType::chord_generator:
        s.params = node_params::Chord{};
        break;
    case NodeType::white_noise:
        s.params = node_params::WhiteNoise{};
        break;
    case NodeType::echo:
        s.params = node_params::Echo{};
        break;
    case NodeType::preamp:
        s.params = node_params::Preamp{};
        break;
    case NodeType::high_pass:
        s.params = node_params::HighPass{};
        break;
    case NodeType::low_pass:
        s.params = node_params::LowPass{};
        break;
    case NodeType::drive_pedal:
        s.params = node_params::Drive{};
        break;
    case NodeType::compressor:
        s.params = node_params::Compressor{};
        break;
    case NodeType::noise_gate:
        s.params = node_params::NoiseGate{};
        break;
    case NodeType::eq_three_band:
        s.params = node_params::EqThreeBand{};
        break;
    case NodeType::reverb:
        s.params = node_params::Reverb{};
        break;
    case NodeType::dry_wet_mix:
        s.params = node_params::DryWet{};
        break;
    case NodeType::wav_recorder:
        s.params = node_params::WavRecorder{};
        break;
    case NodeType::vu_meter:
        s.params = node_params::VuMeter{};
        break;
    case NodeType::oscilloscope:
        s.params = node_params::Oscilloscope{};
        break;
    case NodeType::spectrum:
        s.params = node_params::Spectrum{};
        break;
    case NodeType::pitch_detector:
        s.params = node_params::PitchDetector{};
        break;
    case NodeType::mixer:
        s.params = node_params::Mixer{};
        break;
    default:
        s.params = node_params::None{};
        break;
    }
    return s;
}

std::string NodeState::module_name() const {
    return std::visit([](const auto& m) { return m.name(); }, params);
}

bool NodeState::load_by_module_name(const std::string& module_name, const std::string& text,
                                    size_t& i) {
    NodeType type = NodeType::preamp;
    if (!node_params::try_resolve_name(module_name, type)) {
        return false;
    }
    return read_params(type, text, i);
}

std::string NodeState::write_params() const {
    std::string out = "{";
    module_json::write_bool(out, "bypassed", bypassed);
    bool any_muted = false;
    for (bool m : input_muted) {
        if (m) {
            any_muted = true;
            break;
        }
    }
    if (any_muted) {
        module_json::append_comma(out);
        out += "\"muted\":[";
        for (size_t i = 0; i < input_muted.size(); ++i) {
            if (i > 0) {
                out.push_back(',');
            }
            out += input_muted[i] ? "true" : "false";
        }
        out += ']';
    }
    std::visit(
        [&](const auto& m) { module_json::append_object_members(out, m.save()); }, params);
    out += '}';
    return out;
}

bool NodeState::read_params(NodeType type, const std::string& text, size_t& i) {
    *this = make(type);
    if (!module_json::skip_ws(text, i) || text[i] != '{') {
        return false;
    }
    const size_t start = i;
    if (!module_json::skip_value(text, i)) {
        return false;
    }
    const std::string obj = text.substr(start, i - start);

    const bool node_ok = module_json::for_each_member(
        obj, [&](const std::string& key, const std::string& body, size_t& j) -> bool {
            if (key == "bypassed") {
                bool flag = false;
                if (module_json::parse_bool_value(body, j, flag)) {
                    bypassed = flag;
                    return true;
                }
                double num = 0.0;
                if (!module_json::parse_number_value(body, j, num)) {
                    return false;
                }
                bypassed = num != 0.0;
                return true;
            }
            if (key == "muted") {
                if (!module_json::expect_char(body, j, '[')) {
                    return false;
                }
                int idx = 0;
                while (true) {
                    if (!module_json::skip_ws(body, j)) {
                        return false;
                    }
                    if (body[j] == ']') {
                        ++j;
                        break;
                    }
                    bool flag = false;
                    if (module_json::parse_bool_value(body, j, flag)) {
                        if (idx >= 0 && idx < kMaxMixerInputs) {
                            input_muted[static_cast<size_t>(idx)] = flag;
                        }
                    } else {
                        double num = 0.0;
                        if (!module_json::parse_number_value(body, j, num)) {
                            return false;
                        }
                        if (idx >= 0 && idx < kMaxMixerInputs) {
                            input_muted[static_cast<size_t>(idx)] = num != 0.0;
                        }
                    }
                    ++idx;
                    if (!module_json::skip_ws(body, j)) {
                        return false;
                    }
                    if (body[j] == ',') {
                        ++j;
                    }
                }
                return true;
            }
            return module_json::skip_value(body, j);
        });
    if (!node_ok) {
        return false;
    }
    return std::visit([&](auto& m) { return m.load(obj); }, params);
}
