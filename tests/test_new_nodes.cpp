#include "test_harness.hpp"

#include "dsp/agc.hpp"
#include "dsp/denormals.hpp"
#include "dsp/dry_wet_mix.hpp"
#include "dsp/eq_three_band.hpp"
#include "dsp/gen_midi.hpp"
#include "dsp/guitar_pedals.hpp"
#include "dsp/mixer.hpp"
#include "dsp/noise_gate.hpp"
#include "dsp/preamp.hpp"
#include "dsp/simple_reverb.hpp"
#include "dsp/transport.hpp"
#include "dsp/wav_player.hpp"
#include "dsp/wav_recorder.hpp"
#include "dsp/white_noise.hpp"
#include "audio/sink_clock.hpp"
#include "model/clip_clipboard.hpp"
#include "model/clip_snap.hpp"
#include "model/tempo_grid.hpp"
#include "model/waveform_peaks.hpp"
#include "nodes/multi_wav_generator.hpp"
#include "graph/audio_graph.hpp"
#include "graph/graph_processor.hpp"
#include "graph/node_registry.hpp"
#include "paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

// Runs a single-input module on block, feeding it a copy of block as its input link.
static void run_effect(Module& module, std::vector<float>& block) {
    const std::vector<float> in = block;
    const float* inputs[] = {in.data()};
    module.set_output_buffer(0, block.data());
    module.process(inputs, 1, block.size(), 48000.0f);
}

TEST(registry_resolves_module_names_and_pins) {
    for (const char* name : {"noise_gate", "eq_three_band", "reverb", "dry_wet_mix", "wav_recorder",
                             "white_noise", "agc", "pitch_detector"}) {
        auto module = make_module(name);
        CHECK(module != nullptr);
        CHECK(module->name() == name);
    }
    CHECK(make_module("not_a_module") == nullptr);
    CHECK(make_module("timeline") == nullptr); // Clip Timeline patches load as Multi WAV
    CHECK_EQ(make_module("white_noise")->input_count(), 0);
    CHECK_EQ(make_module("dry_wet_mix")->input_count(), 2);
    CHECK_EQ(make_module("output")->output_count(), 0);
    for (const char* name : {"vu_meter", "oscilloscope", "spectrum", "pitch_detector"}) {
        CHECK_EQ(make_module(name)->output_count(), 0);
        CHECK(!make_module(name)->bypassable());
    }
    CHECK(!make_module("mixer")->bypassable());
    CHECK(!make_module("dry_wet_mix")->bypassable());
    CHECK(make_module("preamp")->bypassable());
    for (const char* name : {"white_noise", "wav_player", "chord_generator", "audio_input",
                             "multi_wav"}) {
        CHECK(!make_module(name)->bypassable());
    }
    CHECK_STREQ(node_label(*make_module("pitch_detector")), "Pitch Detector");
}

TEST(white_noise_source_produces_energy) {
    AudioGraph g;
    g.clear();
    const int noise = g.add_node("white_noise", 0, 0);
    const int out = g.add_node("output", 0, 0);
    g.find_node(noise)->as<WhiteNoise>()->level = 0.5f;
    CHECK(g.add_link(noise, 0, out, 0));

    constexpr size_t block = 256;
    std::vector<float> live(block, 0.0f);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    auto compiled = g.compile();
    CHECK(compiled);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));

    float energy = 0.0f;
    for (float s : output) {
        energy += s * s;
    }
    CHECK(energy > 1.0e-3f);
}

TEST(wav_player_plays_its_file_and_silence_when_missing) {
    AudioGraph g;
    const int wav = g.add_node("wav_player", 0, 0);
    const int out = g.add_node("output", 0, 0);
    CHECK(g.add_link(wav, 0, out, 0));
    auto* player = g.find_node(wav)->as<WavPlayer>();
    auto compiled = g.compile();
    GraphProcessor proc(48000.0f);

    auto energy_of_one_second = [&] {
        constexpr size_t block = 4800;
        std::vector<float> output(block, 0.0f);
        float energy = 0.0f;
        for (int n = 0; n < 20; ++n) {
            proc.process(*compiled, nullptr, output.data(), block);
            for (float s : output) {
                energy += s * s;
            }
        }
        return energy;
    };
    CHECK(energy_of_one_second() > 1.0e-3f);

    CHECK(player->error().empty());
    CHECK(!player->set_path("assets/audio/does_not_exist.wav"));
    CHECK(!player->error().empty());
    CHECK_NEAR(energy_of_one_second(), 0.0f, 1e-9f);
}

// Back at the start, playing, as a fresh transport is.
static void reset_transport() {
    transport().seek(0);
    transport().play();
    transport().begin_block();
}

// One block of module at the shared transport's position, as GraphProcessor runs it.
static void run_block(Module& module, size_t samples) {
    transport().begin_block();
    module.process(nullptr, 0, samples, 48000.0f);
    transport().end_block(static_cast<int64_t>(samples / 2));
}

