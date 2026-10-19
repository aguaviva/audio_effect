#include "test_harness.hpp"

#include "dsp/eq_three_band.hpp"
#include "dsp/mixer.hpp"
#include "dsp/noise_gate.hpp"
#include "dsp/simple_reverb.hpp"
#include "dsp/wav_recorder.hpp"
#include "graph/audio_graph.hpp"
#include "graph/graph_processor.hpp"
#include "graph/node_state.hpp"
#include "graph/node_types.hpp"
#include "paths.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

TEST(new_node_types_parse_and_labels) {
    NodeType t = NodeType::output;
    CHECK(try_parse_node_type("noise_gate", t));
    CHECK(t == NodeType::noise_gate);
    CHECK(try_parse_node_type("eq_three_band", t));
    CHECK(try_parse_node_type("reverb", t));
    CHECK(try_parse_node_type("dry_wet_mix", t));
    CHECK(try_parse_node_type("wav_recorder", t));
    CHECK(try_parse_node_type("white_noise", t));
    CHECK(t == NodeType::white_noise);
    CHECK(node_is_source(NodeType::white_noise));
    CHECK(!node_has_input_pin(NodeType::white_noise));
    CHECK(node_is_dry_wet(NodeType::dry_wet_mix));
    CHECK_EQ(node_input_slot_limit(NodeType::dry_wet_mix), 2);
}

TEST(white_noise_source_produces_energy) {
    AudioGraph g;
    g.clear();
    const int noise = g.add_node(NodeType::white_noise, 0, 0);
    const int out = g.add_node(NodeType::output, 0, 0);
    g.find_node(noise)->state().as<node_params::WhiteNoise>()->level = 0.5f;
    CHECK(g.add_link(noise, out));

    constexpr size_t block = 256;
    std::vector<int32_t> live(block, 0);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    proc.set_block_size(block);
    auto compiled = g.compile();
    CHECK(compiled && compiled->valid);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));

    float energy = 0.0f;
    for (float s : output) {
        energy += s * s;
    }
    CHECK(energy > 1.0e-3f);
}

TEST(mixer_dsp_averages_sources) {
    Mixer mixer;
    CHECK(mixer.name() == "mixer");
    std::vector<float> a(8, 0.2f);
    std::vector<float> b(8, 0.6f);
    std::vector<float> out(8, -1.0f);
    const float* sources[] = {a.data(), b.data()};
    ProcessContext ctx;
    ctx.buffer = out.data();
    ctx.num_samples = out.size();
    ctx.inputs = sources;
    ctx.input_count = 2;
    mixer.process(ctx);
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
    ProcessContext ctx;
    ctx.buffer = out.data();
    ctx.num_samples = out.size();
    ctx.inputs = sources;
    ctx.input_count = 2;
    mixer.process(ctx);
    // average of (1*1 + 1*0) / 2 = 0.5
    CHECK_NEAR(out[0], 0.5f, 1e-5f);
}

TEST(noise_gate_silences_below_threshold) {
    NoiseGate gate;
    std::vector<float> quiet(64, 0.0001f);
    gate.threshold_db = -20.0f;
    gate.attack_ms = 1.0f;
    gate.release_ms = 1.0f;
    gate.process(quiet.data(), quiet.size(), 48000.0f);
    float max_abs = 0.0f;
    for (float s : quiet) {
        max_abs = std::max(max_abs, std::fabs(s));
    }
    CHECK(max_abs < 0.0002f);
}

TEST(eq_three_band_runs_in_place) {
    EqThreeBand eq;
    std::vector<float> block(128, 0.25f);
    eq.low_db = 3.0f;
    eq.mid_db = -3.0f;
    eq.high_db = 2.0f;
    eq.process(block.data(), block.size(), 48000.0f);
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
    rev.process(block.data(), block.size(), 48000.0f);
    float energy = 0.0f;
    for (size_t i = 512; i < block.size(); ++i) {
        energy += block[i] * block[i];
    }
    CHECK(energy > 1.0e-6f);
}

TEST(dry_wet_mix_blends_slots) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node(NodeType::audio_input, 0, 0);
    const int pre_a = g.add_node(NodeType::preamp, 0, 0);
    const int pre_b = g.add_node(NodeType::preamp, 0, 0);
    const int mix = g.add_node(NodeType::dry_wet_mix, 0, 0);
    const int out = g.add_node(NodeType::output, 0, 0);
    g.find_node(pre_a)->state().as<node_params::Preamp>()->gain_db = 0.0f;
    g.find_node(pre_b)->state().as<node_params::Preamp>()->gain_db = 6.0f;
    g.find_node(mix)->state().as<node_params::DryWet>()->mix = 0.5f;
    CHECK(g.add_link(in, pre_a));
    CHECK(g.add_link(in, pre_b));
    CHECK(g.add_link(pre_a, mix, 0));
    CHECK(g.add_link(pre_b, mix, 1));
    CHECK(g.add_link(mix, out));

    constexpr size_t block = 32;
    const int32_t pcm = static_cast<int32_t>(0.25 * 2147483648.0);
    std::vector<int32_t> live(block, pcm);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    proc.set_block_size(block);
    auto compiled = g.compile();
    CHECK(compiled && compiled->valid);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    // 0.5 * (0.25 + ~0.5) ≈ 0.375
    CHECK_NEAR(output[0], 0.375f, 0.04f);
}

TEST(wav_recorder_writes_file) {
    const char* path = "tests/_tmp_record.wav";
    std::remove(path);
    WavRecorderEngine rec;
    rec.set_path(path);
    rec.set_armed(true, 48000.0f);
    std::vector<float> block(64, 0.1f);
    rec.write_samples(block.data(), block.size());
    rec.set_armed(false, 48000.0f);
    FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    if (f) {
        char riff[4];
        CHECK_EQ(static_cast<int>(std::fread(riff, 1, 4, f)), 4);
        CHECK(riff[0] == 'R' && riff[1] == 'I' && riff[2] == 'F' && riff[3] == 'F');
        std::fclose(f);
    }
    std::remove(path);
    CHECK_STREQ(kDefaultRecordPath, "assets/recordings/capture.wav");
}
