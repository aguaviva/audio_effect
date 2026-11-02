#include "wav_player.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "dsp/transport.hpp"
#include "imgui.h"
#include "ui/message_box.hpp"

bool WavPlayer::set_path(const std::string& path) {
    path_ = path;
    auto clip = WavGenerator::load(path, kSampleRate, error_);
    length_ = clip ? clip->interleaved_stereo.size() : 0;
    shown_clip_ = clip;
    peaks_ = clip ? std::make_unique<WaveformPeaks>(*clip) : nullptr;
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

    transport_controls();
    if (length_ > 0) {
        // Where the transport is in the clip.
        const size_t frames = length_ / 2;
        const int64_t frame = transport().position();
        const size_t in_clip = loop ? static_cast<size_t>(frame) % frames
                                    : std::min(static_cast<size_t>(frame), frames);
        float seconds = static_cast<float>(in_clip) / kSampleRate;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##position", &seconds, 0.0f, static_cast<float>(frames) / kSampleRate,
                               "%.1f s")) {
            transport().seek(static_cast<int64_t>(frame) - static_cast<int64_t>(in_clip) +
                             static_cast<int64_t>(seconds * kSampleRate));
        }
    }

    changed |= ImGui::Checkbox("Loop", &loop);
    return changed;
}

bool WavPlayer::render_timeline(Timeline& timeline, const char* title) {
    timeline.BeginTrack("clip");
    ImGui::TextUnformatted(title);
    const char* slash = std::strrchr(path_.c_str(), '/');
    const char* leaf = slash ? slash + 1 : path_.c_str();
    ImGui::TextDisabled("%s%s", leaf[0] ? leaf : "(no file)", loop ? " (loop)" : "");
    if (ImGui::IsItemHovered() && !path_.empty()) {
        ImGui::SetTooltip("%s", path_.c_str());
    }
    if (shown_clip_ && length_ > 0) {
        timeline.Clip(*shown_clip_, *peaks_, 0.0, kSampleRate);
        if (loop) {
            const double seconds = static_cast<double>(length_ / 2) / kSampleRate;
            for (double start = std::max(1.0, std::floor(timeline.VisibleStart() / seconds)) * seconds;
                 start < timeline.VisibleEnd(); start += seconds) {
                timeline.Clip(*shown_clip_, *peaks_, start, kSampleRate, false, true);
            }
        }
    }
    timeline.EndTrack();
    return true;
}

void WavPlayer::process(const float* const* /*inputs*/, int /*input_count*/,
                        size_t sample_count, float /*sample_rate*/) {
    std::unique_lock<std::mutex> lock(clip_mutex_, std::try_to_lock);
    if (lock.owns_lock() && clip_changed_) {
        std::swap(clip_, next_clip_);
        clip_changed_ = false;
    }
    float* output = output_buffer(0);
    if (!output) {
        return;
    }
    const size_t clip_samples = clip_ ? clip_->interleaved_stereo.size() : 0;
    size_t written = 0;
    if (transport().block_playing() && clip_samples > 0) {
        size_t cursor = 2 * static_cast<size_t>(transport().block_frame());
        if (loop) {
            cursor %= clip_samples;
        }
        while (written < sample_count && cursor < clip_samples) {
            const size_t n = std::min(sample_count - written, clip_samples - cursor);
            std::copy_n(clip_->interleaved_stereo.data() + cursor, n, output + written);
            written += n;
            cursor = loop ? 0 : cursor + n;
        }
    }
    std::fill(output + written, output + sample_count, 0.0f);
}