TEST(paused_transport_is_silent_and_resumes_where_it_stopped) {
    reset_transport();
    WavPlayer player;
    CHECK(player.load("{}"));
    std::string error;
    auto clip = WavGenerator::load(kDefaultWavPath, kSampleRate, error);
    CHECK(clip != nullptr);
    std::vector<float> out(48000, 0.0f);
    player.set_output_buffer(0, out.data());
    run_block(player, out.size());
    CHECK(std::equal(out.begin(), out.end(), clip->interleaved_stereo.begin()));

    transport().pause();
    run_block(player, out.size());
    for (float s : out) {
        CHECK_EQ(s, 0.0f);
    }
    CHECK_EQ(transport().block_frame(), static_cast<int64_t>(24000));

    transport().play();
    run_block(player, out.size());
    CHECK(std::equal(out.begin(), out.end(), clip->interleaved_stereo.begin() + 48000));
    float energy = 0.0f;
    for (float s : out) {
        energy += s * s;
    }
    CHECK(energy > 1.0e-3f);

    // Past the end it loops: a seek lands that far into the next pass.
    const int64_t frames = static_cast<int64_t>(clip->interleaved_stereo.size() / 2);
    transport().seek(frames + 100);
    run_block(player, out.size());
    CHECK(std::equal(out.begin(), out.end(), clip->interleaved_stereo.begin() + 200));
    reset_transport();
}

TEST(cursor_is_where_the_sink_is_not_where_rendering_is) {
    reset_transport();
    transport().set_sink_frame(0);
    SinkClock sink;
    const auto render = [&] {
        transport().begin_block();
        const Transport::Stamp stamp = transport().block_stamp();
        transport().end_block(256);
        sink.queued(stamp, 256);
    };

    // Three blocks rendered ahead of the device: the cursor has not moved yet.
    render();
    render();
    render();
    CHECK_EQ(transport().block_frame(), static_cast<int64_t>(768));
    CHECK_EQ(transport().position(), static_cast<int64_t>(0));
    // The device takes part of one block, then across the next.
    sink.consumed(100);
    CHECK_EQ(transport().position(), static_cast<int64_t>(100));
    sink.consumed(300);
    CHECK_EQ(transport().position(), static_cast<int64_t>(400));

    // Paused blocks hold the cursor at the pause, once the device reaches them.
    transport().pause();
    render();
    sink.consumed(368);
    CHECK_EQ(transport().position(), static_cast<int64_t>(768));
    sink.consumed(200);
    CHECK_EQ(transport().position(), static_cast<int64_t>(768));

    // A seek reaches the cursor when the device gets to the first block rendered after it.
    transport().play();
    transport().seek(48000);
    render();
    CHECK_EQ(transport().position(), static_cast<int64_t>(768));
    sink.consumed(56 + 10);
    CHECK_EQ(transport().position(), static_cast<int64_t>(48010));
    // An underrun takes nothing: the cursor stays.
    sink.consumed(1000);
    CHECK_EQ(transport().position(), static_cast<int64_t>(48256));
    sink.consumed(256);
    CHECK_EQ(transport().position(), static_cast<int64_t>(48256));
    reset_transport();
    transport().set_sink_frame(0);
}

TEST(waveform_peaks_match_a_direct_scan) {
    WavClip clip;
    for (int i = 0; i < 10000; ++i) {
        const float v = std::sin(i * 0.013f) * (i % 7 == 0 ? 1.0f : 0.5f);
        clip.interleaved_stereo.push_back(v);
        clip.interleaved_stereo.push_back(-0.25f * v);
    }
    const WaveformPeaks peaks(clip);
    CHECK_EQ(peaks.levels.back().size(), static_cast<size_t>(1));
    for (size_t begin : {0u, 10u, 640u, 3000u}) {
        for (size_t len : {5u, 64u, 300u, 4096u}) {
            const size_t end = std::min<size_t>(begin + len, 5000);
            // Whole buckets are read, so the envelope covers at least the exact range.
            const auto [lo, hi] = peaks.range(clip, begin, end);
            const auto first = clip.interleaved_stereo.begin();
            auto [elo, ehi] = std::minmax_element(first + 2 * begin, first + 2 * end);
            CHECK(lo <= *elo && hi >= *ehi);
        }
    }
}

TEST(timeline_plays_a_clip_from_where_it_is_placed) {
    MultiWavGenerator timeline;
    CHECK(timeline.load(std::string("{\"channels\":6,\"channel3_clips\":[{\"path\":\"") +
                        kDefaultWavPath + "\",\"start\":1000}]}"));
    std::string error;
    auto clip = WavGenerator::load(kDefaultWavPath, kSampleRate, error);
    CHECK(clip != nullptr);
    reset_transport();
    // Only pins 2 and 3 are linked: track 2 is empty, track 3 plays on its own pin.
    constexpr size_t kBlock = 4096;
    std::vector<float> track2(kBlock, 1.0f);
    std::vector<float> track3(kBlock, 1.0f);
    timeline.set_output_buffer(1, track2.data());
    timeline.set_output_buffer(2, track3.data());
    run_block(timeline, kBlock);
    for (size_t i = 0; i < kBlock; ++i) {
        CHECK_EQ(track2[i], 0.0f);
    }
    for (size_t i = 0; i < 2000; ++i) {
        CHECK_EQ(track3[i], 0.0f);
    }
    for (size_t i = 2000; i < kBlock; ++i) {
        CHECK_EQ(track3[i], clip->interleaved_stereo[i - 2000]);
    }

    transport().seek(1500);
    run_block(timeline, kBlock);
    for (size_t i = 0; i < kBlock; ++i) {
        CHECK_EQ(track3[i], clip->interleaved_stereo[i + 1000]);
    }
    reset_transport();
}

