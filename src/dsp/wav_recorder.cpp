#include "wav_recorder.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#include "imgui.h"

namespace {

constexpr uint64_t kWavHeaderBytes = 44;
// RIFF sizes are 32-bit and count everything after the first 8 bytes.
constexpr uint64_t kMaxDataBytes = 0xffffffffull - (kWavHeaderBytes - 8);

} // namespace

std::string WavRecorder::save() const {
    std::string out = "{";
    module_json::write_string(out, "path", path);
    out += '}';
    return out;
}

bool WavRecorder::load(const std::string& json) {
    module_json::read(json, "path", path);
    return true;
}

bool WavRecorder::render() {
    const bool changed = ImGui::Checkbox("Arm Record", &armed);
    ImGui::TextWrapped("%s", path.c_str());
    return changed;
}

void WavRecorder::process(const float* const* inputs, int input_count, float* output,
                          size_t sample_count, float sample_rate) {
    copy_input(inputs, input_count, 0, output, sample_count);
    engine_.set_armed(armed, path, sample_rate);
    engine_.write_samples(output, sample_count);
}

WavRecorderEngine::~WavRecorderEngine() {
    close_file();
}

void WavRecorderEngine::set_armed(bool armed, const std::string& path, float sample_rate) {
    if (armed == armed_) {
        return;
    }
    if (armed) {
        sample_rate_ = static_cast<uint32_t>(std::max(1.0f, sample_rate));
        armed_ = open_file(path);
    } else {
        close_file();
        armed_ = false;
    }
}

bool WavRecorderEngine::open_file(const std::string& path) {
    close_file();
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_) {
        return false;
    }
    data_bytes_ = 0;
    std::vector<char> header(44, 0);
    file_.write(header.data(), static_cast<std::streamsize>(header.size()));
    return static_cast<bool>(file_);
}

void WavRecorderEngine::patch_header() {
    if (!file_) {
        return;
    }
    const uint32_t byte_rate = sample_rate_ * 2 * 16 / 8;
    const uint16_t block_align = 2 * 16 / 8;
    const uint32_t data_size = static_cast<uint32_t>(std::min(data_bytes_, kMaxDataBytes));
    const uint32_t riff_size = static_cast<uint32_t>(kWavHeaderBytes - 8) + data_size;

    auto write_u16 = [](char* d, uint16_t v) {
        d[0] = static_cast<char>(v & 0xff);
        d[1] = static_cast<char>((v >> 8) & 0xff);
    };
    auto write_u32 = [](char* d, uint32_t v) {
        d[0] = static_cast<char>(v & 0xff);
        d[1] = static_cast<char>((v >> 8) & 0xff);
        d[2] = static_cast<char>((v >> 16) & 0xff);
        d[3] = static_cast<char>((v >> 24) & 0xff);
    };

    char h[44]{};
    h[0] = 'R'; h[1] = 'I'; h[2] = 'F'; h[3] = 'F';
    write_u32(h + 4, riff_size);
    h[8] = 'W'; h[9] = 'A'; h[10] = 'V'; h[11] = 'E';
    h[12] = 'f'; h[13] = 'm'; h[14] = 't'; h[15] = ' ';
    write_u32(h + 16, 16);
    write_u16(h + 20, 1);
    write_u16(h + 22, 2);
    write_u32(h + 24, sample_rate_);
    write_u32(h + 28, byte_rate);
    write_u16(h + 32, block_align);
    write_u16(h + 34, 16);
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    write_u32(h + 40, data_size);

    const auto pos = file_.tellp();
    file_.seekp(0);
    file_.write(h, 44);
    file_.seekp(pos);
}

void WavRecorderEngine::close_file() {
    if (!file_) {
        return;
    }
    patch_header();
    file_.flush();
    file_.close();
}

void WavRecorderEngine::write_samples(const float* interleaved, size_t sample_count) {
    if (!armed_ || !file_ || interleaved == nullptr || sample_count == 0) {
        return;
    }
    const uint64_t room_samples = (kMaxDataBytes - std::min(data_bytes_, kMaxDataBytes)) / 2;
    sample_count = static_cast<size_t>(std::min<uint64_t>(sample_count, room_samples));
    for (size_t i = 0; i < sample_count; ++i) {
        const float x = std::clamp(interleaved[i], -1.0f, 1.0f);
        const int16_t pcm = static_cast<int16_t>(std::lrint(x * 32767.0f));
        char bytes[2] = {static_cast<char>(pcm & 0xff),
                         static_cast<char>((pcm >> 8) & 0xff)};
        file_.write(bytes, 2);
        data_bytes_ += 2;
    }
}
