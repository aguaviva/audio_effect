#include "test_harness.hpp"

#include "graph/audio_graph.hpp"
#include "dsp/audio_output.hpp"
#include "dsp/mixer.hpp"
#include "dsp/preamp.hpp"
#include "graph/graph_processor.hpp"

#include <algorithm>
#include <vector>

static std::shared_ptr<const CompiledGraph> compile_chain(AudioGraph& g) {
    auto compiled = g.compile();
    CHECK(compiled);
    return compiled;
}

TEST(graph_processor_preamp_applies_gain) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int pre = g.add_node("preamp", 0, 0);
    const int out = g.add_node("output", 0, 0);
    g.find_node(pre)->as<Preamp>()->gain_db = 6.0f; // ~2x
    CHECK(g.add_link(in, 0, pre, 0));
    CHECK(g.add_link(pre, 0, out, 0));

    constexpr size_t block = 64; // 32 stereo frames
    std::vector<float> live(block, 0.5f);
    std::vector<float> output(block, 0.0f);

    GraphProcessor proc(48000.0f);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));

    // 0.5 * 10^(6/20) ≈ 0.5 * 2 = 1.0 (near clip)
    CHECK_NEAR(output[0], 0.998f, 0.05f);
}

TEST(graph_processor_meters_every_output_pin) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int pre = g.add_node("preamp", 0, 0);
    const int unlinked = g.add_node("preamp", 0, 0);
    const int out = g.add_node("output", 0, 0);
    g.find_node(pre)->as<Preamp>()->gain_db = 6.0f;
    CHECK(g.add_link(in, 0, pre, 0));
    CHECK(g.add_link(in, 0, unlinked, 0));
    CHECK(g.add_link(pre, 0, out, 0));

    constexpr size_t block = 64;
    std::vector<float> live(block, -0.5f);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    proc.process(*compile_chain(g), live.data(), output.data(), block);
    CHECK_NEAR(g.find_node(in)->module->output_level(0), 0.5f, 1e-6f);
    CHECK_NEAR(g.find_node(pre)->module->output_level(0), 0.998f, 0.05f);
    // Nothing is linked to its output, so it outputs nothing.
    CHECK_EQ(g.find_node(unlinked)->module->output_level(0), 0.0f);

    std::fill(live.begin(), live.end(), 0.0f);
    proc.process(*compile_chain(g), live.data(), output.data(), block);
    const float held = g.find_node(in)->module->output_level(0);
    CHECK(held < 0.5f && held > 0.45f); // falls slowly, so short sounds stay visible
}

TEST(graph_processor_sees_ui_edits_without_recompile) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int pre = g.add_node("preamp", 0, 0);
    const int out = g.add_node("output", 0, 0);
    CHECK(g.add_link(in, 0, pre, 0));
    CHECK(g.add_link(pre, 0, out, 0));

    constexpr size_t block = 64;
    std::vector<float> live(block, 0.5f);
    std::vector<float> output(block, 0.0f);

    GraphProcessor proc(48000.0f);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    CHECK_NEAR(output[0], 0.5f, 0.02f);

    // Same snapshot: the processor runs the module the UI edits.
    g.find_node(pre)->as<Preamp>()->gain_db = 6.0f;
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    CHECK_NEAR(output[0], 0.998f, 0.05f);
}

TEST(graph_processor_bypass_skips_preamp_gain) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int pre = g.add_node("preamp", 0, 0);
    const int out = g.add_node("output", 0, 0);
    g.find_node(pre)->as<Preamp>()->gain_db = 6.0f;
    g.find_node(pre)->bypassed = true;
    CHECK(g.add_link(in, 0, pre, 0));
    CHECK(g.add_link(pre, 0, out, 0));

    constexpr size_t block = 64;
    std::vector<float> live(block, 0.5f);
    std::vector<float> output(block, 0.0f);

    GraphProcessor proc(48000.0f);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));

    // Bypassed preamp must pass the dry 0.5 level through.
    CHECK_NEAR(output[0], 0.5f, 0.02f);
}