// The whole of path's file at start, as the lane editor adds a picked file; its index or -1.
static int add_file(MultiWavGenerator& node, int channel, const std::string& path, int64_t start) {
    const WavLibrary::Entry& file = node.library().get(path);
    if (!file.wav) {
        return -1;
    }
    ArrangedClip clip;
    clip.path = path;
    clip.start = start;
    clip.wav = file.wav;
    clip.peaks = file.peaks;
    const int index = node.arrangement().add(channel, std::move(clip));
    node.sync();
    return index;
}

TEST(timeline_track_holds_trimmed_clips_from_several_files) {
    std::string error;
    auto a = WavGenerator::load(kDefaultWavPath, kSampleRate, error);
    auto b = WavGenerator::load("assets/audio/rhythm.wav", kSampleRate, error);
    CHECK(a != nullptr && b != nullptr);
    MultiWavGenerator timeline;
    CHECK(timeline.load("{\"channels\":6}"));
    ClipArrangement& tracks = timeline.arrangement();
    CHECK_EQ(add_file(timeline, 0, kDefaultWavPath, 0), 0);
    CHECK_EQ(add_file(timeline, 0, "assets/audio/rhythm.wav", 3000), 1);
    CHECK_EQ(add_file(timeline, 0, "assets/audio/does_not_exist.wav", 0), -1);
    CHECK(!timeline.library().get("assets/audio/does_not_exist.wav").error.empty());
    CHECK_EQ(tracks.clips(0).size(), static_cast<size_t>(2));

    // Trimming moves the bounds into the file; the clip keeps its place in time.
    tracks.trim_start(0, 0, 500);
    tracks.trim_end(0, 0, 2000);
    const ArrangedClip& first = tracks.clips(0)[0];
    CHECK_EQ(first.start, static_cast<int64_t>(500));
    CHECK_EQ(first.offset, static_cast<int64_t>(500));
    CHECK_EQ(first.frames(), static_cast<int64_t>(1500));
    // Neither edge goes past the file, and a clip keeps at least a frame.
    tracks.trim_start(0, 0, 0);
    CHECK_EQ(tracks.clips(0)[0].offset, static_cast<int64_t>(0));
    tracks.trim_start(0, 0, 5000);
    CHECK_EQ(tracks.clips(0)[0].frames(), static_cast<int64_t>(1));
    tracks.trim_start(0, 0, 500);
    tracks.trim_end(0, 1, 1 << 30);
    CHECK_EQ(tracks.clips(0)[1].frames(), static_cast<int64_t>(b->interleaved_stereo.size() / 2));
    tracks.trim_end(0, 1, 3100);

    // A copy pasted on another track, of another node even, keeps the trim.
    clip_clipboard::copy(tracks.clips(0)[0]);
    CHECK(clip_clipboard::get() != nullptr);
    ArrangedClip pasted = *clip_clipboard::get();
    pasted.start = 10000;
    CHECK_EQ(tracks.add(4, pasted), 0);
    CHECK_EQ(tracks.clips(4)[0].offset, static_cast<int64_t>(500));
    CHECK_EQ(tracks.clips(4)[0].frames(), static_cast<int64_t>(1500));
    const int duplicate = tracks.duplicate(4, 0);
    CHECK_EQ(tracks.clips(4)[duplicate].start, static_cast<int64_t>(11500));
    tracks.remove(4, duplicate);

    // Played only after sync() hands the edits over.
    reset_transport();
    constexpr size_t kBlock = 2 * 4096;
    std::vector<float> track1(kBlock, 1.0f);
    std::vector<float> track5(kBlock, 1.0f);
    timeline.set_output_buffer(0, track1.data());
    timeline.set_output_buffer(4, track5.data());
    timeline.sync();
    run_block(timeline, kBlock);
    for (size_t f = 0; f < kBlock / 2; ++f) {
        float want = 0.0f;
        if (f >= 500 && f < 2000) {
            want = a->interleaved_stereo[2 * f];
        } else if (f >= 3000 && f < 3100) {
            want = b->interleaved_stereo[2 * (f - 3000)];
        }
        CHECK_EQ(track1[2 * f], want);
        CHECK_EQ(track5[2 * f], 0.0f);
    }

    // Saved and loaded, trims and all.
    const std::string saved = timeline.save();
    CHECK(saved.find("\"channel1_clips\"") != std::string::npos);
    MultiWavGenerator loaded;
    CHECK(loaded.load(saved));
    const ClipArrangement& again = loaded.arrangement();
    CHECK_EQ(again.clips(0).size(), static_cast<size_t>(2));
    CHECK(again.clips(0)[1].path == "assets/audio/rhythm.wav");
    CHECK_EQ(again.clips(0)[1].start, static_cast<int64_t>(3000));
    CHECK_EQ(again.clips(0)[1].frames(), static_cast<int64_t>(100));
    CHECK_EQ(again.clips(4)[0].offset, static_cast<int64_t>(500));
    CHECK_EQ(again.clips(4)[0].frames(), static_cast<int64_t>(1500));

    // A missing file keeps its clip, so saving again does not drop it.
    MultiWavGenerator missing;
    CHECK(missing.load(R"({"channel2_clips":[{"path":"assets/audio/gone.wav","start":7,"offset":3,"length":9}]})"));
    CHECK_EQ(missing.arrangement().clips(1).size(), static_cast<size_t>(1));
    CHECK_EQ(missing.arrangement().clips(1)[0].frames(), static_cast<int64_t>(0));
    CHECK(!missing.arrangement().error(1).empty());
    CHECK(missing.save().find("\"length\":9") != std::string::npos);

    tracks.remove(0, 0);
    CHECK_EQ(tracks.clips(0).size(), static_cast<size_t>(1));
    reset_transport();
}

