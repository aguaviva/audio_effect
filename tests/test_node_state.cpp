#include "test_harness.hpp"

#include "dsp/chord_catalog.hpp"
#include "graph/node_state.hpp"
#include "graph/node_types.hpp"

TEST(node_state_defaults_and_public_fields) {
    NodeState pre = NodeState::make(NodeType::preamp);
    auto* pg = pre.as<node_params::Preamp>();
    CHECK(pg != nullptr);
    CHECK_NEAR(pg->gain_db, 0.0f, 1e-6f);
    pg->gain_db = 6.5f;
    CHECK_NEAR(pg->gain_db, 6.5f, 1e-6f);

    NodeState echo = NodeState::make(NodeType::echo);
    auto* pe = echo.as<node_params::Echo>();
    CHECK(pe != nullptr);
    CHECK_NEAR(pe->delay_ms, 320.0f, 1e-6f);
    pe->mix = 0.5f;
    CHECK_NEAR(pe->mix, 0.5f, 1e-6f);

    NodeState wav = NodeState::make(NodeType::wav_player);
    auto* pw = wav.as<node_params::WavPlayer>();
    CHECK(pw != nullptr);
    CHECK(pw->loop);
    pw->loop = false;
    CHECK(!pw->loop);
}

TEST(node_state_serialize_roundtrip) {
    NodeState s = NodeState::make(NodeType::preamp);
    s.bypassed = true;
    s.as<node_params::Preamp>()->gain_db = 3.5f;

    const std::string json = s.write_params();
    CHECK(json.find("\"gain_db\"") != std::string::npos);
    CHECK(json.find("\"bypassed\":true") != std::string::npos);

    NodeState loaded;
    size_t i = 0;
    CHECK(loaded.read_params(NodeType::preamp, json, i));
    CHECK(loaded.bypassed);
    CHECK_NEAR(loaded.as<node_params::Preamp>()->gain_db, 3.5f, 1e-4f);
}

TEST(module_name_and_json_load_by_name) {
    CHECK(::Preamp{}.name() == "preamp");
    CHECK(EchoEffect{}.name() == "echo");
    CHECK(DynamicHighPassFilter{}.name() == "high_pass");
    CHECK(DynamicLowPassFilter{}.name() == "low_pass");

    node_params::Preamp pre;
    pre.gain_db = 4.25f;
    const std::string fields_json = pre.save();
    CHECK(fields_json.find("\"gain_db\"") != std::string::npos);

    node_params::Preamp loaded_mod;
    CHECK(loaded_mod.load(fields_json));
    CHECK_NEAR(loaded_mod.gain_db, 4.25f, 1e-4f);

    NodeType resolved = NodeType::count;
    CHECK(node_params::try_resolve_name("preamp", resolved));
    CHECK(resolved == NodeType::preamp);
    CHECK(!node_params::try_resolve_name("not_a_module", resolved));

    NodeState s = NodeState::make(NodeType::echo);
    s.as<node_params::Echo>()->delay_ms = 100.0f;
    const std::string params = s.write_params();
    CHECK(s.module_name() == "echo");

    NodeState by_name;
    size_t i = 0;
    CHECK(by_name.load_by_module_name("echo", params, i));
    CHECK(by_name.module_name() == "echo");
    CHECK_NEAR(by_name.as<node_params::Echo>()->delay_ms, 100.0f, 1e-4f);

    i = 0;
    CHECK(!by_name.load_by_module_name("nope", params, i));
}

TEST(node_state_bypass_defaults_off) {
    NodeState s = NodeState::make(NodeType::preamp);
    CHECK(!s.bypassed);
    s.bypassed = true;
    CHECK(s.bypassed);
}

TEST(node_state_chord_index_clamped_on_read) {
    NodeState s;
    const std::string low = R"({"chord_index":-5})";
    size_t i = 0;
    CHECK(s.read_params(NodeType::chord_generator, low, i));
    CHECK_EQ(s.as<node_params::Chord>()->index, 0);

    const std::string high = R"({"chord_index":999})";
    i = 0;
    CHECK(s.read_params(NodeType::chord_generator, high, i));
    CHECK_EQ(s.as<node_params::Chord>()->index, kChordCount - 1);
}
