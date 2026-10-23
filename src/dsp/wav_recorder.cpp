#include "wav_recorder.hpp"

#include <algorithm>
#include <filesystem>

#include "imgui.h"
#include "ui/message_box.hpp"

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
    if (changed && armed) {
        engine_.clear_failure();
    } else if (armed && engine_.failed()) {
        armed = false;
        show_error("Could not record: " + path + " cannot be written.");
    }
    ImGui::TextWrapped("%s", path.c_str());
    return changed;
}

void WavRecorder::process(const float* const* inputs, int input_count, float* const* outputs,
                          size_t sample_count, float sample_rate) {
    const float* in = pass_through(inputs, input_count, outputs, sample_count);
    engine_.set_armed(armed, path, sample_rate);
    engine_.write_samples(in, sample_count);
}

WavRecorderEngine::~WavRecorderEngine() {
    close_file();
}

void WavRecorderEngine::set_armed(bool armed, const std::string& path, float sample_rate) {
    if (armed == requested_) {
        return;
    }
    requested_ = armed;
    if (armed) {
        armed_ = open_file(path, sample_rate);
        failed_.store(!armed_, std::memory_order_relaxed);
    } else {
        close_file();
    }
}

bool WavRecorderEngine::open_file(const std::string& path, float sample_rate) {
    close_file();
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
    }
    ma_encoder_config config = ma_encoder_config_init(
        ma_encoding_format_wav, ma_format_f32, 2, static_cast<ma_uint32>(std::max(1.0f, sample_rate)));
    return ma_encoder_init_file(path.c_str(), &config, &encoder_) == MA_SUCCESS;
}

void WavRecorderEngine::close_file() {
    if (armed_) {
        ma_encoder_uninit(&encoder_);
        armed_ = false;
    }
}

void WavRecorderEngine::write_samples(const float* interleaved, size_t sample_count) {
    if (!armed_ || interleaved == nullptr || sample_count == 0) {
        return;
    }
    ma_encoder_write_pcm_frames(&encoder_, interleaved, sample_count / 2, nullptr);
}