TEST(multi_wav_has_a_pin_and_lane_per_channel) {
    MultiWavGenerator node;
    CHECK(node.load("{}"));
    CHECK_EQ(node.channel_count(), MultiWavGenerator::kDefaultChannels);
    CHECK_EQ(node.output_count(), 4);
    CHECK_EQ(node.input_count(), 0);
    CHECK(!node.needs_control_lock());
    for (int ch = 0; ch < 4; ++ch) {
        CHECK(node.arrangement().clips(ch).empty());
    }

    std::string error;
    auto a = WavGenerator::load(kDefaultWavPath, kSampleRate, error);
    CHECK(a != nullptr);
    CHECK_EQ(add_file(node, 3, kDefaultWavPath, 100), 0);
    CHECK_EQ(add_file(node, 3, kDefaultWavPath, 2000), 1);
    node.arrangement().trim_end(3, 0, 200);
    node.sync();

    reset_transport();
    constexpr size_t kBlock = 2 * 4096;
    std::vector<float> ch4(kBlock, 1.0f);
    node.set_output_buffer(3, ch4.data());
    run_block(node, kBlock);
    for (size_t f = 0; f < kBlock / 2; ++f) {
        float want = 0.0f;
        if (f >= 100 && f < 200) {
            want = a->interleaved_stereo[2 * (f - 100)];
        } else if (f >= 2000) {
            want = a->interleaved_stereo[2 * (f - 2000)];
        }
        CHECK_EQ(ch4[2 * f], want);
    }

    // Saved with its channel count; more channels, more pins.
    node.set_channel_count(6);
    CHECK_EQ(node.output_count(), 6);
    const std::string saved = node.save();
    CHECK(saved.find("\"channels\":6") != std::string::npos);
    MultiWavGenerator loaded;
    CHECK(loaded.load(saved));
    CHECK_EQ(loaded.output_count(), 6);
    CHECK_EQ(loaded.arrangement().clips(3).size(), static_cast<size_t>(2));
    CHECK_EQ(loaded.arrangement().clips(3)[0].frames(), static_cast<int64_t>(100));

    // Fewer channels drop those clips; a removed channel's buffer, still linked until the
    // graph drops the link, plays silence.
    node.set_channel_count(2);
    CHECK_EQ(node.output_count(), 2);
    node.set_channel_count(4);
    CHECK(node.arrangement().clips(3).empty());
    node.set_channel_count(2);
    std::fill(ch4.begin(), ch4.end(), 1.0f);
    run_block(node, kBlock);
    for (float v : ch4) {
        CHECK_EQ(v, 0.0f);
    }
    node.set_channel_count(0);
    CHECK_EQ(node.output_count(), 1);
    node.set_channel_count(100);
    CHECK_EQ(node.output_count(), MultiWavGenerator::kMaxChannels);
    reset_transport();
}

TEST(removing_channels_drops_their_links) {
    AudioGraph g;
    const int wavs = g.add_node("multi_wav", 0, 0);
    const int mixer = g.add_node("mixer", 0, 0);
    CHECK_EQ(g.find_node(wavs)->module->output_count(), 4);
    for (int ch = 0; ch < 4; ++ch) {
        CHECK(g.add_link(wavs, ch, mixer, ch));
    }
    CHECK(!g.add_link(wavs, 4, mixer, 4));
    CHECK(!g.drop_stale_links());
    g.find_node(wavs)->as<MultiWavGenerator>()->set_channel_count(2);
    CHECK(g.drop_stale_links());
    CHECK_EQ(g.links().size(), static_cast<size_t>(2));
    for (const GraphLink& link : g.links()) {
        CHECK(link.from_slot < 2);
    }
}

