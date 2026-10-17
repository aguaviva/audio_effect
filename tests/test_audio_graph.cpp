#include "test_harness.hpp"

#include "graph/audio_graph.hpp"
#include "graph/node_types.hpp"

TEST(audio_graph_add_remove_nodes_and_links) {
    AudioGraph g;
    g.clear();
    const int a = g.add_node(NodeType::wav_player, 0, 0);
    const int b = g.add_node(NodeType::preamp, 10, 0);
    const int c = g.add_node(NodeType::output, 20, 0);
    CHECK(g.add_link(a, b));
    CHECK(g.add_link(b, c));
    CHECK_EQ(static_cast<int>(g.links().size()), 2);
    CHECK(g.remove_node(b));
    CHECK_EQ(static_cast<int>(g.nodes().size()), 2);
    CHECK_EQ(static_cast<int>(g.links().size()), 0); // incident links removed
}

TEST(audio_graph_rejects_cycles_and_duplicates) {
    AudioGraph g;
    g.clear();
    const int a = g.add_node(NodeType::preamp, 0, 0);
    const int b = g.add_node(NodeType::low_pass, 0, 0);
    CHECK(g.add_link(a, b));
    CHECK(!g.add_link(a, b));      // duplicate
    CHECK(!g.add_link(b, a));      // cycle
    CHECK(!g.add_link(a, a));      // self
}

TEST(audio_graph_mixer_auto_pins) {
    AudioGraph g;
    g.clear();
    const int src1 = g.add_node(NodeType::wav_player, 0, 0);
    const int src2 = g.add_node(NodeType::chord_generator, 0, 0);
    const int mix = g.add_node(NodeType::mixer, 0, 0);
    const int out = g.add_node(NodeType::output, 0, 0);

    CHECK_EQ(g.mixer_pin_count(mix), 1);
    CHECK_EQ(g.mixer_connected_count(mix), 0);
    CHECK(g.add_link(src1, mix, 0));
    CHECK_EQ(g.mixer_connected_count(mix), 1);
    CHECK_EQ(g.mixer_pin_count(mix), 2);
    CHECK(g.add_link(src2, mix, 1));
    CHECK_EQ(g.mixer_pin_count(mix), 3);
    CHECK(!g.add_link(src1, mix, 1)); // slot occupied
    CHECK(g.add_link(mix, out));
}

TEST(audio_graph_compile_topological_order) {
    AudioGraph g;
    g.clear();
    const int a = g.add_node(NodeType::wav_player, 0, 0);
    const int b = g.add_node(NodeType::preamp, 0, 0);
    const int c = g.add_node(NodeType::output, 0, 0);
    CHECK(g.add_link(a, b));
    CHECK(g.add_link(b, c));
    auto compiled = g.compile();
    CHECK(compiled);
    CHECK(compiled->valid);
    CHECK_EQ(static_cast<int>(compiled->order.size()), 3);
    CHECK(compiled->order.front().type() == NodeType::wav_player);
    CHECK(compiled->order.back().type() == NodeType::output);
}

TEST(audio_graph_default_chain_has_mixer_and_output) {
    AudioGraph g;
    g.reset_to_default_chain();
    bool has_mixer = false;
    bool has_output = false;
    bool has_wav = false;
    for (const auto& n : g.nodes()) {
        has_mixer = has_mixer || n.type() == NodeType::mixer;
        has_output = has_output || n.type() == NodeType::output;
        has_wav = has_wav || n.type() == NodeType::wav_player;
    }
    CHECK(has_mixer);
    CHECK(has_output);
    CHECK(has_wav);
    auto compiled = g.compile();
    CHECK(compiled);
    CHECK(compiled->valid);
}
