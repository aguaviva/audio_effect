#include "node_state.hpp"

#include "paths.hpp"

#include <cctype>
#include <cmath>
#include <sstream>

namespace {

void append_comma(std::string& out, bool& first) {
    if (!first) {
        out.push_back(',');
    }
    first = false;
}

void write_bool(std::string& out, bool& first, const char* key, bool value) {
    append_comma(out, first);
    out += '"';
    out += key;
    out += "\":";
    out += value ? "true" : "false";
}

void write_float(std::string& out, bool& first, const char* key, float value) {
    append_comma(out, first);
    out += '"';
    out += key;
    out += "\":";
    out += std::to_string(value);
}

void write_int(std::string& out, bool& first, const char* key, int value) {
    append_comma(out, first);
    out += '"';
    out += key;
    out += "\":";
    out += std::to_string(value);
}

void write_string(std::string& out, bool& first, const char* key, const std::string& value) {
    append_comma(out, first);
    out += '"';
    out += key;
    out += "\":\"";
    for (char c : value) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out += '"';
}

constexpr int kKindNumber = 0;
constexpr int kKindBool = 1;
constexpr int kKindString = 2;

bool skip_ws(const std::string& text, size_t& i) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    return i < text.size();
}

bool expect_char(const std::string& text, size_t& i, char ch) {
    if (!skip_ws(text, i) || text[i] != ch) {
        return false;
    }
    ++i;
    return true;
}

bool parse_string_key(const std::string& text, size_t& i, std::string& key) {
    if (!expect_char(text, i, '"')) {
        return false;
    }
    key.clear();
    while (i < text.size()) {
        char c = text[i++];
        if (c == '\\' && i < text.size()) {
            key.push_back(text[i++]);
            continue;
        }
        if (c == '"') {
            return true;
        }
        key.push_back(c);
    }
    return false;
}

bool parse_number_value(const std::string& text, size_t& i, double& out) {
    if (!skip_ws(text, i)) {
        return false;
    }
    size_t start = i;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        ++i;
    }
    while (i < text.size()) {
        const char c = text[i];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == 'e' || c == 'E') {
            ++i;
            continue;
        }
        if ((c == '+' || c == '-') && i > start && (text[i - 1] == 'e' || text[i - 1] == 'E')) {
            ++i;
            continue;
        }
        break;
    }
    if (start == i) {
        return false;
    }
    try {
        out = std::stod(text.substr(start, i - start));
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace

namespace node_params {

void WavPlayer::write(std::string& out, bool& first) const {
    write_bool(out, first, "loop", loop);
    write_string(out, first, "path", path);
}
bool WavPlayer::read(const std::string& key, double, bool flag, const std::string& str, int kind) {
    if (key == "loop" && kind == kKindBool) {
        loop = flag;
        return true;
    }
    if (key == "path" && kind == kKindString) {
        path = str;
        return true;
    }
    return false;
}
void WavPlayer::from_legacy(const float*, const int*, const bool* b) {
    loop = b[0];
}

void AudioInput::write(std::string& out, bool& first) const {
    write_string(out, first, "device", device);
}
bool AudioInput::read(const std::string& key, double, bool, const std::string& str, int kind) {
    if (key == "device" && kind == kKindString) {
        device = str.empty() ? "default" : str;
        return true;
    }
    return false;
}
void AudioInput::from_legacy(const float*, const int*, const bool*) {}

void AudioOutput::write(std::string& out, bool& first) const {
    write_string(out, first, "device", device);
}
bool AudioOutput::read(const std::string& key, double, bool, const std::string& str, int kind) {
    if (key == "device" && kind == kKindString) {
        device = str.empty() ? "default" : str;
        return true;
    }
    return false;
}
void AudioOutput::from_legacy(const float*, const int*, const bool*) {}

void Chord::write(std::string& out, bool& first) const {
    write_int(out, first, "chord_index", index);
}
bool Chord::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (key == "chord_index" && kind == kKindNumber) {
        index = std::clamp(static_cast<int>(num), 0, kChordCount - 1);
        return true;
    }
    return false;
}
void Chord::from_legacy(const float*, const int* i, const bool*) {
    index = std::clamp(i[0], 0, kChordCount - 1);
}

void WhiteNoise::write(std::string& out, bool& first) const {
    write_float(out, first, "level", level);
}
bool WhiteNoise::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (key == "level" && kind == kKindNumber) {
        level = std::clamp(static_cast<float>(num), 0.0f, 1.0f);
        return true;
    }
    return false;
}
void WhiteNoise::from_legacy(const float* f, const int*, const bool*) {
    level = std::clamp(f[0], 0.0f, 1.0f);
}

