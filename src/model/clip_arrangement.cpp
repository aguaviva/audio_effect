#include "clip_arrangement.hpp"

#include "dsp/module_json.hpp"
#include "model/wav_library.hpp"

#include <algorithm>
#include <cstring>

namespace {

int64_t file_frames(const WavClip& wav) {
    return static_cast<int64_t>(wav.interleaved_stereo.size() / 2);
}

const char* leaf_of(const std::string& path) {
    const char* slash = std::strrchr(path.c_str(), '/');
    return slash ? slash + 1 : path.c_str();
}

} // namespace

int64_t ArrangedClip::frames() const {
    if (!wav) {
        return 0;
    }
    const int64_t available = std::max<int64_t>(0, file_frames(*wav) - offset);
    return length < 0 ? available : std::min(length, available);
}

ClipArrangement::ClipArrangement(int channels) : channels_(std::max(channels, 1)) {}

void ClipArrangement::set_channel_count(int channels) {
    channels = std::max(channels, 1);
    if (channels != channel_count()) {
        channels_.resize(static_cast<size_t>(channels));
        ++revision_;
    }
}

void ClipArrangement::set_error(int channel, std::string error) {
    channels_[channel].error = std::move(error);
}

int ClipArrangement::add(int channel, ArrangedClip clip) {
    clip.start = std::max<int64_t>(clip.start, 0);
    clip.offset = std::max<int64_t>(clip.offset, 0);
    channels_[channel].clips.push_back(std::move(clip));
    ++revision_;
    return static_cast<int>(channels_[channel].clips.size()) - 1;
}

void ClipArrangement::move(int channel, int index, int64_t start) {
    ArrangedClip& c = channels_[channel].clips[index];
    start = std::max<int64_t>(start, 0);
    if (start != c.start) {
        c.start = start;
        ++revision_;
    }
}

void ClipArrangement::trim_start(int channel, int index, int64_t start) {
    ArrangedClip& c = channels_[channel].clips[index];
    if (c.frames() == 0) {
        return;
    }
    const int64_t end = c.end();
    start = std::clamp(start, std::max<int64_t>(0, c.start - c.offset), end - 1);
    if (start != c.start) {
        c.offset += start - c.start;
        c.length = end - start;
        c.start = start;
        ++revision_;
    }
}

void ClipArrangement::trim_end(int channel, int index, int64_t end) {
    ArrangedClip& c = channels_[channel].clips[index];
    if (c.frames() == 0) {
        return;
    }
    end = std::clamp(end, c.start + 1, c.start + file_frames(*c.wav) - c.offset);
    if (end != c.end()) {
        c.length = end - c.start;
        ++revision_;
    }
}

void ClipArrangement::remove(int channel, int index) {
    std::vector<ArrangedClip>& clips = channels_[channel].clips;
    clips.erase(clips.begin() + index);
    ++revision_;
}

int ClipArrangement::duplicate(int channel, int index) {
    ArrangedClip copy = channels_[channel].clips[index];
    copy.start = copy.end();
    return add(channel, std::move(copy));
}

void ClipArrangement::clear() {
    for (Channel& channel : channels_) {
        channel = Channel{};
    }
    ++revision_;
}

std::string ClipArrangement::channel_key(const char* prefix, int channel, const char* field) {
    return prefix + std::to_string(channel + 1) + "_" + field;
}

void ClipArrangement::save_members(std::string& out, const char* prefix) const {
    for (int ch = 0; ch < channel_count(); ++ch) {
        if (channels_[ch].clips.empty()) {
            continue;
        }
        module_json::append_comma(out);
        out += '"' + channel_key(prefix, ch, "clips") + "\":[";
        for (const ArrangedClip& c : channels_[ch].clips) {
            module_json::append_comma(out);
            std::string clip = "{";
            module_json::write_string(clip, "path", c.path);
            module_json::write_int(clip, "start", static_cast<int>(c.start));
            module_json::write_int(clip, "offset", static_cast<int>(c.offset));
            if (c.length >= 0) {
                module_json::write_int(clip, "length", static_cast<int>(c.length));
            }
            out += clip + '}';
        }
        out += ']';
    }
}

void ClipArrangement::load_members(const std::string& json, const char* prefix,
                                   WavLibrary& library) {
    clear();
    module_json::for_each_member(json, [&](const std::string& key, const std::string& text,
                                           size_t& i) {
        for (int ch = 0; ch < channel_count(); ++ch) {
            if (key != channel_key(prefix, ch, "clips")) {
                continue;
            }
            return module_json::for_each_element(text, i, [&](const std::string& element) {
                ArrangedClip clip;
                int start = 0;
                int offset = 0;
                int length = -1;
                module_json::read(element, "path", clip.path);
                module_json::read(element, "start", start);
                module_json::read(element, "offset", offset);
                module_json::read(element, "length", length);
                if (clip.path.empty()) {
                    return true;
                }
                clip.start = start;
                clip.offset = offset;
                clip.length = length < 0 ? -1 : std::max(length, 1);
                const WavLibrary::Entry& file = library.get(clip.path);
                clip.wav = file.wav;
                clip.peaks = file.peaks;
                if (!file.wav) {
                    set_error(ch, leaf_of(clip.path) + std::string(": ") + file.error);
                }
                add(ch, std::move(clip));
                return true;
            });
        }
        return module_json::skip_value(text, i);
    });
}
