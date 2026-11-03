#include "test_harness.hpp"

#include "graph/audio_graph.hpp"
#include "dsp/preamp.hpp"
#include "dsp/wav_player.hpp"
#include "graph/graph_io.hpp"
#include "nodes/multi_wav_generator.hpp"
#include "model/tempo_grid.hpp"
#include "paths.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>

TEST(graph_io_serialize_deserialize_roundtrip) {
    AudioGraph g;
    std::string load_error;
    CHECK(graph_io::load_from_file(g, kDefaultPatchPath, load_error));
    if (auto* wav = g.find_node(g.nodes().front().id)->as<WavPlayer>()) {
        wav->loop = false;
    }
    for (auto& n : g.nodes()) {
        if (n.module->name() == "preamp") {
            if (auto* p = n.as<Preamp>()) {
                p->gain_db = 3.5f;
            }
        }
    }

    const std::string text = graph_io::serialize(g);
    CHECK(text.find("\"version\":2") != std::string::npos);
    CHECK(text.find("mixer") != std::string::npos);
    CHECK(text.find("\"gain_db\"") != std::string::npos);

    AudioGraph loaded;
    loaded.clear();
    std::string error;
    CHECK(graph_io::deserialize(loaded, text, error));
    CHECK_EQ(static_cast<int>(loaded.nodes().size()), static_cast<int>(g.nodes().size()));
    CHECK_EQ(static_cast<int>(loaded.links().size()), static_cast<int>(g.links().size()));

    bool found_gain = false;
    for (const auto& n : loaded.nodes()) {
        if (n.module->name() == "preamp") {
            CHECK_NEAR(n.as<Preamp>()->gain_db, 3.5f, 1e-4f);
            found_gain = true;
        }
    }
    CHECK(found_gain);
}

TEST(graph_io_file_roundtrip) {
    AudioGraph g;
    std::string error;
    CHECK(graph_io::load_from_file(g, kDefaultPatchPath, error));
    const std::string path = "tests/_tmp_graph.json";
    CHECK(graph_io::save_to_file(g, path, error));

    AudioGraph loaded;
    loaded.clear();
    CHECK(graph_io::load_from_file(loaded, path, error));
    CHECK_EQ(static_cast<int>(loaded.nodes().size()), static_cast<int>(g.nodes().size()));
    std::remove(path.c_str());
}

TEST(graph_io_preserves_mixer_to_slot) {
    AudioGraph g;
    g.clear();
    const int a = g.add_node("wav_player", 0, 0);
    const int b = g.add_node("preamp", 0, 0);
    const int m = g.add_node("mixer", 0, 0);
    CHECK(g.add_link(a, 0, m, 0));
    CHECK(g.add_link(b, 0, m, 1));

    std::string error;
    AudioGraph loaded;
    CHECK(graph_io::deserialize(loaded, graph_io::serialize(g), error));
    int slot_sum = 0;
    for (const auto& link : loaded.links()) {
        if (link.to_node == m) {
            slot_sum += link.to_slot;
        }
    }
    CHECK_EQ(slot_sum, 1); // slots 0 and 1
}

TEST(graph_io_rejects_duplicate_ids) {
    const char* dup_nodes = R"({"version":2,"nodes":[
        {"id":1,"type":"preamp","x":0,"y":0},
        {"id":1,"type":"output","x":0,"y":0}],"links":[]})";
    const char* dup_links = R"({"version":2,"nodes":[
        {"id":1,"type":"preamp","x":0,"y":0},
        {"id":2,"type":"output","x":0,"y":0}],
        "links":[{"id":5,"from":1,"to":2,"to_slot":0},{"id":5,"from":1,"to":2,"to_slot":0}]})";
    AudioGraph g;
    std::string error;
    CHECK(!graph_io::deserialize(g, dup_nodes, error));
    CHECK(error.find("Duplicate node id") != std::string::npos);
    CHECK(!graph_io::deserialize(g, dup_links, error));
    CHECK(error.find("Duplicate link id") != std::string::npos);
}