TEST(clip_edits_snap_to_the_grid_and_to_other_clips) {
    TempoGrid grid; // 4/4 at 120 bpm, four lines a beat: one every 6000 frames
    ClipArrangement arrangement(2);
    WavLibrary library(kSampleRate);
    ArrangedClip clip;
    clip.path = kDefaultWavPath;
    clip.wav = library.get(kDefaultWavPath).wav;
    CHECK(clip.wav != nullptr);
    clip.start = 30500;
    clip.length = 10000;
    arrangement.add(1, clip);

    const ClipSnap snap{arrangement, grid, kSampleRate, 800};
    CHECK_EQ(snap.edge(6100), static_cast<int64_t>(6000));
    CHECK_EQ(snap.edge(8900), static_cast<int64_t>(6000));
    CHECK_EQ(snap.edge(9100), static_cast<int64_t>(12000));
    // The other clip's edge wins when it is nearer than the grid line and within tolerance.
    CHECK_EQ(snap.edge(30200), static_cast<int64_t>(30000));
    CHECK_EQ(snap.edge(30400), static_cast<int64_t>(30500));
    CHECK_EQ(snap.edge(40300), static_cast<int64_t>(40500));
    // Not to itself.
    CHECK_EQ(snap.edge(30400, 1, 0), static_cast<int64_t>(30000));
    // A moved clip lands its start, or else its end, on a line: whichever is nearer.
    CHECK_EQ(snap.start(5900, 3000), static_cast<int64_t>(6000));
    CHECK_EQ(snap.start(8800, 3000), static_cast<int64_t>(9000));
    CHECK(snap.start(-100, 3000) >= 0);

    grid.set_division(1);
    CHECK_EQ(snap.edge(14000), static_cast<int64_t>(24000));
    grid.set_time_signature(6, 8);
    CHECK_NEAR(grid.beats_per_bar(), 3.0, 1e-12);
}

TEST(tempo_grid_maps_beats_and_round_trips) {
    TempoGrid grid;
    CHECK_EQ(grid.numerator(), 4);
    CHECK_EQ(grid.denominator(), 4);
    CHECK_NEAR(grid.bpm(), 120.0, 1e-9);
    CHECK_EQ(grid.division(), 4);
    CHECK_NEAR(grid.seconds_at_beat(8.0), 4.0, 1e-12);
    grid.set_tempo_map({{4.0, 0.25}, {0.0, 0.5}});
    CHECK(grid.has_tempo_changes());
    CHECK_NEAR(grid.seconds_at_beat(6.0), 2.5, 1e-12);
    CHECK_NEAR(grid.beat_at_seconds(2.5), 6.0, 1e-12);
    CHECK_NEAR(grid.snap_seconds(2.3), 2.3125, 1e-12);
    grid.set_time_signature(7, 5);
    CHECK_EQ(grid.denominator(), 8);
    grid.set_division(3);

    TempoGrid loaded;
    CHECK(loaded.load(grid.save()));
    CHECK_EQ(loaded.numerator(), 7);
    CHECK_EQ(loaded.denominator(), 8);
    CHECK_EQ(loaded.division(), 3);
    CHECK_EQ(loaded.tempo_map().size(), static_cast<size_t>(2));
    CHECK_NEAR(loaded.seconds_at_beat(6.0), 2.5, 1e-9);
    // Fields it does not find stay as they are.
    CHECK(loaded.load("{\"division\":2}"));
    CHECK_EQ(loaded.numerator(), 7);
    CHECK_EQ(loaded.division(), 2);
}

TEST(midi_playhead_follows_rendered_samples) {
    // One track named "Lead" in 3/4 at 120 bpm, 480 ticks a beat: C4 for beat 0, D4 for
    // beat 1, partly in running status.
    const unsigned char song[] = {
        'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
        'M', 'T', 'r', 'k', 0, 0, 0, 43,
        0x00, 0xFF, 0x03, 0x04, 'L', 'e', 'a', 'd',
        0x00, 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08,
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
        0x00, 0x90, 60, 100, 0x83, 0x60, 60, 0,
        0x00, 62, 100, 0x83, 0x60, 0x80, 62, 0,
        0x00, 0xFF, 0x2F, 0x00};
    const char* path = "tests/_tmp_song.mid";
    FILE* f = std::fopen(path, "wb");
    CHECK(f != nullptr);
    std::fwrite(song, 1, sizeof(song), f);
    std::fclose(f);

    Midi midi;
    CHECK(midi.set_path(path));
    const auto& notes = midi.piano_roll(0).GetNotes();
    CHECK_EQ(notes.size(), static_cast<size_t>(2));
    CHECK_EQ(notes[0].pitch, 60);
    CHECK_NEAR(notes[0].startBeat, 0.0f, 1e-6f);
    CHECK_NEAR(notes[0].duration, 1.0f, 1e-6f);
    CHECK_EQ(notes[1].pitch, 62);
    CHECK_NEAR(notes[1].startBeat, 1.0f, 1e-6f);
    CHECK_NEAR(notes[1].duration, 1.0f, 1e-6f);
    CHECK(midi.track_name(0) == "Lead");
    CHECK_NEAR(midi.piano_roll(0).GetBeatsPerBar(), 3.0f, 1e-6f);
    CHECK_NEAR(midi.piano_roll(5).GetBeatsPerBar(), 3.0f, 1e-6f);
    // The file sets the song grid.
    CHECK_EQ(song_grid().numerator(), 3);
    CHECK_EQ(song_grid().denominator(), 4);
    CHECK_NEAR(song_grid().seconds_at_beat(2.0), 2 * 1041 * 480 / 1.0e6, 1e-9);
    CHECK(!midi.needs_control_lock());
    // 500000 us a beat plays as 1041 us a tick.
    CHECK_NEAR(midi.piano_roll(0).SecondsAtBeat(2.0f), 2 * 1041 * 480 / 1.0e6f, 1e-5f);
    CHECK_NEAR(midi.playhead_beat(), 0.0, 1e-9);

    reset_transport();
    std::vector<float> out(2 * 12000, 0.0f); // a quarter second: half a beat
    const auto energy_of_out = [&] {
        float energy = 0.0f;
        for (float s : out) {
            energy += s * s;
        }
        return energy;
    };
    midi.set_output_buffer(0, out.data());
    run_block(midi, out.size());
    CHECK_NEAR(midi.playhead_beat(), 0.5, 0.01);
    CHECK_NEAR(midi.playhead_seconds(), 0.25, 1e-9);
    CHECK(energy_of_out() > 1.0e-3f);
    run_block(midi, out.size());
    run_block(midi, out.size());
    CHECK_NEAR(midi.playhead_beat(), 1.5, 0.01);

    transport().pause();
    run_block(midi, out.size());
    CHECK_NEAR(midi.playhead_beat(), 1.5, 0.01);
    CHECK_EQ(energy_of_out(), 0.0f);

    transport().play();
    for (int i = 0; i < 4; ++i) {
        run_block(midi, out.size());
    }
    CHECK(midi.finished());
    CHECK_NEAR(midi.playhead_beat(), 2.0, 1e-9);

    // Back from the end: it plays again from where the transport went.
    transport().seek(0);
    run_block(midi, out.size());
    CHECK(!midi.finished());
    CHECK_NEAR(midi.playhead_beat(), 0.5, 0.01);
    CHECK(energy_of_out() > 1.0e-3f);
    // Forward, skipping the end of the first note: the second plays from 1.25 beats.
    transport().seek(18000);
    run_block(midi, out.size());
    CHECK_NEAR(midi.playhead_beat(), 1.25, 0.01);
    CHECK(energy_of_out() > 1.0e-3f);

    // Loading it again starts over.
    CHECK(midi.set_path(path));
    CHECK(!midi.finished());
    CHECK_NEAR(midi.playhead_beat(), 0.0, 1e-9);
    CHECK_NEAR(midi.playhead_seconds(), 0.0, 1e-9);
    reset_transport();
    song_grid() = TempoGrid{};
    std::remove(path);
}

