#include "multi_wav_generator.hpp"

#include "dsp/module_json.hpp"
#include "dsp/transport.hpp"
#include "imgui.h"
#include "ui/timeline_group.hpp"
#include "ui/transport_controls.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace {

const char* const kChannelPins[] = {"ch 1",  "ch 2",  "ch 3",  "ch 4",  "ch 5",  "ch 6",
                                    "ch 7",  "ch 8",  "ch 9",  "ch 10", "ch 11", "ch 12",
                                    "ch 13", "ch 14", "ch 15", "ch 16"};
static_assert(std::size(kChannelPins) == MultiWavGenerator::kMaxChannels, "one name per pin");

} // namespace

MultiWavGenerator::MultiWavGenerator()
    : MultiWavGenerator(kDefaultChannels, false, "channel", "Channel", kChannelPins) {}

MultiWavGenerator::MultiWavGenerator(int channels, bool fixed, const char* prefix,
                                     const char* lane_name, const char* const* pin_names)
    : fixed_(fixed),
      prefix_(prefix),
      lane_name_(lane_name),
      pin_names_(pin_names),
      arrangement_(std::clamp(channels, 1, kMaxChannels)),
      outputs_(arrangement_.channel_count()) {
    sync();
}

void MultiWavGenerator::get_inputs(int* count, const char* const** str) const {
    *count = 0;
    *str = nullptr;
}

void MultiWavGenerator::get_outputs(int* count, const char* const** str) const {
    *count = outputs_.load(std::memory_order_relaxed);
    *str = pin_names_;
}

void MultiWavGenerator::set_channel_count(int channels) {
    if (fixed_) {
        return;
    }
    arrangement_.set_channel_count(std::clamp(channels, 1, kMaxChannels));
    outputs_.store(arrangement_.channel_count(), std::memory_order_relaxed);
    sync();
}

void MultiWavGenerator::sync() {
    player_.collect();
    if (arrangement_.revision() == synced_revision_) {
        return;
    }
    synced_revision_ = arrangement_.revision();
    ClipSchedule schedule;
    schedule.channels.resize(static_cast<size_t>(arrangement_.channel_count()));
    for (int ch = 0; ch < arrangement_.channel_count(); ++ch) {
        for (const ArrangedClip& c : arrangement_.clips(ch)) {
            if (c.frames() > 0) {
                schedule.channels[ch].push_back({c.wav, c.start, c.offset, c.frames()});
            }
        }
    }
    player_.publish(std::move(schedule));
}

std::string MultiWavGenerator::save() const {
    std::string out = "{";
    if (!fixed_) {
        module_json::write_int(out, "channels", arrangement_.channel_count());
    }
    arrangement_.save_members(out, prefix_);
    out += '}';
    return out;
}

bool MultiWavGenerator::load(const std::string& json) {
    if (!fixed_) {
        int channels = arrangement_.channel_count();
        module_json::read(json, "channels", channels);
        set_channel_count(channels);
    }
    arrangement_.load_members(json, prefix_, library_);
    sync();
    return true;
}

bool MultiWavGenerator::render() {
    bool changed = false;
    transport_controls(true);
    if (!fixed_) {
        int channels = arrangement_.channel_count();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Channels");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::InputInt("##channels", &channels)) {
            set_channel_count(channels);
            changed = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("One output pin and timeline lane per channel. Removing channels "
                              "drops their clips and links.");
        }
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextDisabled("Right-click a lane in View > Timeline to load a WAV");
    ImGui::PopTextWrapPos();
    return changed;
}

void MultiWavGenerator::render_window() {
    editor_.poll(arrangement_, library_);
    sync();
}

bool MultiWavGenerator::render_timeline(Timeline& timeline, const char* title) {
    char note[32];
    std::snprintf(note, sizeof(note), "%d %s", arrangement_.channel_count(),
                  arrangement_.channel_count() == 1 ? "lane" : "lanes");
    timeline_title_row(timeline, title,
                       "Drag a waveform to move it or its edges to trim it; it snaps to the "
                       "grid and to other clips.\nRight-click a lane to load a WAV there or "
                       "paste.\nCtrl+C / X / V / D copy, cut, paste at the mouse and duplicate; "
                       "Delete removes.",
                       note);
    editor_.draw(timeline, arrangement_, lane_name_);
    sync();
    return true;
}

void MultiWavGenerator::process(const float* const* /*inputs*/, int /*input_count*/,
                                size_t sample_count, float /*sample_rate*/) {
    // Every pin with a buffer, past the channel count too: until the UI drops a removed
    // channel's links, they play silence.
    std::array<float*, kMaxOutputs> outputs;
    for (int k = 0; k < kMaxOutputs; ++k) {
        outputs[k] = output_buffer(k);
    }
    const size_t frames = sample_count / 2;
    if (!transport().block_playing()) {
        for (float* out : outputs) {
            if (out) {
                std::fill(out, out + sample_count, 0.0f);
            }
        }
        return;
    }
    player_.render(outputs.data(), kMaxOutputs, transport().block_frame(), frames);
}
