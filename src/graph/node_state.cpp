#include "node_state.hpp"

#include "paths.hpp"

#include <algorithm>

namespace node_params {

void WavPlayer::write(std::string& out, bool& first) const {
    module_json::write_bool(out, first, "loop", loop);
    module_json::write_string(out, first, "path", path);
}
bool WavPlayer::read(const std::string& key, double, bool flag, const std::string& str, int kind) {
    if (key == "loop" && kind == module_json::kKindBool) {
        loop = flag;
        return true;
    }
    if (key == "path" && kind == module_json::kKindString) {
        path = str;
        return true;
    }
    return false;
}

void AudioInput::write(std::string& out, bool& first) const {
    module_json::write_string(out, first, "device", device);
}
bool AudioInput::read(const std::string& key, double, bool, const std::string& str, int kind) {
    if (key == "device" && kind == module_json::kKindString) {
        device = str.empty() ? "default" : str;
        return true;
    }
    return false;
}

void AudioOutput::write(std::string& out, bool& first) const {
    module_json::write_string(out, first, "device", device);
}
bool AudioOutput::read(const std::string& key, double, bool, const std::string& str, int kind) {
    if (key == "device" && kind == module_json::kKindString) {
        device = str.empty() ? "default" : str;
        return true;
    }
    return false;
}

void Chord::write(std::string& out, bool& first) const {
    module_json::write_int(out, first, "chord_index", index);
}
bool Chord::read(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "chord_index" && kind == module_json::kKindNumber) {
        index = std::clamp(static_cast<int>(num), 0, kChordCount - 1);
        return true;
    }
    return false;
}

void WhiteNoise::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "level", level);
}
bool WhiteNoise::read(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "level" && kind == module_json::kKindNumber) {
        level = std::clamp(static_cast<float>(num), 0.0f, 1.0f);
        return true;
    }
    return false;
}

void DryWet::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "mix", mix);
}
bool DryWet::read(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "mix" && kind == module_json::kKindNumber) {
        mix = static_cast<float>(num);
        return true;
    }
    return false;
}

void VuMeter::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "display_gain", display_gain);
    module_json::write_float(out, first, "decay", decay);
}
bool VuMeter::read(const std::string& key, double num, bool, const std::string&, int kind) {
    if (kind != module_json::kKindNumber) {
        return false;
    }
    if (key == "display_gain") {
        display_gain = static_cast<float>(num);
        return true;
    }
    if (key == "decay") {
        decay = static_cast<float>(num);
        return true;
    }
    return false;
}

void Oscilloscope::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "display_gain", display_gain);
}
bool Oscilloscope::read(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "display_gain" && kind == module_json::kKindNumber) {
        display_gain = static_cast<float>(num);
        return true;
    }
    return false;
}

void Spectrum::write(std::string& out, bool& first) const {
    module_json::write_float(out, first, "display_gain", display_gain);
}
bool Spectrum::read(const std::string& key, double num, bool, const std::string&, int kind) {
    if (key == "display_gain" && kind == module_json::kKindNumber) {
        display_gain = static_cast<float>(num);
        return true;
    }
    return false;
}

bool try_resolve_name(const std::string& module_name, NodeType& out) {
    if (module_name == WavPlayer::name()) {
        out = NodeType::wav_player;
        return true;
    }
    if (module_name == AudioInput::name()) {
        out = NodeType::audio_input;
        return true;
    }
    if (module_name == AudioOutput::name()) {
        out = NodeType::output;
        return true;
    }
    if (module_name == Chord::name()) {
        out = NodeType::chord_generator;
        return true;
    }
    if (module_name == WhiteNoise::name()) {
        out = NodeType::white_noise;
        return true;
    }
    if (module_name == EchoEffect::name()) {
        out = NodeType::echo;
        return true;
    }
    if (module_name == ::Preamp::name()) {
        out = NodeType::preamp;
        return true;
    }
    if (module_name == DynamicHighPassFilter::name()) {
        out = NodeType::high_pass;
        return true;
    }
    if (module_name == DynamicLowPassFilter::name()) {
        out = NodeType::low_pass;
        return true;
    }
    if (module_name == guitar_pedals::DrivePedal::name()) {
        out = NodeType::drive_pedal;
        return true;
    }
    if (module_name == ValveCompressor::name()) {
        out = NodeType::compressor;
        return true;
    }
    if (module_name == ::NoiseGate::name()) {
        out = NodeType::noise_gate;
        return true;
    }
    if (module_name == ::EqThreeBand::name()) {
        out = NodeType::eq_three_band;
        return true;
    }
    if (module_name == SimpleReverb::name()) {
        out = NodeType::reverb;
        return true;
    }
    if (module_name == DryWet::name()) {
        out = NodeType::dry_wet_mix;
        return true;
    }
    if (module_name == ::WavRecorder::name()) {
        out = NodeType::wav_recorder;
        return true;
    }
    if (module_name == VuMeter::name()) {
        out = NodeType::vu_meter;
        return true;
    }
    if (module_name == Oscilloscope::name()) {
        out = NodeType::oscilloscope;
        return true;
    }
    if (module_name == Spectrum::name()) {
        out = NodeType::spectrum;
        return true;
    }
    if (module_name == PitchDetector::name()) {
        out = NodeType::pitch_detector;
        return true;
    }
    if (module_name == Mixer::name()) {
        out = NodeType::mixer;
        return true;
    }
    return false;
}