TEST(graph_io_new_ids_follow_largest_loaded_id) {
    const char* text = R"({"version":2,"nodes":[
        {"id":7,"type":"preamp","x":0,"y":0},
        {"id":9,"type":"output","x":0,"y":0}],
        "links":[{"id":4,"from":7,"to":9,"to_slot":0}]})";
    AudioGraph g;
    std::string error;
    CHECK(graph_io::deserialize(g, text, error));
    const int added = g.add_node("preamp", 0, 0);
    CHECK_EQ(added, 10);
    CHECK(g.add_link(added, 0, 7, 0));
    CHECK_EQ(g.links().back().id, 5);
}

TEST(graph_io_keeps_output_slots_and_drops_missing_ones) {
    const char* text = R"({"version":2,"nodes":[
        {"id":1,"type":"timeline","x":0,"y":0},
        {"id":2,"type":"output","x":0,"y":0},
        {"id":3,"type":"preamp","x":0,"y":0}],
        "links":[{"id":1,"from":1,"from_slot":5,"to":2,"to_slot":0},
                 {"id":2,"from":1,"from_slot":6,"to":3,"to_slot":0}]})";
    AudioGraph g;
    std::string error;
    CHECK(graph_io::deserialize(g, text, error));
    CHECK_EQ(static_cast<int>(g.links().size()), 1); // the timeline has no seventh output
    AudioGraph loaded;
    CHECK(graph_io::deserialize(loaded, graph_io::serialize(g), error));
    CHECK_EQ(static_cast<int>(loaded.links().size()), 1);
    CHECK_EQ(loaded.links().front().from_slot, 5);
}

TEST(graph_io_rejects_two_links_into_one_input_pin) {
    const char* text = R"({"version":2,"nodes":[
        {"id":1,"type":"preamp","x":0,"y":0},
        {"id":2,"type":"preamp","x":0,"y":0},
        {"id":3,"type":"output","x":0,"y":0}],
        "links":[{"id":1,"from":1,"to":3,"to_slot":0},{"id":2,"from":2,"to":3,"to_slot":0}]})";
    AudioGraph g;
    std::string error;
    CHECK(!graph_io::deserialize(g, text, error));
    CHECK(error.find("already connected") != std::string::npos);
}

TEST(graph_io_rejects_cycles) {
    const char* text = R"({"version":2,"nodes":[
        {"id":1,"type":"preamp","x":0,"y":0},
        {"id":2,"type":"preamp","x":0,"y":0}],
        "links":[{"id":1,"from":1,"to":2,"to_slot":0},{"id":2,"from":2,"to":1,"to_slot":0}]})";
    AudioGraph g;
    std::string error;
    const int before = g.add_node("output", 0, 0);
    CHECK(!graph_io::deserialize(g, text, error));
    CHECK(error.find("cycle") != std::string::npos);
    CHECK(g.find_node(before) != nullptr); // the graph is left as it was
}

TEST(graph_io_params_may_precede_type) {
    const char* text = R"({"version":2,"nodes":[
        {"id":1,"params":{"gain_db":4.5},"type":"preamp","x":0,"y":0}],"links":[]})";
    AudioGraph g;
    std::string error;
    CHECK(graph_io::deserialize(g, text, error));
    CHECK_NEAR(g.find_node(1)->as<Preamp>()->gain_db, 4.5f, 1e-5f);
}

TEST(graph_io_rejects_unknown_module) {
    AudioGraph g;
    std::string error;
    CHECK(!graph_io::deserialize(
        g, R"({"version":2,"nodes":[{"id":1,"type":"nope","x":0,"y":0}],"links":[]})", error));
    CHECK(error.find("Unknown module name") != std::string::npos);
}

