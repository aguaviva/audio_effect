#include "wav_player.hpp"

#include <algorithm>
#include <cstring>

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
    bool changed = false;
    const std::string picked = picker_.poll();
    if (!picked.empty()) {
        set_path(picked);
        changed = true;
    }

    const char* slash = std::strrchr(path_.c_str(), '/');
    const char* leaf = slash ? slash + 1 : path_.c_str();
    const ImGuiStyle& style = ImGui::GetStyle();
    const float button_width = ImGui::CalcTextSize("...").x + style.FramePadding.x * 2.0f;
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                           button_width - style.ItemSpacing.x);
    ImGui::TextUnformatted(leaf[0] ? leaf : "(no file)");
    ImGui::PopTextWrapPos();
    if (ImGui::IsItemHovered() && !path_.empty()) {
        ImGui::SetTooltip("%s", path_.c_str());
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("...")) {
        picker_.open(path_);
    }

    changed |= ImGui::Checkbox("Loop", &loop);
    return changed;
}
void WavPlayer::process(const float* const* /*inputs*/, int /*input_count*/, float* output,
                        size_t sample_count, float /*sample_rate*/) {
    std::unique_lock<std::mutex> lock(path_mutex_, std::try_to_lock);
    if (lock.owns_lock() && path_ != loaded_path_) {
        loaded_path_ = path_;
        lock.unlock();
        clip_ = host_ ? host_->load_wav(loaded_path_) : nullptr;
        cursor_ = 0;
    }
    const size_t clip_samples = clip_ ? clip_->interleaved_stereo.size() : 0;
    size_t written = 0;
    while (written < sample_count && clip_samples > 0) {
        if (cursor_ >= clip_samples) {
            if (!loop) {
                break;
            }
            cursor_ = 0;
        }
        const size_t n = std::min(sample_count - written, clip_samples - cursor_);
        std::copy_n(clip_->interleaved_stereo.data() + cursor_, n, output + written);
        written += n;
        cursor_ += n;
    }
    std::fill(output + written, output + sample_count, 0.0f);
}
