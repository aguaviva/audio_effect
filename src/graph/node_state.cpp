#include "node_state.hpp"

#include "paths.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace node_params {

namespace {

const std::vector<std::pair<std::string, NodeType>>& name_table() {
    static const std::vector<std::pair<std::string, NodeType>> table = [] {
        std::vector<std::pair<std::string, NodeType>> out;
        for (int i = 0; i < static_cast<int>(NodeType::count); ++i) {
            const auto type = static_cast<NodeType>(i);
            const std::string name = NodeState::make(type).module_name();
            if (name != None{}.name()) {
                out.emplace_back(name, type);
            }
        }
        return out;
    }();
    return table;
}

} // namespace

bool try_resolve_name(const std::string& module_name, NodeType& out) {
    for (const auto& [name, type] : name_table()) {
        if (name == module_name) {
            out = type;
            return true;
        }
    }
    return false;
}

std::string name_for_type(NodeType type) {
    for (const auto& [name, t] : name_table()) {
        if (t == type) {
            return name;
        }
    }
    return None{}.name();
}

} // namespace node_params

template <typename T>
static std::shared_ptr<Module> make_module() {
    return std::make_shared<T>();
}

NodeState NodeState::make(NodeType type) {
    NodeState s;
    switch (type) {
    case NodeType::wav_player: {
        auto m = std::make_shared<node_params::WavPlayer>();
        m->loop = true;
        m->set_path(kDefaultWavPath);
        s.module_ = std::move(m);
        break;
    }
    case NodeType::audio_input: s.module_ = make_module<node_params::AudioInput>(); break;
    case NodeType::output: s.module_ = make_module<node_params::AudioOutput>(); break;
    case NodeType::chord_generator: s.module_ = make_module<node_params::Chord>(); break;
    case NodeType::white_noise: s.module_ = make_module<node_params::WhiteNoise>(); break;
    case NodeType::echo: s.module_ = make_module<node_params::Echo>(); break;
    case NodeType::preamp: s.module_ = make_module<node_params::Preamp>(); break;
    case NodeType::high_pass: s.module_ = make_module<node_params::HighPass>(); break;
    case NodeType::low_pass: s.module_ = make_module<node_params::LowPass>(); break;
    case NodeType::drive_pedal: s.module_ = make_module<node_params::Drive>(); break;
    case NodeType::compressor: s.module_ = make_module<node_params::Compressor>(); break;
    case NodeType::noise_gate: s.module_ = make_module<node_params::NoiseGate>(); break;
    case NodeType::eq_three_band: s.module_ = make_module<node_params::EqThreeBand>(); break;
    case NodeType::reverb: s.module_ = make_module<node_params::Reverb>(); break;
    case NodeType::dry_wet_mix: s.module_ = make_module<node_params::DryWet>(); break;
    case NodeType::wav_recorder: s.module_ = make_module<node_params::WavRecorder>(); break;
    case NodeType::vu_meter: s.module_ = make_module<node_params::VuMeter>(); break;
    case NodeType::oscilloscope: s.module_ = make_module<node_params::Oscilloscope>(); break;
    case NodeType::spectrum: s.module_ = make_module<node_params::Spectrum>(); break;
    case NodeType::pitch_detector: s.module_ = make_module<node_params::PitchDetector>(); break;
    case NodeType::mixer: s.module_ = make_module<node_params::Mixer>(); break;
    default: s.module_ = make_module<node_params::None>(); break;
    }
    return s;
}

std::string NodeState::module_name() const {
    return module_->name();
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
    module_json::append_object_members(out, module_->save());
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
    return module_->load(obj);
}
