#include "legacy_patch.hpp"

#include "dsp/module_json.hpp"

#include <algorithm>
#include <array>

namespace legacy_patch {
namespace {

constexpr int kClipTimelineTracks = 6;

std::string track_key(int track, const char* field) {
    return "track" + std::to_string(track + 1) + "_" + field;
}

bool migrate_clip_timeline(std::string& params) {
    struct Track {
        std::string clips; // the raw "trackN_clips" array
        std::string path;
        int start = 0;
    };
    std::array<Track, kClipTimelineTracks> tracks;
    bool bypassed = false;
    const bool ok = module_json::for_each_member(
        params, [&](const std::string& key, const std::string& text, size_t& i) {
            if (key == "bypassed") return module_json::parse_value(text, i, bypassed);
            for (int t = 0; t < kClipTimelineTracks; ++t) {
                if (key == track_key(t, "clips")) return module_json::take_value(text, i, tracks[t].clips);
                if (key == track_key(t, "path")) return module_json::parse_value(text, i, tracks[t].path);
                if (key == track_key(t, "start")) return module_json::parse_value(text, i, tracks[t].start);
            }
            return module_json::skip_value(text, i);
        });
    if (!ok) {
        return false;
    }

    std::string out = "{";
    module_json::write_bool(out, "bypassed", bypassed);
    module_json::write_int(out, "channels", kClipTimelineTracks);
    for (int t = 0; t < kClipTimelineTracks; ++t) {
        const Track& track = tracks[t];
        std::string clips = track.clips;
        if (clips.empty() && !track.path.empty()) {
            std::string clip = "{";
            module_json::write_string(clip, "path", track.path);
            module_json::write_int(clip, "start", std::max(track.start, 0));
            clips = "[" + clip + "}]";
        }
        if (!clips.empty()) {
            module_json::append_comma(out);
            out += "\"channel" + std::to_string(t + 1) + "_clips\":" + clips;
        }
    }
    params = out + '}';
    return true;
}

} // namespace

bool migrate_node(std::string& type, std::string& params) {
    if (type == "timeline") {
        if (!migrate_clip_timeline(params)) {
            return false;
        }
        type = "multi_wav";
    }
    return true;
}

} // namespace legacy_patch
