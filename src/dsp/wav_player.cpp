#include "wav_player.hpp"

#include <algorithm>
#include <cstring>

#include "imgui.h"
#include "ui/message_box.hpp"

bool WavPlayer::set_path(const std::string& path) {
    path_ = path;
    auto clip = WavGenerator::load(path, kSampleRate, error_);
    length_ = clip ? clip->interleaved_stereo.size() : 0;
    {
        std::lock_guard<std::mutex> lock(clip_mutex_);
        std::swap(next_clip_, clip);
        clip_changed_ = true;
    }
    return error_.empty(); // clip now holds the replaced one, freed here outside the lock
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
        if (!set_path(picked)) {
            show_error("Could not load " + picked + ":\n" + error_);
        }
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
    if (!error_.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", error_.c_str());
        ImGui::PopTextWrapPos();
    }

    if (ImGui::Button(playing ? "Pause###play" : "Play###play")) {
        playing = !playing;
    }
    if (length_ > 0) {
        const float samples_per_second = 2.0f * kSampleRate;
        float seconds = std::min(cursor_.load(), length_) / samples_per_second;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##position", &seconds, 0.0f, length_ / samples_per_second,
                               "%.1f s")) {
            cursor_ = static_cast<size_t>(seconds * kSampleRate) * 2;
        }
    }

    changed |= ImGui::Checkbox("Loop", &loop);
    return changed;
}
void WavPlayer::process(const float* const* /*inputs*/, int /*input_count*/, float* output,
                        size_t sample_count, float /*sample_rate*/) {
    std::unique_lock<std::mutex> lock(clip_mutex_, std::try_to_lock);
    if (lock.owns_lock() && clip_changed_) {
        std::swap(clip_, next_clip_);
        clip_changed_ = false;
        cursor_ = 0;
    }
    if (!playing) {
        std::fill(output, output + sample_count, 0.0f);
        return;
    }
    const size_t clip_samples = clip_ ? clip_->interleaved_stereo.size() : 0;
    size_t start = cursor_;
    size_t cursor = start;
    size_t written = 0;
    while (written < sample_count && clip_samples > 0) {
        if (cursor >= clip_samples) {
            if (!loop) {
                break;
            }
            cursor = 0;
        }
        const size_t n = std::min(sample_count - written, clip_samples - cursor);
        std::copy_n(clip_->interleaved_stereo.data() + cursor, n, output + written);
        written += n;
        cursor += n;
    }
    std::fill(output + written, output + sample_count, 0.0f);
    cursor_.compare_exchange_strong(start, cursor); // a seek made during this block wins
}