void Echo::write(std::string& out, bool& first) const {
    write_float(out, first, "delay_ms", delay_ms);
    write_float(out, first, "feedback", feedback);
    write_float(out, first, "mix", mix);
}
bool Echo::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (kind != kKindNumber) {
        return false;
    }
    if (key == "delay_ms") {
        delay_ms = static_cast<float>(num);
        return true;
    }
    if (key == "feedback") {
        feedback = static_cast<float>(num);
        return true;
    }
    if (key == "mix") {
        mix = static_cast<float>(num);
        return true;
    }
    return false;
}
void Echo::from_legacy(const float* f, const int*, const bool*) {
    delay_ms = f[0];
    feedback = f[1];
    mix = f[2];
}

void Preamp::write(std::string& out, bool& first) const {
    write_float(out, first, "gain_db", gain_db);
}
bool Preamp::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (key == "gain_db" && kind == kKindNumber) {
        gain_db = static_cast<float>(num);
        return true;
    }
    return false;
}
void Preamp::from_legacy(const float* f, const int*, const bool*) {
    gain_db = f[0];
}

void HighPass::write(std::string& out, bool& first) const {
    write_float(out, first, "cutoff_hz", cutoff_hz);
}
bool HighPass::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (key == "cutoff_hz" && kind == kKindNumber) {
        cutoff_hz = static_cast<float>(num);
        return true;
    }
    return false;
}
void HighPass::from_legacy(const float* f, const int*, const bool*) {
    cutoff_hz = f[0];
}

void LowPass::write(std::string& out, bool& first) const {
    write_float(out, first, "cutoff_hz", cutoff_hz);
}
bool LowPass::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (key == "cutoff_hz" && kind == kKindNumber) {
        cutoff_hz = static_cast<float>(num);
        return true;
    }
    return false;
}
void LowPass::from_legacy(const float* f, const int*, const bool*) {
    cutoff_hz = f[0];
}

void Drive::write(std::string& out, bool& first) const {
    write_int(out, first, "model", model);
    write_float(out, first, "amount", amount);
    write_float(out, first, "tone", tone);
}
bool Drive::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (kind != kKindNumber) {
        return false;
    }
    if (key == "model") {
        model = static_cast<int>(num);
        return true;
    }
    if (key == "amount") {
        amount = static_cast<float>(num);
        return true;
    }
    if (key == "tone") {
        tone = static_cast<float>(num);
        return true;
    }
    return false;
}
void Drive::from_legacy(const float* f, const int* i, const bool*) {
    model = i[0];
    amount = f[0];
    tone = f[1];
}

void Compressor::write(std::string& out, bool& first) const {
    write_float(out, first, "threshold_db", threshold_db);
    write_float(out, first, "ratio", ratio);
    write_float(out, first, "attack_ms", attack_ms);
    write_float(out, first, "release_ms", release_ms);
    write_float(out, first, "drive", drive);
    write_float(out, first, "makeup_db", makeup_db);
}
bool Compressor::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (kind != kKindNumber) {
        return false;
    }
    if (key == "threshold_db") {
        threshold_db = static_cast<float>(num);
        return true;
    }
    if (key == "ratio") {
        ratio = static_cast<float>(num);
        return true;
    }
    if (key == "attack_ms") {
        attack_ms = static_cast<float>(num);
        return true;
    }
    if (key == "release_ms") {
        release_ms = static_cast<float>(num);
        return true;
    }
    if (key == "drive") {
        drive = static_cast<float>(num);
        return true;
    }
    if (key == "makeup_db") {
        makeup_db = static_cast<float>(num);
        return true;
    }
    return false;
}
void Compressor::from_legacy(const float* f, const int*, const bool*) {
    threshold_db = f[0];
    ratio = f[1];
    attack_ms = f[2];
    release_ms = f[3];
    drive = f[4];
    makeup_db = f[5];
}