std::string name_for_type(NodeType type) {
    switch (type) {
    case NodeType::wav_player:
        return WavPlayer::name();
    case NodeType::audio_input:
        return AudioInput::name();
    case NodeType::output:
        return AudioOutput::name();
    case NodeType::chord_generator:
        return Chord::name();
    case NodeType::white_noise:
        return WhiteNoise::name();
    case NodeType::echo:
        return EchoEffect::name();
    case NodeType::preamp:
        return ::Preamp::name();
    case NodeType::high_pass:
        return DynamicHighPassFilter::name();
    case NodeType::low_pass:
        return DynamicLowPassFilter::name();
    case NodeType::drive_pedal:
        return guitar_pedals::DrivePedal::name();
    case NodeType::compressor:
        return ValveCompressor::name();
    case NodeType::noise_gate:
        return ::NoiseGate::name();
    case NodeType::eq_three_band:
        return ::EqThreeBand::name();
    case NodeType::reverb:
        return SimpleReverb::name();
    case NodeType::dry_wet_mix:
        return DryWet::name();
    case NodeType::wav_recorder:
        return ::WavRecorder::name();
    case NodeType::vu_meter:
        return VuMeter::name();
    case NodeType::oscilloscope:
        return Oscilloscope::name();
    case NodeType::spectrum:
        return Spectrum::name();
    case NodeType::pitch_detector:
        return PitchDetector::name();
    case NodeType::mixer:
        return Mixer::name();
    default:
        return None::name();
    }
}

} // namespace node_params

