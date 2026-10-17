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

TEST(node_state_bypass_defaults_off) {
    NodeState s = NodeState::make(NodeType::preamp);
    CHECK(!s.bypassed);
    s.bypassed = true;
    CHECK(s.bypassed);
}

TEST(node_state_mixer_input_gain_roundtrip) {
    NodeState state = NodeState::make(NodeType::mixer);
    CHECK_NEAR(state.input_gain[0], 1.0f, 1e-6f);
    state.input_gain[1] = 1.5f;

    const std::string json = state.write_params();
    CHECK(json.find("\"input_gain\"") != std::string::npos);

    NodeState loaded;
    size_t i = 0;
    CHECK(loaded.read_params(NodeType::mixer, json, i));
    CHECK_NEAR(loaded.input_gain[0], 1.0f, 1e-6f);
    CHECK_NEAR(loaded.input_gain[1], 1.5f, 1e-6f);

    const std::string old_patch = R"({"muted":[false,true]})";
    i = 0;
    CHECK(loaded.read_params(NodeType::mixer, old_patch, i));
    CHECK_NEAR(loaded.input_gain[0], 1.0f, 1e-6f);
    CHECK_NEAR(loaded.input_gain[1], 1.0f, 1e-6f);
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

TEST(node_state_legacy_arrays) {
    float f[8] = {12.0f, 0, 0, 0, 0, 0, 0, 0};
    int ii[4] = {0, 0, 0, 0};
    bool b[4] = {false, false, false, true};
    NodeState s;
    s.from_legacy_arrays(NodeType::preamp, f, ii, b);
    CHECK(s.bypassed);
    CHECK_NEAR(s.as<node_params::Preamp>()->gain_db, 12.0f, 1e-6f);

    const std::string legacy = R"({"f":[6,0,0,0,0,0,0,0],"i":[0,0,0,0],"b":[false,false,false,false]})";
    size_t idx = 0;
    NodeState loaded;
    CHECK(loaded.read_params(NodeType::preamp, legacy, idx));
    CHECK_NEAR(loaded.as<node_params::Preamp>()->gain_db, 6.0f, 1e-6f);
    CHECK(!loaded.bypassed);
}
