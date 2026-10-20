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
void WavPlayer::process(ProcessContext& ctx) {
    if (ctx.buffer == nullptr || ctx.num_samples == 0) {
        return;
    }
    if (scratch_.size() != ctx.num_samples) {
        scratch_.assign(ctx.num_samples, 0);
    }
    std::unique_lock<std::mutex> lock(path_mutex_, std::try_to_lock);
    if (lock.owns_lock() && path_ != loaded_path_) {
        loaded_path_ = path_;
        lock.unlock();
        clip_.reset();
        if (ctx.host) {
            clip_ = ctx.host->load_wav(loaded_path_);
            if (!clip_) {
                clip_ = ctx.host->fallback_wav();
            }
        }
        wav_.reset();
    }
    if (clip_ && clip_->frame_count() > 0) {
        wav_.render(clip_, loop, scratch_.data(), ctx.num_samples / 2);
    } else {
        std::fill(scratch_.begin(), scratch_.end(), 0);
    }
    for (size_t i = 0; i < ctx.num_samples; ++i) {
        ctx.buffer[i] = static_cast<float>(scratch_[i]) / 2147483648.0f;
    }
}