TEST(piano_roll_copy_paste_and_duplicate_selection) {
    audio_effect::PianoRollEditor roll;
    roll.AddNote(60, 2.0f, 1.0f);
    roll.AddNote(64, 2.5f, 0.75f);
    auto selected = [&] {
        std::vector<std::pair<int, float>> out;
        for (const auto& note : roll.GetNotes()) {
            if (note.selected) {
                out.emplace_back(note.pitch, note.startBeat);
            }
        }
        return out;
    };
    using Notes = std::vector<std::pair<int, float>>;

    // The selection spans 1.25 beats, already on the quarter-beat grid.
    roll.SelectAll();
    roll.DuplicateSelection();
    CHECK_EQ(roll.GetNotes().size(), static_cast<size_t>(4));
    CHECK(selected() == (Notes{{60, 3.25f}, {64, 3.75f}}));

    roll.CopySelection();
    roll.Paste(10.0f);
    CHECK_EQ(roll.GetNotes().size(), static_cast<size_t>(6));
    CHECK(selected() == (Notes{{60, 10.0f}, {64, 10.5f}}));

    roll.CutSelection();
    CHECK_EQ(roll.GetNotes().size(), static_cast<size_t>(4));
    CHECK(selected().empty());
    roll.Paste(0.0f);
    CHECK(selected() == (Notes{{60, 0.0f}, {64, 0.5f}}));

    // The clipboard is shared, so notes paste into another roll.
    audio_effect::PianoRollEditor other;
    other.Paste(1.0f);
    CHECK_EQ(other.GetNotes().size(), static_cast<size_t>(2));
}

TEST(piano_roll_seconds_follow_the_tempo_map) {
    audio_effect::PianoRollEditor roll;
    CHECK_NEAR(roll.SecondsAtBeat(4.0f), 2.0f, 1e-6f); // SetTempo's 120 bpm
    roll.SetTempoMap({{0.0f, 0.5f}, {4.0f, 0.25f}});
    CHECK_NEAR(roll.SecondsAtBeat(4.0f), 2.0f, 1e-6f);
    CHECK_NEAR(roll.SecondsAtBeat(6.0f), 2.5f, 1e-6f);
    CHECK_NEAR(roll.BeatAtSeconds(1.0f), 2.0f, 1e-6f);
    CHECK_NEAR(roll.BeatAtSeconds(2.5f), 6.0f, 1e-6f);
}

TEST(mixer_dsp_averages_sources) {
    Mixer mixer;
    CHECK(mixer.name() == "mixer");
    std::vector<float> a(8, 0.2f);
    std::vector<float> b(8, 0.6f);
    std::vector<float> out(8, -1.0f);
    const float* sources[] = {a.data(), b.data()};
    mixer.set_output_buffer(0, out.data());
    mixer.process(sources, 2, out.size(), 48000.0f);
    CHECK_NEAR(out[0], 0.4f, 1e-5f);
    CHECK_NEAR(out[7], 0.4f, 1e-5f);
}