TEST(graph_io_bypass_roundtrip) {
    AudioGraph g;
    const int pre = g.add_node("preamp", 0, 0);
    g.find_node(pre)->bypassed = true;
    g.find_node(pre)->as<Preamp>()->gain_db = 3.5f;
    AudioGraph loaded;
    std::string error;
    CHECK(graph_io::deserialize(loaded, graph_io::serialize(g), error));
    CHECK(loaded.find_node(pre)->bypassed);
    CHECK_NEAR(loaded.find_node(pre)->as<Preamp>()->gain_db, 3.5f, 1e-4f);
}

TEST(graph_io_rejects_other_versions) {
    AudioGraph g;
    std::string error;
    CHECK(!graph_io::deserialize(g, R"({"version":1,"nodes":[],"links":[]})", error));
    CHECK(error.find("Unsupported patch version 1") != std::string::npos);
    CHECK(!graph_io::deserialize(g, R"({"nodes":[],"links":[]})", error));
}

TEST(graph_io_bundled_patches_load) {
    const char* patches[] = {"assets/patches/default_patch.json", "assets/patches/demo_patch.json",
                             "assets/patches/ui_demo.json", "assets/patches/visualizer_demo.json"};
    for (const char* path : patches) {
        AudioGraph g;
        std::string error;
        CHECK(graph_io::load_from_file(g, path, error));
        CHECK(!g.nodes().empty());

        std::ifstream file(path);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        size_t links_in_file = 0;
        for (size_t at = text.find("\"from\":"); at != std::string::npos; at = text.find("\"from\":", at + 1)) {
            ++links_in_file;
        }
        CHECK_EQ(g.links().size(), links_in_file);
    }
}

TEST(graph_io_saves_nodes_before_links) {
    AudioGraph g;
    std::string load_error;
    CHECK(graph_io::load_from_file(g, kDefaultPatchPath, load_error));
    const std::string text = graph_io::serialize(g);
    CHECK(text.find("\"nodes\"") < text.find("\"links\""));
    CHECK(text.find("next_node_id") == std::string::npos);
    CHECK(text.find("next_link_id") == std::string::npos);
    CHECK(text.find("wav_path") == std::string::npos);
}

TEST(graph_io_saves_the_song_grid) {
    AudioGraph g;
    g.add_node("multi_wav", 0, 0);
    song_grid() = TempoGrid{};
    song_grid().set_time_signature(5, 8);
    song_grid().set_bpm(90.0);
    song_grid().set_division(3);
    const std::string text = graph_io::serialize(g);
    CHECK(text.find("\"grid\":") != std::string::npos);

    song_grid() = TempoGrid{};
    AudioGraph loaded;
    std::string error;
    CHECK(graph_io::deserialize(loaded, text, error));
    CHECK_EQ(song_grid().numerator(), 5);
    CHECK_EQ(song_grid().denominator(), 8);
    CHECK_NEAR(song_grid().bpm(), 90.0, 1e-4);
    CHECK_EQ(song_grid().division(), 3);

    // A patch from before the grid gets the default one.
    CHECK(graph_io::deserialize(loaded, R"({"version":2,"nodes":[
        {"id":1,"type":"preamp","x":0,"y":0,"params":{}}],"links":[]})", error));
    CHECK_EQ(song_grid().numerator(), 4);
    CHECK_NEAR(song_grid().bpm(), 120.0, 1e-9);

    // A patch that fails to load leaves the grid as it was.
    song_grid().set_time_signature(3, 4);
    CHECK(!graph_io::deserialize(loaded, R"({"version":2,"grid":{"numerator":7},"nodes":[
        {"id":1,"type":"nope","x":0,"y":0}],"links":[]})", error));
    CHECK_EQ(song_grid().numerator(), 3);
    song_grid() = TempoGrid{};
}

