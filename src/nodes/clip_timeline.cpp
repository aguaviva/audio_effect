#include "clip_timeline.hpp"

#include "dsp/module_json.hpp"

#include <algorithm>
#include <iterator>

namespace {

const char* const kTrackPins[] = {"track 1",  "track 2",  "track 3",  "track 4",
                                  "track 5",  "track 6",  "track 7",  "track 8",
                                  "track 9",  "track 10", "track 11", "track 12",
                                  "track 13", "track 14", "track 15", "track 16"};
static_assert(std::size(kTrackPins) == MultiWavGenerator::kMaxChannels, "one name per pin");

} // namespace

ClipTimeline::ClipTimeline() : MultiWavGenerator(kTracks, true, "track", "Track", kTrackPins) {}

bool ClipTimeline::load(const std::string& json) {
    MultiWavGenerator::load(json);
    ClipArrangement& tracks = arrangement();
    for (int t = 0; t < kTracks; ++t) {
        ArrangedClip clip;
        int start = 0;
        if (!tracks.clips(t).empty() ||
            !module_json::read(json, ClipArrangement::channel_key("track", t, "path").c_str(),
                               clip.path) ||
            clip.path.empty()) {
            continue;
        }
        module_json::read(json, ClipArrangement::channel_key("track", t, "start").c_str(), start);
        clip.start = std::max(start, 0);
        const WavLibrary::Entry& file = library().get(clip.path);
        clip.wav = file.wav;
        clip.peaks = file.peaks;
        if (!file.wav) {
            tracks.set_error(t, clip.path + ": " + file.error);
        }
        tracks.add(t, std::move(clip));
    }
    sync();
    return true;
}