TEST(mixer_gains_save_load_and_scale) {
    Mixer mixer;
    mixer.gain[0] = 0.5f;
    mixer.gain[1] = 1.5f;
    const std::string fields = mixer.save();
    CHECK(fields.find("\"gain_0\"") != std::string::npos);
    CHECK(fields.find("\"gain_1\"") != std::string::npos);

    Mixer loaded;
    CHECK(loaded.load(R"({"gain_0":0.25,"gain_1":2.0})"));
    CHECK_NEAR(loaded.gain[0], 0.25f, 1e-5f);
    CHECK_NEAR(loaded.gain[1], 2.0f, 1e-5f);

    std::vector<float> a(4, 1.0f);
    std::vector<float> b(4, 1.0f);
    std::vector<float> out(4, 0.0f);
    const float* sources[] = {a.data(), b.data()};
    mixer.gain[0] = 1.0f;
    mixer.gain[1] = 0.0f;
    mixer.set_output_buffer(0, out.data());
    mixer.process(sources, 2, out.size(), 48000.0f);
    // average of (1*1 + 1*0) / 2 = 0.5
    CHECK_NEAR(out[0], 0.5f, 1e-5f);
}

TEST(mixer_mute_skips_input_and_persists) {
    Mixer mixer;
    mixer.input_muted[1] = true;
    Mixer loaded;
    CHECK(loaded.load(mixer.save()));
    CHECK(!loaded.input_muted[0]);
    CHECK(loaded.input_muted[1]);

    std::vector<float> a(4, 0.2f);
    std::vector<float> b(4, 0.6f);
    std::vector<float> out(4, 0.0f);
    const float* sources[] = {a.data(), b.data()};
    loaded.set_output_buffer(0, out.data());
    loaded.process(sources, 2, out.size(), 48000.0f);
    CHECK_NEAR(out[0], 0.2f, 1e-5f);
}

TEST(effect_with_unconnected_input_outputs_silence) {
    EqThreeBand eq;
    std::vector<float> out(16, 1.0f);
    const float* inputs[] = {nullptr};
    eq.set_output_buffer(0, out.data());
    eq.process(inputs, 1, out.size(), 48000.0f);
    for (float s : out) {
        CHECK_NEAR(s, 0.0f, 1e-6f);
    }
}

TEST(noise_gate_silences_below_threshold) {
    NoiseGate gate;
    std::vector<float> quiet(64, 0.0001f);
    gate.threshold_db = -20.0f;
    gate.attack_ms = 1.0f;
    gate.release_ms = 1.0f;
    run_effect(gate, quiet);
    float max_abs = 0.0f;
    for (float s : quiet) {
        max_abs = std::max(max_abs, std::fabs(s));
    }
    CHECK(max_abs < 0.0002f);
}

TEST(eq_three_band_runs) {
    EqThreeBand eq;
    std::vector<float> block(128, 0.25f);
    eq.low_db = 3.0f;
    eq.mid_db = -3.0f;
    eq.high_db = 2.0f;
    run_effect(eq, block);
    CHECK(std::isfinite(block[0]));
}

TEST(reverb_mix_changes_output) {
    SimpleReverb rev;
    // Feed a short burst, then silence — tails should remain in later frames.
    std::vector<float> block(4096, 0.0f);
    for (size_t i = 0; i < 64; ++i) {
        block[i] = (i % 2 == 0) ? 0.8f : -0.8f;
    }
    rev.room = 0.9f;
    rev.damping = 0.2f;
    rev.mix = 1.0f;
    run_effect(rev, block);
    float energy = 0.0f;
    for (size_t i = 512; i < block.size(); ++i) {
        energy += block[i] * block[i];
    }
    CHECK(energy > 1.0e-6f);
}

TEST(dry_wet_mix_blends_slots) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int pre_a = g.add_node("preamp", 0, 0);
    const int pre_b = g.add_node("preamp", 0, 0);
    const int mix = g.add_node("dry_wet_mix", 0, 0);
    const int out = g.add_node("output", 0, 0);
    g.find_node(pre_a)->as<Preamp>()->gain_db = 0.0f;
    g.find_node(pre_b)->as<Preamp>()->gain_db = 6.0f;
    g.find_node(mix)->as<DryWetMix>()->mix = 0.5f;
    CHECK(g.add_link(in, 0, pre_a, 0));
    CHECK(g.add_link(in, 0, pre_b, 0));
    CHECK(g.add_link(pre_a, 0, mix, 0));
    CHECK(g.add_link(pre_b, 0, mix, 1));
    CHECK(g.add_link(mix, 0, out, 0));

    constexpr size_t block = 32;
    std::vector<float> live(block, 0.25f);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    auto compiled = g.compile();
    CHECK(compiled);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    // 0.5 * (0.25 + ~0.5) ≈ 0.375
    CHECK_NEAR(output[0], 0.375f, 0.04f);
}

TEST(wav_recorder_writes_file) {
    const char* path = "tests/_tmp_record.wav";
    std::remove(path);
    WavRecorderEngine rec;
    rec.set_armed(true, path, 48000.0f);
    std::vector<float> block(64, 0.1f);
    rec.write_samples(block.data(), block.size());
    rec.set_armed(false, path, 48000.0f);
    FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    if (f) {
        char riff[4];
        CHECK_EQ(static_cast<int>(std::fread(riff, 1, 4, f)), 4);
        CHECK(riff[0] == 'R' && riff[1] == 'I' && riff[2] == 'F' && riff[3] == 'F');
        std::fclose(f);
    }
    std::remove(path);
}