void NoiseGate::write(std::string& out, bool& first) const {
    write_float(out, first, "threshold_db", threshold_db);
    write_float(out, first, "attack_ms", attack_ms);
    write_float(out, first, "release_ms", release_ms);
}
bool NoiseGate::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (kind != kKindNumber) {
        return false;
    }
    if (key == "threshold_db") {
        threshold_db = static_cast<float>(num);
        return true;
    }
    if (key == "attack_ms") {
        attack_ms = static_cast<float>(num);
        return true;
    }
    if (key == "release_ms") {
        release_ms = static_cast<float>(num);
        return true;
    }
    return false;
}
void NoiseGate::from_legacy(const float* f, const int*, const bool*) {
    threshold_db = f[0];
    attack_ms = f[2];
    release_ms = f[3];
}

void EqThreeBand::write(std::string& out, bool& first) const {
    write_float(out, first, "low_db", low_db);
    write_float(out, first, "mid_db", mid_db);
    write_float(out, first, "high_db", high_db);
}
bool EqThreeBand::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (kind != kKindNumber) {
        return false;
    }
    if (key == "low_db") {
        low_db = static_cast<float>(num);
        return true;
    }
    if (key == "mid_db") {
        mid_db = static_cast<float>(num);
        return true;
    }
    if (key == "high_db") {
        high_db = static_cast<float>(num);
        return true;
    }
    return false;
}
void EqThreeBand::from_legacy(const float* f, const int*, const bool*) {
    low_db = f[0];
    mid_db = f[1];
    high_db = f[2];
}

void Reverb::write(std::string& out, bool& first) const {
    write_float(out, first, "room", room);
    write_float(out, first, "damping", damping);
    write_float(out, first, "mix", mix);
}
bool Reverb::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (kind != kKindNumber) {
        return false;
    }
    if (key == "room") {
        room = static_cast<float>(num);
        return true;
    }
    if (key == "damping") {
        damping = static_cast<float>(num);
        return true;
    }
    if (key == "mix") {
        mix = static_cast<float>(num);
        return true;
    }
    return false;
}
void Reverb::from_legacy(const float* f, const int*, const bool*) {
    room = f[0];
    damping = f[1];
    mix = f[2];
}

void DryWet::write(std::string& out, bool& first) const {
    write_float(out, first, "mix", mix);
}
bool DryWet::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (key == "mix" && kind == kKindNumber) {
        mix = static_cast<float>(num);
        return true;
    }
    return false;
}
void DryWet::from_legacy(const float* f, const int*, const bool*) {
    mix = f[0];
}

void WavRecorder::write(std::string& out, bool& first) const {
    write_bool(out, first, "armed", armed);
}
bool WavRecorder::read(const std::string& key, double, bool flag, const std::string& /*str*/, int kind) {
    if (key == "armed" && kind == kKindBool) {
        armed = flag;
        return true;
    }
    return false;
}
void WavRecorder::from_legacy(const float*, const int*, const bool* b) {
    armed = b[0];
}

void VuMeter::write(std::string& out, bool& first) const {
    write_float(out, first, "display_gain", display_gain);
    write_float(out, first, "decay", decay);
}
bool VuMeter::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (kind != kKindNumber) {
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
void VuMeter::from_legacy(const float* f, const int*, const bool*) {
    display_gain = f[0];
    decay = f[1];
}

void Oscilloscope::write(std::string& out, bool& first) const {
    write_float(out, first, "display_gain", display_gain);
}
bool Oscilloscope::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (key == "display_gain" && kind == kKindNumber) {
        display_gain = static_cast<float>(num);
        return true;
    }
    return false;
}
void Oscilloscope::from_legacy(const float* f, const int*, const bool*) {
    display_gain = f[0];
}

