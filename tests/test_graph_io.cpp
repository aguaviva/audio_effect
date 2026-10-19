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