TEST(wav_recorder_finishes_file_when_destroyed_while_armed) {
    const char* path = "tests/_tmp_record_destroyed.wav";
    std::remove(path);
    {
        WavRecorderEngine rec;
        rec.set_armed(true, path, 48000.0f);
        std::vector<float> block(64, 0.1f);
        rec.write_samples(block.data(), block.size());
    }
    std::string error;
    auto clip = WavGenerator::load(path, 48000, error);
    CHECK(clip != nullptr);
    if (clip) {
        CHECK_EQ(static_cast<int>(clip->interleaved_stereo.size()), 64);
        CHECK_NEAR(clip->interleaved_stereo[63], 0.1f, 1e-6f);
    }
    std::remove(path);
}

TEST(wav_recorder_armed_is_not_persisted) {
    WavRecorder rec;
    rec.armed = true;
    CHECK(rec.save().find("armed") == std::string::npos);
    WavRecorder loaded;
    CHECK(loaded.load(R"({"armed":true})"));
    CHECK(!loaded.armed);
}

TEST(wav_recorder_path_is_saved) {
    WavRecorder rec;
    CHECK(rec.path == kDefaultRecordPath);
    rec.path = "out/take.wav";
    WavRecorder loaded;
    CHECK(loaded.load(rec.save()));
    CHECK(loaded.path == "out/take.wav");
}

TEST(drive_pedal_amount_zero_is_clean_for_every_model) {
    for (int model = 0; model < 3; ++model) {
        guitar_pedals::DrivePedal pedal;
        pedal.model = model;
        pedal.amount = 0.0f;
        pedal.tone = 1.0f;
        std::vector<float> block(4096, 0.01f);
        run_effect(pedal, block);
        float peak = 0.0f;
        bool finite = true;
        for (float s : block) {
            finite = finite && std::isfinite(s);
            peak = std::max(peak, std::fabs(s));
        }
        CHECK(finite);
        // Small signals stay small (no full-scale square) and are not muted.
        CHECK(peak < 0.05f);
        CHECK(std::fabs(block.back()) > 0.005f);
    }
}

TEST(flush_denormals_to_zero_applies_to_calling_thread) {
    bool flushed = false;
    std::thread worker([&] {
        enable_flush_denormals_to_zero();
        volatile float tiny = 1.0e-38f;
        volatile float half = 0.5f;
        const float product = tiny * half;
        flushed = product == 0.0f;
    });
    worker.join();
#if defined(__SSE__) || defined(_M_X64) || defined(__aarch64__)
    CHECK(flushed);
#else
    (void)flushed;
#endif
}

// Feeds `seconds` of a stereo 440 Hz sine with the given RMS through the AGC in
// 512-sample blocks and returns the RMS (dB) of the last block.
static float agc_settled_rms_db(Agc& agc, float input_rms_db, float seconds) {
    const float amp = std::pow(10.0f, input_rms_db / 20.0f) * std::sqrt(2.0f);
    std::vector<float> block(512);
    size_t frame = 0;
    const size_t blocks = static_cast<size_t>(seconds * 48000.0f / 256.0f);
    double sum_sq = 0.0;
    for (size_t b = 0; b < blocks; ++b) {
        for (size_t i = 0; i < block.size(); i += 2, ++frame) {
            const float s = amp * std::sin(2.0f * 3.14159265f * 440.0f * frame / 48000.0f);
            block[i] = s;
            block[i + 1] = s;
        }
        run_effect(agc, block);
        sum_sq = 0.0;
        for (float s : block) {
            sum_sq += static_cast<double>(s) * s;
        }
    }
    return 10.0f * std::log10(static_cast<float>(sum_sq / block.size()));
}

TEST(agc_raises_quiet_input_to_target) {
    Agc agc;
    agc.target_db = -18.0f;
    CHECK_NEAR(agc_settled_rms_db(agc, -36.0f, 4.0f), -18.0f, 1.5f);
    CHECK(agc.gain_db() > 15.0f);
}

TEST(agc_lowers_loud_input_to_target) {
    Agc agc;
    agc.target_db = -18.0f;
    CHECK_NEAR(agc_settled_rms_db(agc, -6.0f, 1.0f), -18.0f, 1.5f);
}

TEST(agc_boost_is_capped_by_max_gain) {
    Agc agc;
    agc.target_db = -18.0f;
    agc.max_gain_db = 6.0f;
    CHECK_NEAR(agc_settled_rms_db(agc, -40.0f, 4.0f), -34.0f, 1.0f);
}

TEST(agc_holds_gain_below_gate) {
    Agc agc;
    agc.gate_db = -60.0f;
    agc_settled_rms_db(agc, -80.0f, 2.0f);
    CHECK_NEAR(agc.gain_db(), 0.0f, 1e-3f);
}

TEST(agc_save_load_roundtrip) {
    Agc agc;
    agc.target_db = -12.0f;
    agc.max_gain_db = 10.0f;
    agc.release_ms = 900.0f;
    Agc loaded;
    CHECK(loaded.load(agc.save()));
    CHECK_NEAR(loaded.target_db, -12.0f, 1e-5f);
    CHECK_NEAR(loaded.max_gain_db, 10.0f, 1e-5f);
    CHECK_NEAR(loaded.release_ms, 900.0f, 1e-5f);
}
