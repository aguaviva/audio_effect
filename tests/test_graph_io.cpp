#include "test_harness.hpp"

#include "graph/audio_graph.hpp"
#include "graph/graph_io.hpp"
#include "graph/node_state.hpp"

#include <cstdio>
#include <fstream>
#include <string>

TEST(graph_io_serialize_deserialize_roundtrip) {
    AudioGraph g;
    g.reset_to_default_chain();
    if (auto* wav = g.find_node(g.nodes().front().id())->state().as<node_params::WavPlayer>()) {
        wav->loop = false;
    }
    for (auto& n : g.nodes()) {
        if (n.type() == NodeType::preamp) {
            if (auto* p = n.state().as<node_params::Preamp>()) {
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
        if (n.type() == NodeType::preamp) {
            CHECK_NEAR(n.state().as<node_params::Preamp>()->gain_db, 3.5f, 1e-4f);
            found_gain = true;
        }
    }
    CHECK(found_gain);
}

TEST(graph_io_file_roundtrip) {
    AudioGraph g;
    g.reset_to_default_chain();
    const std::string path = "tests/_tmp_graph.json";
    std::string error;
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
    const int a = g.add_node(NodeType::wav_player, 0, 0);
    const int b = g.add_node(NodeType::preamp, 0, 0);
    const int m = g.add_node(NodeType::mixer, 0, 0);
    CHECK(g.add_link(a, m, 0));
    CHECK(g.add_link(b, m, 1));

    std::string error;
    AudioGraph loaded;
    CHECK(graph_io::deserialize(loaded, graph_io::serialize(g), error));
    int slot_sum = 0;
    for (const auto& link : loaded.links()) {
        if (link.to_node() == m) {
            slot_sum += link.to_slot();
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

TEST(graph_io_next_ids_never_reuse_loaded_ids) {
    const char* text = R"({"version":2,"next_node_id":1,"next_link_id":1,"nodes":[
        {"id":7,"type":"preamp","x":0,"y":0},
        {"id":9,"type":"output","x":0,"y":0}],
        "links":[{"id":4,"from":7,"to":9,"to_slot":0}]})";
    AudioGraph g;
    std::string error;
    CHECK(graph_io::deserialize(g, text, error));
    CHECK_EQ(g.next_node_id(), 10);
    CHECK_EQ(g.next_link_id(), 5);
    const int added = g.add_node(NodeType::preamp, 0, 0);
    CHECK_EQ(added, 10);
}

TEST(graph_io_params_may_precede_type) {
    const char* text = R"({"version":2,"nodes":[
        {"id":1,"params":{"gain_db":4.5},"type":"preamp","x":0,"y":0}],"links":[]})";
    AudioGraph g;
    std::string error;
    CHECK(graph_io::deserialize(g, text, error));
    CHECK_NEAR(g.find_node(1)->state().as<node_params::Preamp>()->gain_db, 4.5f, 1e-5f);
}

TEST(graph_io_migrates_v1_positional_params) {
    const char* text = R"({"version":1,"wav_path":"assets/audio/x.wav","nodes":[
        {"id":1,"type":"wav_player","x":0,"y":0,"params":{"f":[0,0,0,0,0,0,0,0],"i":[0,0,0,0],"b":[false,false,false,false]}},
        {"id":2,"type":"echo","x":0,"y":0,"params":{"f":[320,0.35,0.4,0,0,0,0,0],"i":[0,0,0,0],"b":[false,false,false,true]}},
        {"id":3,"type":"drive_pedal","x":0,"y":0,"params":{"f":[3.5,0.65,0,0,0,0,0,0],"i":[2,0,0,0],"b":[false,false,false,false]}},
        {"id":4,"type":"noise_gate","x":0,"y":0,"params":{"f":[-50,99,2,80,0,0,0,0],"i":[0,0,0,0],"b":[false,false,false,false]}},
        {"id":5,"type":"vu_meter","x":0,"y":0,"params":{"f":[1.5,0.85,0,0,0,0,0,0],"i":[0,0,0,0],"b":[false,false,false,false]}}
        ],"links":[]})";
    AudioGraph g;
    std::string error;
    CHECK(graph_io::deserialize(g, text, error));

    const auto* wav = g.find_node(1)->state().as<node_params::WavPlayer>();
    CHECK(wav && !wav->loop);
    CHECK(wav && wav->path() == "assets/audio/x.wav");

    const auto& echo_state = g.find_node(2)->state();
    const auto* echo = echo_state.as<node_params::Echo>();
    CHECK(echo_state.bypassed);
    CHECK_NEAR(echo->delay_ms, 320.0f, 1e-4f);
    CHECK_NEAR(echo->feedback, 0.35f, 1e-5f);
    CHECK_NEAR(echo->mix, 0.4f, 1e-5f);

    const auto* drive = g.find_node(3)->state().as<node_params::Drive>();
    CHECK_EQ(drive->model, 2);
    CHECK_NEAR(drive->amount, 3.5f, 1e-5f);
    CHECK_NEAR(drive->tone, 0.65f, 1e-5f);

    const auto* gate = g.find_node(4)->state().as<node_params::NoiseGate>();
    CHECK_NEAR(gate->threshold_db, -50.0f, 1e-4f);
    CHECK_NEAR(gate->attack_ms, 2.0f, 1e-5f);
    CHECK_NEAR(gate->release_ms, 80.0f, 1e-4f);

    const auto* vu = g.find_node(5)->state().as<node_params::VuMeter>();
    CHECK_NEAR(vu->display_gain, 1.5f, 1e-5f);
    CHECK_NEAR(vu->decay, 0.85f, 1e-5f);

    // Re-saving writes named keys, not the positional arrays.
    const std::string resaved = graph_io::serialize(g);
    CHECK(resaved.find("\"f\":") == std::string::npos);
    CHECK(resaved.find("\"delay_ms\"") != std::string::npos);
}

TEST(graph_io_bundled_patches_load) {
    const char* patches[] = {"assets/patches/default_patch.json", "assets/patches/demo_patch.json",
                             "assets/patches/ui_demo.json", "assets/patches/visualizer_demo.json"};
    for (const char* path : patches) {
        AudioGraph g;
        std::string error;
        CHECK(graph_io::load_from_file(g, path, error));
        CHECK(!g.nodes().empty());
    }
}
