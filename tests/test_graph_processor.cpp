#include "test_harness.hpp"

#include "graph/audio_graph.hpp"
#include "graph/graph_processor.hpp"
#include "graph/node_state.hpp"

#include <vector>

static std::shared_ptr<const CompiledGraph> compile_chain(AudioGraph& g) {
    auto compiled = g.compile();
    CHECK(compiled);
    CHECK(compiled->valid);
    return compiled;
}

TEST(graph_processor_preamp_applies_gain) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node(NodeType::audio_input, 0, 0);
    const int pre = g.add_node(NodeType::preamp, 0, 0);
    const int out = g.add_node(NodeType::output, 0, 0);
    g.find_node(pre)->state().as<node_params::Preamp>()->gain_db = 6.0f; // ~2x
    CHECK(g.add_link(in, pre));
    CHECK(g.add_link(pre, out));

    constexpr size_t block = 64; // 32 stereo frames
    std::vector<int32_t> live(block, 1073741824); // 0.5 in S32 scale-ish (/2^31)
    // 1073741824 / 2147483648 = 0.5
    std::vector<float> output(block, 0.0f);

    GraphProcessor proc(48000.0f);
    proc.set_block_size(block);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));

    // 0.5 * 10^(6/20) ≈ 0.5 * 2 = 1.0 (near clip)
    CHECK_NEAR(output[0], 0.998f, 0.05f);
}

TEST(graph_processor_sees_ui_edits_without_recompile) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node(NodeType::audio_input, 0, 0);
    const int pre = g.add_node(NodeType::preamp, 0, 0);
    const int out = g.add_node(NodeType::output, 0, 0);
    CHECK(g.add_link(in, pre));
    CHECK(g.add_link(pre, out));

    constexpr size_t block = 64;
    std::vector<int32_t> live(block, 1073741824); // 0.5 float
    std::vector<float> output(block, 0.0f);

    GraphProcessor proc(48000.0f);
    proc.set_block_size(block);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    CHECK_NEAR(output[0], 0.5f, 0.02f);

    // Same snapshot: the processor runs the module the UI edits.
    g.find_node(pre)->state().as<node_params::Preamp>()->gain_db = 6.0f;
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    CHECK_NEAR(output[0], 0.998f, 0.05f);
}

TEST(graph_processor_bypass_skips_preamp_gain) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node(NodeType::audio_input, 0, 0);
    const int pre = g.add_node(NodeType::preamp, 0, 0);
    const int out = g.add_node(NodeType::output, 0, 0);
    g.find_node(pre)->state().as<node_params::Preamp>()->gain_db = 6.0f;
    g.find_node(pre)->state().bypassed = true;
    CHECK(g.add_link(in, pre));
    CHECK(g.add_link(pre, out));

    constexpr size_t block = 64;
    std::vector<int32_t> live(block, 1073741824); // 0.5 float
    std::vector<float> output(block, 0.0f);

    GraphProcessor proc(48000.0f);
    proc.set_block_size(block);
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
    const int in = g.add_node(NodeType::audio_input, 0, 0);
    const int pre_a = g.add_node(NodeType::preamp, 0, 0);
    const int pre_b = g.add_node(NodeType::preamp, 0, 0);
    const int mix = g.add_node(NodeType::mixer, 0, 0);
    const int out = g.add_node(NodeType::output, 0, 0);
    g.find_node(pre_a)->state().as<node_params::Preamp>()->gain_db = 0.0f;  // x1
    g.find_node(pre_b)->state().as<node_params::Preamp>()->gain_db = 6.0f;  // ~x2
    g.find_node(mix)->state().input_gain[0] = 0.5f;
    g.find_node(mix)->state().input_gain[1] = 1.5f;
    CHECK(g.add_link(in, pre_a));
    CHECK(g.add_link(in, pre_b));
    CHECK(g.add_link(pre_a, mix, 0));
    CHECK(g.add_link(pre_b, mix, 1));
    CHECK(g.add_link(mix, out));

    constexpr size_t block = 32;
    // live sample corresponding to 0.25 float
    const int32_t pcm = static_cast<int32_t>(0.25 * 2147483648.0);
    std::vector<int32_t> live(block, pcm);
    std::vector<float> output(block, 0.0f);

    GraphProcessor proc(48000.0f);
    proc.set_block_size(block);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));

    // average(0.25 * 0.5, ~0.5 * 1.5) ≈ 0.4375
    CHECK_NEAR(output[0], 0.4375f, 0.04f);
}

TEST(graph_processor_rejects_mismatched_empty_graph) {
    AudioGraph g;
    g.clear();
    GraphProcessor proc(48000.0f);
    proc.set_block_size(16);
    auto compiled = g.compile();
    // empty graph compiles valid with 0 nodes? clear then compile - order empty
    std::vector<int32_t> live(16, 0);
    std::vector<float> out(16, 1.0f);
    CHECK(compiled);
    // no nodes -> process returns false and zeros output
    const bool produced = proc.process(*compiled, live.data(), out.data(), 16);
    CHECK(!produced);
    CHECK_NEAR(out[0], 0.0f, 1e-6f);
}

TEST(graph_processor_block_size_contract) {
    AudioGraph g;
    g.reset_to_default_chain();
    // Replace wav with audio input for deterministic PCM
    // Just process default — may not have audio_input; use custom mini graph.
    g.clear();
    const int in = g.add_node(NodeType::audio_input, 0, 0);
    const int out = g.add_node(NodeType::output, 0, 0);
    CHECK(g.add_link(in, out));

    GraphProcessor proc(48000.0f);
    constexpr size_t block = 48;
    proc.set_block_size(block);
    CHECK_EQ(static_cast<int>(proc.block_size()), static_cast<int>(block));

    std::vector<int32_t> live(block, 0);
    std::vector<float> output(block, 0.0f);
    auto compiled = compile_chain(g);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
}
