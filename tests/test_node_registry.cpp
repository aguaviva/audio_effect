#include "test_harness.hpp"

#include "dsp/chord_catalog.hpp"
#include "dsp/chord_generator.hpp"
#include "dsp/dynamic_high_pass_filter.hpp"
#include "dsp/dynamic_low_pass_filter.hpp"
#include "dsp/echo_effect.hpp"
#include "dsp/preamp.hpp"
#include "dsp/wav_player.hpp"
#include "graph/audio_graph.hpp"
#include "graph/node_registry.hpp"
#include "paths.hpp"

TEST(registry_names_are_unique_and_constructible) {
    const auto& kinds = node_kinds();
    for (size_t i = 0; i < kinds.size(); ++i) {
        const std::string name = kinds[i].make()->name();
        auto module = make_module(name);
        CHECK(module != nullptr);
        CHECK_STREQ(node_label(*module), kinds[i].label);
        for (size_t j = i + 1; j < kinds.size(); ++j) {
            CHECK(kinds[j].make()->name() != name);
        }
    }
}

TEST(module_defaults_and_public_fields) {
    AudioGraph g;
    auto* pre = g.find_node(g.add_node("preamp", 0, 0))->as<Preamp>();
    CHECK(pre != nullptr);
    CHECK_NEAR(pre->gain_db, 0.0f, 1e-6f);

    auto* echo = g.find_node(g.add_node("echo", 0, 0))->as<EchoEffect>();
    CHECK(echo != nullptr);
    CHECK_NEAR(echo->delay_ms, 320.0f, 1e-6f);

    auto* wav = g.find_node(g.add_node("wav_player", 0, 0))->as<WavPlayer>();
    CHECK(wav != nullptr);
    CHECK(wav->loop);
    CHECK(wav->path() == kDefaultWavPath);

    CHECK(!g.nodes().front().bypassed);
}

TEST(module_names_and_json_roundtrip) {
    CHECK(Preamp{}.name() == "preamp");
    CHECK(EchoEffect{}.name() == "echo");
    CHECK(DynamicHighPassFilter{}.name() == "high_pass");
    CHECK(DynamicLowPassFilter{}.name() == "low_pass");

    Preamp pre;
    pre.gain_db = 4.25f;
    const std::string json = pre.save();
    CHECK(json.find("\"gain_db\"") != std::string::npos);

    Preamp loaded;
    CHECK(loaded.load(json));
    CHECK_NEAR(loaded.gain_db, 4.25f, 1e-4f);
}

TEST(chord_index_clamped_on_load) {
    ChordGenerator chord;
    CHECK(chord.load(R"({"chord_index":-5})"));
    CHECK_EQ(chord.index, 0);
    CHECK(chord.load(R"({"chord_index":999})"));
    CHECK_EQ(chord.index, kChordCount - 1);
}