NodeState NodeState::make(NodeType type) {
    NodeState s;
    switch (type) {
    case NodeType::wav_player:
        s.params = node_params::WavPlayer{true, kDefaultWavPath};
        break;
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

namespace {

std::string name_of(const node_params::None&) { return node_params::None::name(); }
std::string name_of(const node_params::WavPlayer&) { return node_params::WavPlayer::name(); }
std::string name_of(const node_params::AudioInput&) { return node_params::AudioInput::name(); }
std::string name_of(const node_params::AudioOutput&) { return node_params::AudioOutput::name(); }
std::string name_of(const node_params::Chord&) { return node_params::Chord::name(); }
std::string name_of(const node_params::WhiteNoise&) { return node_params::WhiteNoise::name(); }
std::string name_of(const node_params::Echo&) { return EchoEffect::name(); }
std::string name_of(const node_params::Preamp&) { return ::Preamp::name(); }
std::string name_of(const node_params::HighPass&) { return DynamicHighPassFilter::name(); }
std::string name_of(const node_params::LowPass&) { return DynamicLowPassFilter::name(); }
std::string name_of(const node_params::Drive&) { return guitar_pedals::DrivePedal::name(); }
std::string name_of(const node_params::Compressor&) { return ValveCompressor::name(); }
std::string name_of(const node_params::NoiseGate&) { return ::NoiseGate::name(); }
std::string name_of(const node_params::EqThreeBand&) { return ::EqThreeBand::name(); }
std::string name_of(const node_params::Reverb&) { return SimpleReverb::name(); }
std::string name_of(const node_params::DryWet&) { return node_params::DryWet::name(); }
std::string name_of(const node_params::WavRecorder&) { return ::WavRecorder::name(); }
std::string name_of(const node_params::VuMeter&) { return node_params::VuMeter::name(); }
std::string name_of(const node_params::Oscilloscope&) { return node_params::Oscilloscope::name(); }
std::string name_of(const node_params::Spectrum&) { return node_params::Spectrum::name(); }
std::string name_of(const node_params::PitchDetector&) { return node_params::PitchDetector::name(); }
std::string name_of(const node_params::Mixer&) { return node_params::Mixer::name(); }

} // namespace

std::string NodeState::module_name() const {
    return std::visit([](const auto& p) { return name_of(p); }, params);
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
    bool first = true;
    module_json::write_bool(out, first, "bypassed", bypassed);
    bool any_muted = false;
    for (bool m : input_muted) {
        if (m) {
            any_muted = true;
            break;
        }
    }
    if (any_muted) {
        module_json::append_comma(out, first);
        out += "\"muted\":[";
        for (size_t i = 0; i < input_muted.size(); ++i) {
            if (i > 0) {
                out.push_back(',');
            }
            out += input_muted[i] ? "true" : "false";
        }
        out += ']';
    }
    bool any_non_unity_gain = false;
    for (float gain : input_gain) {
        if (gain != 1.0f) {
            any_non_unity_gain = true;
            break;
        }
    }
    if (any_non_unity_gain) {
        append_comma(out, first);
        out += "\"input_gain\":[";
        for (size_t i = 0; i < input_gain.size(); ++i) {
            if (i > 0) {
                out.push_back(',');
            }
            out += std::to_string(input_gain[i]);
        }
        out += ']';
    }
    std::visit([&](const auto& p) { p.write(out, first); }, params);
    out += '}';
    return out;
}

bool NodeState::read_params(NodeType type, const std::string& text, size_t& i) {
    *this = make(type);
    if (!module_json::expect_char(text, i, '{')) {
        return false;
    }

    while (true) {
        if (!module_json::skip_ws(text, i)) {
            return false;
        }
        if (text[i] == '}') {
            ++i;
            return true;
        }
        std::string key;
        if (!module_json::parse_string_key(text, i, key) || !module_json::expect_char(text, i, ':')) {
            return false;
        }
        if (!module_json::skip_ws(text, i)) {
            return false;
        }

        if (key == "muted") {
            if (!module_json::expect_char(text, i, '[')) {
                return false;
            }
            int idx = 0;
            while (true) {
                if (!module_json::skip_ws(text, i)) {
                    return false;
                }
                if (text[i] == ']') {
                    ++i;
                    break;
                }
                if (text.compare(i, 4, "true") == 0) {
                    if (idx >= 0 && idx < kMaxMixerInputs) {
                        input_muted[static_cast<size_t>(idx)] = true;
                    }
                    i += 4;
                } else if (text.compare(i, 5, "false") == 0) {
                    if (idx >= 0 && idx < kMaxMixerInputs) {
                        input_muted[static_cast<size_t>(idx)] = false;
                    }
                    i += 5;
                } else {
                    double num = 0.0;
                    if (!module_json::parse_number_value(text, i, num)) {
                        return false;
                    }
                    if (idx >= 0 && idx < kMaxMixerInputs) {
                        input_muted[static_cast<size_t>(idx)] = num != 0.0;
                    }
                }
                ++idx;
                if (!module_json::skip_ws(text, i)) {
                    return false;
                }
                if (text[i] == ',') {
                    ++i;
                }
            }
        } else if (text[i] == '"') {
            std::string str;
            if (!module_json::parse_string_key(text, i, str)) {
                return false;
            }
            std::visit([&](auto& p) { p.read(key, 0.0, false, str, module_json::kKindString); },
                       params);
        } else if (text.compare(i, 4, "true") == 0 || text.compare(i, 5, "false") == 0) {
            const bool flag = text.compare(i, 4, "true") == 0;
            i += flag ? 4 : 5;
            if (key == "bypassed") {
                bypassed = flag;
            } else {
                std::visit(
                    [&](auto& p) { p.read(key, 0.0, flag, std::string{}, module_json::kKindBool); },
                    params);
            }
        } else {
            double num = 0.0;
            if (!module_json::parse_number_value(text, i, num)) {
                return false;
            }
            if (key == "bypassed") {
                bypassed = num != 0.0;
            } else {
                std::visit(
                    [&](auto& p) {
                        p.read(key, num, false, std::string{}, module_json::kKindNumber);
                    },
                    params);
            }
        }

        if (!module_json::skip_ws(text, i)) {
            return false;
        }
        if (text[i] == ',') {
            ++i;
            continue;
        }
        if (text[i] == '}') {
            ++i;
            return true;
        }
        return false;
    }
}
