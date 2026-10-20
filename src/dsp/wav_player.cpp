#include "wav_player.hpp"

#include <algorithm>

#include "imgui.h"

void WavPlayer::set_path(const std::string& path) {
    std::lock_guard<std::mutex> lock(path_mutex_);
    path_ = path;
}

std::string WavPlayer::save() const {
    std::string out = "{";
    module_json::write_bool(out, "loop", loop);
    module_json::write_string(out, "path", path_);
    out += '}';
    return out;
}

bool WavPlayer::load(const std::string& json) {
    module_json::read(json, "loop", loop);
    std::string path = path_;
    module_json::read(json, "path", path);
    set_path(path);
    return true;
}

bool WavPlayer::render() {
    return ImGui::Checkbox("Loop", &loop);
}
void WavPlayer::process(const float* const* /*inputs*/, int /*input_count*/, float* output,
                        size_t sample_count, float /*sample_rate*/) {
    if (scratch_.size() != sample_count) {
        scratch_.assign(sample_count, 0);
    }
    std::unique_lock<std::mutex> lock(path_mutex_, std::try_to_lock);
    if (lock.owns_lock() && path_ != loaded_path_) {
        loaded_path_ = path_;
        lock.unlock();
        clip_ = host_ ? host_->load_wav(loaded_path_) : nullptr;
        wav_.reset();
    }
    if (clip_ && clip_->frame_count() > 0) {
        wav_.render(clip_, loop, scratch_.data(), sample_count / 2);
    } else {
        std::fill(scratch_.begin(), scratch_.end(), 0);
    }
    for (size_t i = 0; i < sample_count; ++i) {
        output[i] = static_cast<float>(scratch_[i]) / 2147483648.0f;
    }
}
