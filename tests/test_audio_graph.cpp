#include "test_harness.hpp"

#include "graph/audio_graph.hpp"
#include "graph/graph_io.hpp"
#include "paths.hpp"

TEST(audio_graph_rejects_unknown_type_and_bad_pins) {
    AudioGraph g;
    CHECK_EQ(g.add_node("not_a_module", 0, 0), -1);
    const int src = g.add_node("white_noise", 0, 0);
    const int out = g.add_node("output", 0, 0);
    const int fx = g.add_node("preamp", 0, 0);
    CHECK(!g.add_link(out, fx)); // output has no output pin
    CHECK(!g.add_link(fx, src)); // sources have no input pin
    CHECK(!g.add_link(src, fx, 1)); // preamp has one input
    CHECK(g.add_link(src, fx));
}

TEST(audio_graph_add_remove_nodes_and_links) {
    AudioGraph g;
    g.clear();
    const int a = g.add_node("wav_player", 0, 0);
    const int b = g.add_node("preamp", 10, 0);
    const int c = g.add_node("output", 20, 0);
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
    const int a = g.add_node("preamp", 0, 0);
    const int b = g.add_node("low_pass", 0, 0);
    CHECK(g.add_link(a, b));
    CHECK(!g.add_link(a, b));      // duplicate
    CHECK(!g.add_link(b, a));      // cycle
    CHECK(!g.add_link(a, a));      // self
}

TEST(audio_graph_one_link_per_input_pin) {
    AudioGraph g;
    g.clear();
    const int a = g.add_node("white_noise", 0, 0);
    const int b = g.add_node("chord_generator", 0, 0);
    const int fx = g.add_node("preamp", 0, 0);
    const int out = g.add_node("output", 0, 0);
    CHECK(g.add_link(a, fx));
    CHECK(!g.add_link(b, fx)); // fan-in needs a mixer
    CHECK(g.add_link(fx, out));
    CHECK(!g.add_link(a, out)); // output pin already taken
}

TEST(audio_graph_compile_gives_every_link_a_buffer) {
    AudioGraph g;
    g.clear();
    const int src = g.add_node("white_noise", 0, 0);
    const int fx_a = g.add_node("preamp", 0, 0);
    const int fx_b = g.add_node("low_pass", 0, 0);
    CHECK(g.add_link(src, fx_a));
    CHECK(g.add_link(src, fx_b));
    auto compiled = g.compile();
    CHECK(compiled->valid);
    CHECK_EQ(static_cast<int>(compiled->link_count), 2);
    const auto& first = compiled->order.front();
    CHECK_EQ(first.id, src);
    CHECK_EQ(static_cast<int>(first.outputs.size()), 2);
    CHECK(first.outputs[0] != first.outputs[1]);
}

TEST(audio_graph_mixer_auto_pins) {
    AudioGraph g;
    g.clear();
    const int src1 = g.add_node("wav_player", 0, 0);
    const int src2 = g.add_node("chord_generator", 0, 0);
    const int mix = g.add_node("mixer", 0, 0);
    const int out = g.add_node("output", 0, 0);

    CHECK_EQ(g.input_pin_count(mix), 1);
    CHECK(g.add_link(src1, mix, 0));
    CHECK_EQ(g.input_pin_count(mix), 2);
    CHECK(g.add_link(src2, mix, 1));
    CHECK_EQ(g.input_pin_count(mix), 3);
    CHECK(!g.add_link(src1, mix, 1)); // slot occupied
    CHECK(g.add_link(mix, out));
    CHECK_EQ(g.input_pin_count(out), 1);
    CHECK_EQ(g.input_pin_count(src1), 0);
}

TEST(audio_graph_compile_topological_order) {
    AudioGraph g;
    g.clear();
    const int a = g.add_node("wav_player", 0, 0);
    const int b = g.add_node("preamp", 0, 0);
    const int c = g.add_node("output", 0, 0);
    CHECK(g.add_link(a, b));
    CHECK(g.add_link(b, c));
    auto compiled = g.compile();
    CHECK(compiled);
    CHECK(compiled->valid);
    CHECK_EQ(static_cast<int>(compiled->order.size()), 3);
    CHECK(compiled->order.front().module->name() == "wav_player");
    CHECK(compiled->order.back().module->name() == "output");
}

TEST(audio_graph_default_patch_has_mixer_and_output) {
    AudioGraph g;
    std::string error;
    CHECK(graph_io::load_from_file(g, kDefaultPatchPath, error));
    bool has_mixer = false;
    bool has_output = false;
    bool has_wav = false;
    for (const auto& n : g.nodes()) {
        has_mixer = has_mixer || n.module->name() == "mixer";
        has_output = has_output || n.module->name() == "output";
        has_wav = has_wav || n.module->name() == "wav_player";
    }
    CHECK(has_mixer);
    CHECK(has_output);
    CHECK(has_wav);
    auto compiled = g.compile();
    CHECK(compiled);
    CHECK(compiled->valid);
}

TEST(audio_graph_frees_deleted_modules_on_publishing_thread) {
    AudioGraph g;
    const int pre = g.add_node("preamp", 0, 0);
    std::weak_ptr<Module> module = g.find_node(pre)->module;
    g.publish(g.compile());
    auto audio_snapshot = g.latest(); // the audio thread is mid-block on this snapshot

    g.remove_node(pre);
    g.publish(g.compile());
    CHECK(!module.expired());

    audio_snapshot.reset(); // the audio thread moves on; it must not run the destructor
    CHECK(!module.expired());

    g.publish(g.compile()); // the next UI-thread publish frees it
    CHECK(module.expired());
}