void Spectrum::write(std::string& out, bool& first) const {
    write_float(out, first, "display_gain", display_gain);
}
bool Spectrum::read(const std::string& key, double num, bool, const std::string& /*str*/, int kind) {
    if (key == "display_gain" && kind == kKindNumber) {
        display_gain = static_cast<float>(num);
        return true;
    }
    return false;
}
void Spectrum::from_legacy(const float* f, const int*, const bool*) {
    display_gain = f[0];
}

// --- module JSON object save/load (fields only) ---

template <typename T>
bool parse_module_object(const std::string& text, size_t& i, T& module) {
    if (!expect_char(text, i, '{')) {
        return false;
    }
    while (true) {
        if (!skip_ws(text, i)) {
            return false;
        }
        if (text[i] == '}') {
            ++i;
            return true;
        }
        std::string key;
        if (!parse_string_key(text, i, key) || !expect_char(text, i, ':')) {
            return false;
        }
        if (!skip_ws(text, i)) {
            return false;
        }
        if (text[i] == '"') {
            std::string str;
            if (!parse_string_key(text, i, str)) {
                return false;
            }
            module.read(key, 0.0, false, str, kKindString);
        } else if (text.compare(i, 4, "true") == 0 || text.compare(i, 5, "false") == 0) {
            const bool flag = text.compare(i, 4, "true") == 0;
            i += flag ? 4 : 5;
            module.read(key, 0.0, flag, std::string{}, kKindBool);
        } else {
            double num = 0.0;
            if (!parse_number_value(text, i, num)) {
                return false;
            }
            module.read(key, num, false, std::string{}, kKindNumber);
        }
        if (!skip_ws(text, i)) {
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

template <typename T>
std::string save_module_json(const T& module) {
    std::string out = "{";
    bool first = true;
    module.write(out, first);
    out += '}';
    return out;
}

#define DEFINE_MODULE_JSON(Type)                                                                   \
    std::string Type::save_json() const { return save_module_json(*this); }                        \
    bool Type::load_json(const std::string& text, size_t& i) { return parse_module_object(text, i, *this); }

DEFINE_MODULE_JSON(None)
DEFINE_MODULE_JSON(WavPlayer)
DEFINE_MODULE_JSON(AudioInput)
DEFINE_MODULE_JSON(AudioOutput)
DEFINE_MODULE_JSON(Chord)
DEFINE_MODULE_JSON(WhiteNoise)
DEFINE_MODULE_JSON(Echo)
DEFINE_MODULE_JSON(Preamp)
DEFINE_MODULE_JSON(HighPass)
DEFINE_MODULE_JSON(LowPass)
DEFINE_MODULE_JSON(Drive)
DEFINE_MODULE_JSON(Compressor)
DEFINE_MODULE_JSON(NoiseGate)
DEFINE_MODULE_JSON(EqThreeBand)
DEFINE_MODULE_JSON(Reverb)
DEFINE_MODULE_JSON(DryWet)
DEFINE_MODULE_JSON(WavRecorder)
DEFINE_MODULE_JSON(VuMeter)
DEFINE_MODULE_JSON(Oscilloscope)
DEFINE_MODULE_JSON(Spectrum)
DEFINE_MODULE_JSON(PitchDetector)
DEFINE_MODULE_JSON(Mixer)

bool try_resolve_name(const std::string& module_name, NodeType& out) {
    if (module_name == WavPlayer::name()) { out = NodeType::wav_player; return true; }
    if (module_name == AudioInput::name()) { out = NodeType::audio_input; return true; }
    if (module_name == AudioOutput::name()) { out = NodeType::output; return true; }
    if (module_name == Chord::name()) { out = NodeType::chord_generator; return true; }
    if (module_name == WhiteNoise::name()) { out = NodeType::white_noise; return true; }
    if (module_name == Echo::name()) { out = NodeType::echo; return true; }
    if (module_name == Preamp::name()) { out = NodeType::preamp; return true; }
    if (module_name == HighPass::name()) { out = NodeType::high_pass; return true; }
    if (module_name == LowPass::name()) { out = NodeType::low_pass; return true; }
    if (module_name == Drive::name()) { out = NodeType::drive_pedal; return true; }
    if (module_name == Compressor::name()) { out = NodeType::compressor; return true; }
    if (module_name == NoiseGate::name()) { out = NodeType::noise_gate; return true; }
    if (module_name == EqThreeBand::name()) { out = NodeType::eq_three_band; return true; }
    if (module_name == Reverb::name()) { out = NodeType::reverb; return true; }
    if (module_name == DryWet::name()) { out = NodeType::dry_wet_mix; return true; }
    if (module_name == WavRecorder::name()) { out = NodeType::wav_recorder; return true; }
    if (module_name == VuMeter::name()) { out = NodeType::vu_meter; return true; }
    if (module_name == Oscilloscope::name()) { out = NodeType::oscilloscope; return true; }
    if (module_name == Spectrum::name()) { out = NodeType::spectrum; return true; }
    if (module_name == PitchDetector::name()) { out = NodeType::pitch_detector; return true; }
    if (module_name == Mixer::name()) { out = NodeType::mixer; return true; }
    return false;
}

std::string name_for_type(NodeType type) {
    switch (type) {
    case NodeType::wav_player: return WavPlayer::name();
    case NodeType::audio_input: return AudioInput::name();
    case NodeType::output: return AudioOutput::name();
    case NodeType::chord_generator: return Chord::name();
    case NodeType::white_noise: return WhiteNoise::name();
    case NodeType::echo: return Echo::name();
    case NodeType::preamp: return Preamp::name();
    case NodeType::high_pass: return HighPass::name();
    case NodeType::low_pass: return LowPass::name();
    case NodeType::drive_pedal: return Drive::name();
    case NodeType::compressor: return Compressor::name();
    case NodeType::noise_gate: return NoiseGate::name();
    case NodeType::eq_three_band: return EqThreeBand::name();
    case NodeType::reverb: return Reverb::name();
    case NodeType::dry_wet_mix: return DryWet::name();
    case NodeType::wav_recorder: return WavRecorder::name();
    case NodeType::vu_meter: return VuMeter::name();
    case NodeType::oscilloscope: return Oscilloscope::name();
    case NodeType::spectrum: return Spectrum::name();
    case NodeType::pitch_detector: return PitchDetector::name();
    case NodeType::mixer: return Mixer::name();
    default: return None::name();
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

std::string NodeState::module_name() const {
    return std::visit([](const auto& p) { return p.name(); }, params);
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
    write_bool(out, first, "bypassed", bypassed);
    bool any_muted = false;
    for (bool m : input_muted) {
        if (m) {
            any_muted = true;
            break;
        }
    }
    if (any_muted) {
        append_comma(out, first);
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
    if (!expect_char(text, i, '{')) {
        return false;
    }

    // Detect legacy {"f":[...],"i":[...],"b":[...]}
    size_t look = i;
    skip_ws(text, look);
    if (look + 3 <= text.size() && text[look] == '"' && text.compare(look + 1, 1, "f") == 0) {
        // Fall through to legacy parse below via f/i/b keys.
    }

    float legacy_f[8]{};
    int legacy_i[4]{};
    bool legacy_b[4]{};
    bool saw_legacy = false;

    while (true) {
        if (!skip_ws(text, i)) {
            return false;
        }
        if (text[i] == '}') {
            ++i;
            if (saw_legacy) {
                from_legacy_arrays(type, legacy_f, legacy_i, legacy_b);
            }
            return true;
        }
        std::string key;
        if (!parse_string_key(text, i, key) || !expect_char(text, i, ':')) {
            return false;
        }
        if (!skip_ws(text, i)) {
            return false;
        }

        if (key == "muted") {
            if (!expect_char(text, i, '[')) {
                return false;
            }
            int idx = 0;
            while (true) {
                if (!skip_ws(text, i)) {
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
                    if (!parse_number_value(text, i, num)) {
                        return false;
                    }
                    if (idx >= 0 && idx < kMaxMixerInputs) {
                        input_muted[static_cast<size_t>(idx)] = num != 0.0;
                    }
                }
                ++idx;
                if (!skip_ws(text, i)) {
                    return false;
                }
                if (text[i] == ',') {
                    ++i;
                }
            }
        } else if (key == "input_gain") {
            if (!expect_char(text, i, '[')) {
                return false;
            }
            int idx = 0;
            while (true) {
                if (!skip_ws(text, i)) {
                    return false;
                }
                if (text[i] == ']') {
                    ++i;
                    break;
                }
                double num = 0.0;
                if (!parse_number_value(text, i, num)) {
                    return false;
                }
                if (idx < kMaxMixerInputs) {
                    input_gain[static_cast<size_t>(idx)] =
                        std::clamp(static_cast<float>(num), 0.0f, 2.0f);
                }
                ++idx;
                if (!skip_ws(text, i)) {
                    return false;
                }
                if (text[i] == ',') {
                    ++i;
                }
            }
        } else if (key == "f" || key == "i" || key == "b") {
            saw_legacy = true;
            if (!expect_char(text, i, '[')) {
                return false;
            }
            int idx = 0;
            while (true) {
                if (!skip_ws(text, i)) {
                    return false;
                }
                if (text[i] == ']') {
                    ++i;
                    break;
                }
                if (key == "b") {
                    if (text.compare(i, 4, "true") == 0) {
                        if (idx < 4) {
                            legacy_b[idx] = true;
                        }
                        i += 4;
                    } else if (text.compare(i, 5, "false") == 0) {
                        if (idx < 4) {
                            legacy_b[idx] = false;
                        }
                        i += 5;
                    } else {
                        return false;
                    }
                } else {
                    double num = 0.0;
                    if (!parse_number_value(text, i, num)) {
                        return false;
                    }
                    if (key == "f" && idx < 8) {
                        legacy_f[idx] = static_cast<float>(num);
                    }
                    if (key == "i" && idx < 4) {
                        legacy_i[idx] = static_cast<int>(num);
                    }
                }
                ++idx;
                if (!skip_ws(text, i)) {
                    return false;
                }
                if (text[i] == ',') {
                    ++i;
                }
            }
        } else if (text[i] == '"') {
            std::string str;
            if (!parse_string_key(text, i, str)) {
                return false;
            }
            std::visit([&](auto& p) { p.read(key, 0.0, false, str, kKindString); }, params);
        } else if (text.compare(i, 4, "true") == 0 || text.compare(i, 5, "false") == 0) {
            const bool flag = text.compare(i, 4, "true") == 0;
            i += flag ? 4 : 5;
            if (key == "bypassed") {
                bypassed = flag;
            } else {
                std::visit([&](auto& p) { p.read(key, 0.0, flag, std::string{}, kKindBool); }, params);
            }
        } else {
            double num = 0.0;
            if (!parse_number_value(text, i, num)) {
                return false;
            }
            if (key == "bypassed") {
                bypassed = num != 0.0;
            } else {
                std::visit([&](auto& p) { p.read(key, num, false, std::string{}, kKindNumber); },
                           params);
            }
        }

        if (!skip_ws(text, i)) {
            return false;
        }
        if (text[i] == ',') {
            ++i;
            continue;
        }
        if (text[i] == '}') {
            ++i;
            if (saw_legacy) {
                from_legacy_arrays(type, legacy_f, legacy_i, legacy_b);
            }
            return true;
        }
        return false;
    }
}

void NodeState::from_legacy_arrays(NodeType type, const float* f, const int* i, const bool* b) {
    *this = make(type);
    bypassed = b[3];
    std::visit([&](auto& p) { p.from_legacy(f, i, b); }, params);
}
