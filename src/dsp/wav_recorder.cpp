#include "wav_recorder.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#include "imgui.h"

void WavRecorder::save(std::string& out, bool& first) const {
    module_json::write_bool(out, first, "armed", armed);
}

bool WavRecorder::load(const std::string& key, double, bool flag, const std::string&, int kind) {
    if (key == "armed" && kind == module_json::kKindBool) {
        armed = flag;
        return true;
    }
    return false;
}

bool WavRecorder::render() {
    return ImGui::Checkbox("Arm Record", &armed);
}

void WavRecorderEngine::set_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (armed_) {
        return;
    }
    if (!path.empty()) {
        path_ = path;
    }
}

void WavRecorderEngine::set_armed(bool armed, float sample_rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (armed == armed_) {
        return;
    }
    if (armed) {
        sample_rate_ = static_cast<uint32_t>(std::max(1.0f, sample_rate));
        if (open_file(sample_rate)) {
            armed_ = true;
            status_ = "Recording " + path_;
        } else {
            armed_ = false;
            status_ = "Failed to open " + path_;
        }
    } else {
        close_file();
        armed_ = false;
        status_ = "Saved " + path_;
    }
}

bool WavRecorderEngine::open_file(float /*sample_rate*/) {
    close_file();
    std::error_code ec;
    std::filesystem::path p(path_);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    file_.open(path_, std::ios::binary | std::ios::trunc);
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
    const uint32_t data_size = static_cast<uint32_t>(
        std::min<uint64_t>(data_bytes_, 0xffffffffu));
    const uint32_t riff_size = 36u + data_size;

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
    std::lock_guard<std::mutex> lock(mutex_);
    if (!armed_ || !file_ || interleaved == nullptr || sample_count == 0) {
        return;
    }
    for (size_t i = 0; i < sample_count; ++i) {
        const float x = std::clamp(interleaved[i], -1.0f, 1.0f);
        const int16_t pcm = static_cast<int16_t>(std::lrint(x * 32767.0f));
        char bytes[2] = {static_cast<char>(pcm & 0xff),
                         static_cast<char>((pcm >> 8) & 0xff)};
        file_.write(bytes, 2);
        data_bytes_ += 2;
    }
}

std::string WavRecorderEngine::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