TEST(graph_processor_mixer_applies_per_input_gain) {
    AudioGraph g;
    g.clear();
    // Use two audio_input nodes — both read the same live PCM; we can't feed
    // different PCM per source easily, so build: input -> preamp(+6dB) and
    // input -> preamp(0dB) via... only one audio_input reads live buffer.
    // Instead: wav-less sources won't work. Use one audio_input split to two
    // preamps with different gains, then mix.
    const int in = g.add_node("audio_input", 0, 0);
    const int pre_a = g.add_node("preamp", 0, 0);
    const int pre_b = g.add_node("preamp", 0, 0);
    const int mix = g.add_node("mixer", 0, 0);
    const int out = g.add_node("output", 0, 0);
    g.find_node(pre_a)->as<Preamp>()->gain_db = 0.0f;  // x1
    g.find_node(pre_b)->as<Preamp>()->gain_db = 6.0f;  // ~x2
    g.find_node(mix)->as<Mixer>()->gain[0] = 0.5f;
    g.find_node(mix)->as<Mixer>()->gain[1] = 1.5f;
    CHECK(g.add_link(in, 0, pre_a, 0));
    CHECK(g.add_link(in, 0, pre_b, 0));
    CHECK(g.add_link(pre_a, 0, mix, 0));
    CHECK(g.add_link(pre_b, 0, mix, 1));
    CHECK(g.add_link(mix, 0, out, 0));

    constexpr size_t block = 32;
    std::vector<float> live(block, 0.25f);
    std::vector<float> output(block, 0.0f);

    GraphProcessor proc(48000.0f);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));

    // average(0.25 * 0.5, ~0.5 * 1.5) ≈ 0.4375
    CHECK_NEAR(output[0], 0.4375f, 0.04f);
}

TEST(graph_processor_rejects_mismatched_empty_graph) {
    AudioGraph g;
    g.clear();
    GraphProcessor proc(48000.0f);
    auto compiled = g.compile();
    // empty graph compiles valid with 0 nodes? clear then compile - order empty
    std::vector<float> live(16, 0.0f);
    std::vector<float> out(16, 1.0f);
    CHECK(compiled);
    // no nodes -> process returns false and zeros output
    const bool produced = proc.process(*compiled, live.data(), out.data(), 16);
    CHECK(!produced);
    CHECK_NEAR(out[0], 0.0f, 1e-6f);
}

TEST(graph_processor_block_size_contract) {
    AudioGraph g;
    const int in = g.add_node("audio_input", 0, 0);
    const int out = g.add_node("output", 0, 0);
    CHECK(g.add_link(in, 0, out, 0));

    GraphProcessor proc(48000.0f);
    constexpr size_t block = 48;

    std::vector<float> live(block, 0.0f);
    std::vector<float> output(block, 0.0f);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
}

TEST(graph_processor_output_node_ignores_bypass_flag) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int out = g.add_node("output", 0, 0);
    CHECK(g.add_link(in, 0, out, 0));
    g.find_node(out)->bypassed = true;
    CHECK(!g.find_node(out)->module->supports_bypass());

    constexpr size_t block = 16;
    std::vector<float> live(block, 0.5f);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    CHECK_NEAR(output[0], 0.5f, 1e-5f);
}

TEST(graph_processor_reports_no_output_without_output_node) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int pre = g.add_node("preamp", 0, 0);
    CHECK(g.add_link(in, 0, pre, 0));

    constexpr size_t block = 16;
    std::vector<float> live(block, 0.5f);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    auto compiled = g.compile();
    CHECK(compiled);
    // The pipeline turns this into silence rather than passing the raw input through.
    CHECK(!proc.process(*compiled, live.data(), output.data(), block));
}

TEST(graph_processor_fan_out_feeds_every_link) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int pre = g.add_node("preamp", 0, 0);
    const int out_a = g.add_node("output", 0, 0);
    const int out_b = g.add_node("output", 0, 0);
    CHECK(g.add_link(in, 0, pre, 0));
    CHECK(g.add_link(pre, 0, out_a, 0));
    CHECK(g.add_link(pre, 0, out_b, 0));

    constexpr size_t block = 16;
    std::vector<float> live(block, 0.25f);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    // Both Output nodes receive the preamp's block and are summed into the device.
    CHECK_NEAR(output[0], 0.5f, 1e-5f);
}

TEST(graph_processor_output_volume_and_mute) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int out = g.add_node("output", 0, 0);
    CHECK(g.add_link(in, 0, out, 0));
    auto* m = g.find_node(out)->as<AudioOutput>();
    m->volume_db = -6.0206f; // x0.5

    constexpr size_t block = 16;
    std::vector<float> live(block, 0.5f);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    auto compiled = compile_chain(g);
    proc.process(*compiled, live.data(), output.data(), block); // ramps 1 -> 0.5
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    CHECK_NEAR(output[0], 0.25f, 1e-4f);

    m->muted = true;
    proc.process(*compiled, live.data(), output.data(), block); // ramps to 0
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    CHECK_NEAR(output[0], 0.0f, 1e-6f);
    CHECK_NEAR(output[block - 1], 0.0f, 1e-6f);
}

TEST(output_mute_and_volume_persist) {
    AudioOutput out;
    out.muted = true;
    out.volume_db = -12.0f;
    AudioOutput loaded;
    CHECK(loaded.load(out.save()));
    CHECK(loaded.muted);
    CHECK_NEAR(loaded.volume_db, -12.0f, 1e-5f);
    CHECK(loaded.load(R"({"volume_db":99})"));
    CHECK_NEAR(loaded.volume_db, AudioOutput::kMaxVolumeDb, 1e-5f);
}