TEST(graph_io_converts_clip_timelines_to_multi_wav) {
    // A patch saved with the Clip Timeline: track 1 holds a trimmed clip and a whole file,
    // track 3 uses the one-file keys from before tracks held several clips, and three of its
    // pins are linked.
    const std::string wav = kDefaultWavPath;
    const std::string text = R"({"version":2,"nodes":[
        {"id":7,"type":"timeline","x":120.5,"y":64,"params":{"bypassed":true,"cursor":0,
            "track1_clips":[{"path":")" + wav + R"(","start":100,"offset":50,"length":400},
                            {"path":"assets/audio/rhythm.wav","start":9000,"offset":0}],
            "track3_path":")" + wav + R"(","track3_start":2500,
            "track6_clips":[{"path":"assets/audio/gone.wav","start":7,"offset":3,"length":9}]}},
        {"id":2,"type":"mixer","x":400,"y":0,"params":{}},
        {"id":3,"type":"output","x":600,"y":0,"params":{}}],
        "links":[{"id":1,"from":7,"from_slot":0,"to":2,"to_slot":0},
                 {"id":2,"from":7,"from_slot":2,"to":2,"to_slot":1},
                 {"id":3,"from":7,"from_slot":5,"to":2,"to_slot":2},
                 {"id":4,"from":2,"from_slot":0,"to":3,"to_slot":0}]})";
    AudioGraph g;
    std::string error;
    CHECK(graph_io::deserialize(g, text, error));
    const GraphNode* node = g.find_node(7);
    CHECK(node != nullptr);
    CHECK(node->module->name() == "multi_wav");
    CHECK_NEAR(node->x, 120.5f, 1e-6f);
    CHECK_NEAR(node->y, 64.0f, 1e-6f);
    CHECK(node->bypassed);
    auto* wavs = node->as<MultiWavGenerator>();
    CHECK(wavs != nullptr);
    CHECK_EQ(wavs->channel_count(), 6);
    CHECK_EQ(wavs->output_count(), 6);

    const ClipArrangement& clips = wavs->arrangement();
    CHECK_EQ(clips.clips(0).size(), static_cast<size_t>(2));
    CHECK(clips.clips(0)[0].path == wav);
    CHECK_EQ(clips.clips(0)[0].start, static_cast<int64_t>(100));
    CHECK_EQ(clips.clips(0)[0].offset, static_cast<int64_t>(50));
    CHECK_EQ(clips.clips(0)[0].frames(), static_cast<int64_t>(400));
    CHECK_EQ(clips.clips(0)[1].start, static_cast<int64_t>(9000));
    CHECK_EQ(clips.clips(0)[1].length, static_cast<int64_t>(-1));
    CHECK(clips.clips(1).empty());
    CHECK_EQ(clips.clips(2).size(), static_cast<size_t>(1));
    CHECK_EQ(clips.clips(2)[0].start, static_cast<int64_t>(2500));
    CHECK_EQ(clips.clips(2)[0].offset, static_cast<int64_t>(0));
    // A missing file keeps its clip.
    CHECK_EQ(clips.clips(5).size(), static_cast<size_t>(1));
    CHECK_EQ(clips.clips(5)[0].length, static_cast<int64_t>(9));

    // Every link from its pins survives, on the same pins.
    CHECK_EQ(g.links().size(), static_cast<size_t>(4));
    for (int slot : {0, 2, 5}) {
        CHECK(std::any_of(g.links().begin(), g.links().end(), [&](const GraphLink& l) {
            return l.from_node == 7 && l.from_slot == slot;
        }));
    }

    // Saved as a Multi WAV, which loads the same.
    const std::string saved = graph_io::serialize(g);
    CHECK(saved.find("\"timeline\"") == std::string::npos);
    CHECK(saved.find("\"multi_wav\"") != std::string::npos);
    CHECK(saved.find("track1_clips") == std::string::npos);
    AudioGraph again;
    CHECK(graph_io::deserialize(again, saved, error));
    const auto* reloaded = again.find_node(7)->as<MultiWavGenerator>();
    CHECK(reloaded != nullptr);
    CHECK_EQ(reloaded->channel_count(), 6);
    CHECK_EQ(reloaded->arrangement().clips(0)[0].offset, static_cast<int64_t>(50));
    CHECK_EQ(reloaded->arrangement().clips(2)[0].start, static_cast<int64_t>(2500));
    CHECK_EQ(again.links().size(), static_cast<size_t>(4));
}
