#include "test_harness.hpp"

#include "dsp/agc.hpp"
#include "dsp/denormals.hpp"
#include "dsp/dry_wet_mix.hpp"
#include "dsp/eq_three_band.hpp"
#include "dsp/guitar_pedals.hpp"
#include "dsp/mixer.hpp"
#include "dsp/noise_gate.hpp"
#include "dsp/preamp.hpp"
#include "dsp/simple_reverb.hpp"
#include "dsp/timeline.hpp"
#include "dsp/wav_player.hpp"
#include "dsp/wav_recorder.hpp"
#include "dsp/white_noise.hpp"
#include "graph/audio_graph.hpp"
#include "graph/graph_processor.hpp"
#include "graph/node_registry.hpp"
#include "paths.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

// Runs a single-input module on block, feeding it a copy of block as its input link.
static void run_effect(Module& module, std::vector<float>& block) {
    const std::vector<float> in = block;
    const float* inputs[] = {in.data()};
    module.set_output_buffer(0, block.data());
    module.process(inputs, 1, block.size(), 48000.0f);
}

TEST(registry_resolves_module_names_and_pins) {
    for (const char* name : {"noise_gate", "eq_three_band", "reverb", "dry_wet_mix", "wav_recorder",
                             "white_noise", "agc", "pitch_detector"}) {
        auto module = make_module(name);
        CHECK(module != nullptr);
        CHECK(module->name() == name);
    }
    CHECK(make_module("not_a_module") == nullptr);
    CHECK_EQ(make_module("white_noise")->input_count(), 0);
    CHECK_EQ(make_module("dry_wet_mix")->input_count(), 2);
    CHECK_EQ(make_module("output")->output_count(), 0);
    for (const char* name : {"vu_meter", "oscilloscope", "spectrum", "pitch_detector"}) {
        CHECK_EQ(make_module(name)->output_count(), 0);
    }
    CHECK_STREQ(node_label(*make_module("pitch_detector")), "Pitch Detector");
}

TEST(white_noise_source_produces_energy) {
    AudioGraph g;
    g.clear();
    const int noise = g.add_node("white_noise", 0, 0);
    const int out = g.add_node("output", 0, 0);
    g.find_node(noise)->as<WhiteNoise>()->level = 0.5f;
    CHECK(g.add_link(noise, 0, out, 0));

    constexpr size_t block = 256;
    std::vector<float> live(block, 0.0f);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    auto compiled = g.compile();
    CHECK(compiled);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));

    float energy = 0.0f;
    for (float s : output) {
        energy += s * s;
    }
    CHECK(energy > 1.0e-3f);
}

TEST(wav_player_plays_its_file_and_silence_when_missing) {
    AudioGraph g;
    const int wav = g.add_node("wav_player", 0, 0);
    const int out = g.add_node("output", 0, 0);
    CHECK(g.add_link(wav, 0, out, 0));
    auto* player = g.find_node(wav)->as<WavPlayer>();
    auto compiled = g.compile();
    GraphProcessor proc(48000.0f);

    auto energy_of_one_second = [&] {
        constexpr size_t block = 4800;
        std::vector<float> output(block, 0.0f);
        float energy = 0.0f;
        for (int n = 0; n < 20; ++n) {
            proc.process(*compiled, nullptr, output.data(), block);
            for (float s : output) {
                energy += s * s;
            }
        }
        return energy;
    };
    CHECK(energy_of_one_second() > 1.0e-3f);

    CHECK(player->error().empty());
    CHECK(!player->set_path("assets/audio/does_not_exist.wav"));
    CHECK(!player->error().empty());
    CHECK_NEAR(energy_of_one_second(), 0.0f, 1e-9f);
}

TEST(paused_generator_is_silent_and_resumes_where_it_stopped) {
    WavPlayer paused;
    WavPlayer reference;
    CHECK(paused.load("{}"));
    CHECK(reference.load("{}"));
    std::vector<float> a(48000, 0.0f);
    std::vector<float> b(48000, 0.0f);
    paused.set_output_buffer(0, a.data());
    reference.set_output_buffer(0, b.data());
    paused.process(nullptr, 0, a.size(), 48000.0f);
    reference.process(nullptr, 0, b.size(), 48000.0f);

    paused.playing = false;
    paused.process(nullptr, 0, a.size(), 48000.0f);
    for (float s : a) {
        CHECK_EQ(s, 0.0f);
    }

    paused.playing = true;
    paused.process(nullptr, 0, a.size(), 48000.0f);
    reference.process(nullptr, 0, b.size(), 48000.0f);
    CHECK(a == b);
    float energy = 0.0f;
    for (float s : b) {
        energy += s * s;
    }
    CHECK(energy > 1.0e-3f);
}

TEST(waveform_peaks_match_a_direct_scan) {
    WavClip clip;
    for (int i = 0; i < 10000; ++i) {
        const float v = std::sin(i * 0.013f) * (i % 7 == 0 ? 1.0f : 0.5f);
        clip.interleaved_stereo.push_back(v);
        clip.interleaved_stereo.push_back(-0.25f * v);
    }
    const WaveformPeaks peaks(clip);
    CHECK_EQ(peaks.levels.back().size(), static_cast<size_t>(1));
    for (size_t begin : {0u, 10u, 640u, 3000u}) {
        for (size_t len : {5u, 64u, 300u, 4096u}) {
            const size_t end = std::min<size_t>(begin + len, 5000);
            // Whole buckets are read, so the envelope covers at least the exact range.
            const auto [lo, hi] = peaks.range(clip, begin, end);
            const auto first = clip.interleaved_stereo.begin();
            auto [elo, ehi] = std::minmax_element(first + 2 * begin, first + 2 * end);
            CHECK(lo <= *elo && hi >= *ehi);
        }
    }
}

TEST(timeline_plays_a_clip_from_where_it_is_placed) {
    Timeline timeline;
    CHECK(timeline.load(std::string("{\"track3_path\":\"") + kDefaultWavPath +
                        "\",\"track3_start\":1000}"));
    std::string error;
    auto clip = WavGenerator::load(kDefaultWavPath, kSampleRate, error);
    CHECK(clip != nullptr);
    timeline.playing = true;
    // Only pins 2 and 3 are linked: track 2 is empty, track 3 plays on its own pin.
    constexpr size_t kBlock = 4096;
    std::vector<float> track2(kBlock, 1.0f);
    std::vector<float> track3(kBlock, 1.0f);
    timeline.set_output_buffer(1, track2.data());
    timeline.set_output_buffer(2, track3.data());
    timeline.process(nullptr, 0, kBlock, 48000.0f);
    for (size_t i = 0; i < kBlock; ++i) {
        CHECK_EQ(track2[i], 0.0f);
    }
    for (size_t i = 0; i < 2000; ++i) {
        CHECK_EQ(track3[i], 0.0f);
    }
    for (size_t i = 2000; i < kBlock; ++i) {
        CHECK_EQ(track3[i], clip->interleaved_stereo[i - 2000]);
    }
}

TEST(mixer_dsp_averages_sources) {
    Mixer mixer;
    CHECK(mixer.name() == "mixer");
    std::vector<float> a(8, 0.2f);
    std::vector<float> b(8, 0.6f);
    std::vector<float> out(8, -1.0f);
    const float* sources[] = {a.data(), b.data()};
    mixer.set_output_buffer(0, out.data());
    mixer.process(sources, 2, out.size(), 48000.0f);
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
    mixer.set_output_buffer(0, out.data());
    mixer.process(sources, 2, out.size(), 48000.0f);
    // average of (1*1 + 1*0) / 2 = 0.5
    CHECK_NEAR(out[0], 0.5f, 1e-5f);
}

TEST(mixer_mute_skips_input_and_persists) {
    Mixer mixer;
    mixer.input_muted[1] = true;
    Mixer loaded;
    CHECK(loaded.load(mixer.save()));
    CHECK(!loaded.input_muted[0]);
    CHECK(loaded.input_muted[1]);

    std::vector<float> a(4, 0.2f);
    std::vector<float> b(4, 0.6f);
    std::vector<float> out(4, 0.0f);
    const float* sources[] = {a.data(), b.data()};
    loaded.set_output_buffer(0, out.data());
    loaded.process(sources, 2, out.size(), 48000.0f);
    CHECK_NEAR(out[0], 0.2f, 1e-5f);
}

TEST(effect_with_unconnected_input_outputs_silence) {
    EqThreeBand eq;
    std::vector<float> out(16, 1.0f);
    const float* inputs[] = {nullptr};
    eq.set_output_buffer(0, out.data());
    eq.process(inputs, 1, out.size(), 48000.0f);
    for (float s : out) {
        CHECK_NEAR(s, 0.0f, 1e-6f);
    }
}

TEST(noise_gate_silences_below_threshold) {
    NoiseGate gate;
    std::vector<float> quiet(64, 0.0001f);
    gate.threshold_db = -20.0f;
    gate.attack_ms = 1.0f;
    gate.release_ms = 1.0f;
    run_effect(gate, quiet);
    float max_abs = 0.0f;
    for (float s : quiet) {
        max_abs = std::max(max_abs, std::fabs(s));
    }
    CHECK(max_abs < 0.0002f);
}

TEST(eq_three_band_runs) {
    EqThreeBand eq;
    std::vector<float> block(128, 0.25f);
    eq.low_db = 3.0f;
    eq.mid_db = -3.0f;
    eq.high_db = 2.0f;
    run_effect(eq, block);
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
    run_effect(rev, block);
    float energy = 0.0f;
    for (size_t i = 512; i < block.size(); ++i) {
        energy += block[i] * block[i];
    }
    CHECK(energy > 1.0e-6f);
}

TEST(dry_wet_mix_blends_slots) {
    AudioGraph g;
    g.clear();
    const int in = g.add_node("audio_input", 0, 0);
    const int pre_a = g.add_node("preamp", 0, 0);
    const int pre_b = g.add_node("preamp", 0, 0);
    const int mix = g.add_node("dry_wet_mix", 0, 0);
    const int out = g.add_node("output", 0, 0);
    g.find_node(pre_a)->as<Preamp>()->gain_db = 0.0f;
    g.find_node(pre_b)->as<Preamp>()->gain_db = 6.0f;
    g.find_node(mix)->as<DryWetMix>()->mix = 0.5f;
    CHECK(g.add_link(in, 0, pre_a, 0));
    CHECK(g.add_link(in, 0, pre_b, 0));
    CHECK(g.add_link(pre_a, 0, mix, 0));
    CHECK(g.add_link(pre_b, 0, mix, 1));
    CHECK(g.add_link(mix, 0, out, 0));

    constexpr size_t block = 32;
    std::vector<float> live(block, 0.25f);
    std::vector<float> output(block, 0.0f);
    GraphProcessor proc(48000.0f);
    auto compiled = g.compile();
    CHECK(compiled);
    CHECK(proc.process(*compiled, live.data(), output.data(), block));
    // 0.5 * (0.25 + ~0.5) ≈ 0.375
    CHECK_NEAR(output[0], 0.375f, 0.04f);
}

TEST(wav_recorder_writes_file) {
    const char* path = "tests/_tmp_record.wav";
    std::remove(path);
    WavRecorderEngine rec;
    rec.set_armed(true, path, 48000.0f);
    std::vector<float> block(64, 0.1f);
    rec.write_samples(block.data(), block.size());
    rec.set_armed(false, path, 48000.0f);
    FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    if (f) {
        char riff[4];
        CHECK_EQ(static_cast<int>(std::fread(riff, 1, 4, f)), 4);
        CHECK(riff[0] == 'R' && riff[1] == 'I' && riff[2] == 'F' && riff[3] == 'F');
        std::fclose(f);
    }
    std::remove(path);
}

TEST(wav_recorder_finishes_file_when_destroyed_while_armed) {
    const char* path = "tests/_tmp_record_destroyed.wav";
    std::remove(path);
    {
        WavRecorderEngine rec;
        rec.set_armed(true, path, 48000.0f);
        std::vector<float> block(64, 0.1f);
        rec.write_samples(block.data(), block.size());
    }
    std::string error;
    auto clip = WavGenerator::load(path, 48000, error);
    CHECK(clip != nullptr);
    if (clip) {
        CHECK_EQ(static_cast<int>(clip->interleaved_stereo.size()), 64);
        CHECK_NEAR(clip->interleaved_stereo[63], 0.1f, 1e-6f);
    }
    std::remove(path);
}

TEST(wav_recorder_armed_is_not_persisted) {
    WavRecorder rec;
    rec.armed = true;
    CHECK(rec.save().find("armed") == std::string::npos);
    WavRecorder loaded;
    CHECK(loaded.load(R"({"armed":true})"));
    CHECK(!loaded.armed);
}

TEST(wav_recorder_path_is_saved) {
    WavRecorder rec;
    CHECK(rec.path == kDefaultRecordPath);
    rec.path = "out/take.wav";
    WavRecorder loaded;
    CHECK(loaded.load(rec.save()));
    CHECK(loaded.path == "out/take.wav");
}

TEST(drive_pedal_amount_zero_is_clean_for_every_model) {
    for (int model = 0; model < 3; ++model) {
        guitar_pedals::DrivePedal pedal;
        pedal.model = model;
        pedal.amount = 0.0f;
        pedal.tone = 1.0f;
        std::vector<float> block(4096, 0.01f);
        run_effect(pedal, block);
        float peak = 0.0f;
        bool finite = true;
        for (float s : block) {
            finite = finite && std::isfinite(s);
            peak = std::max(peak, std::fabs(s));
        }
        CHECK(finite);
        // Small signals stay small (no full-scale square) and are not muted.
        CHECK(peak < 0.05f);
        CHECK(std::fabs(block.back()) > 0.005f);
    }
}

TEST(flush_denormals_to_zero_applies_to_calling_thread) {
    bool flushed = false;
    std::thread worker([&] {
        enable_flush_denormals_to_zero();
        volatile float tiny = 1.0e-38f;
        volatile float half = 0.5f;
        const float product = tiny * half;
        flushed = product == 0.0f;
    });
    worker.join();
#if defined(__SSE__) || defined(_M_X64) || defined(__aarch64__)
    CHECK(flushed);
#else
    (void)flushed;
#endif
}

// Feeds `seconds` of a stereo 440 Hz sine with the given RMS through the AGC in
// 512-sample blocks and returns the RMS (dB) of the last block.
static float agc_settled_rms_db(Agc& agc, float input_rms_db, float seconds) {
    const float amp = std::pow(10.0f, input_rms_db / 20.0f) * std::sqrt(2.0f);
    std::vector<float> block(512);
    size_t frame = 0;
    const size_t blocks = static_cast<size_t>(seconds * 48000.0f / 256.0f);
    double sum_sq = 0.0;
    for (size_t b = 0; b < blocks; ++b) {
        for (size_t i = 0; i < block.size(); i += 2, ++frame) {
            const float s = amp * std::sin(2.0f * 3.14159265f * 440.0f * frame / 48000.0f);
            block[i] = s;
            block[i + 1] = s;
        }
        run_effect(agc, block);
        sum_sq = 0.0;
        for (float s : block) {
            sum_sq += static_cast<double>(s) * s;
        }
    }
    return 10.0f * std::log10(static_cast<float>(sum_sq / block.size()));
}

TEST(agc_raises_quiet_input_to_target) {
    Agc agc;
    agc.target_db = -18.0f;
    CHECK_NEAR(agc_settled_rms_db(agc, -36.0f, 4.0f), -18.0f, 1.5f);
    CHECK(agc.gain_db() > 15.0f);
}

TEST(agc_lowers_loud_input_to_target) {
    Agc agc;
    agc.target_db = -18.0f;
    CHECK_NEAR(agc_settled_rms_db(agc, -6.0f, 1.0f), -18.0f, 1.5f);
}

TEST(agc_boost_is_capped_by_max_gain) {
    Agc agc;
    agc.target_db = -18.0f;
    agc.max_gain_db = 6.0f;
    CHECK_NEAR(agc_settled_rms_db(agc, -40.0f, 4.0f), -34.0f, 1.0f);
}

TEST(agc_holds_gain_below_gate) {
    Agc agc;
    agc.gate_db = -60.0f;
    agc_settled_rms_db(agc, -80.0f, 2.0f);
    CHECK_NEAR(agc.gain_db(), 0.0f, 1e-3f);
}

TEST(agc_save_load_roundtrip) {
    Agc agc;
    agc.target_db = -12.0f;
    agc.max_gain_db = 10.0f;
    agc.release_ms = 900.0f;
    Agc loaded;
    CHECK(loaded.load(agc.save()));
    CHECK_NEAR(loaded.target_db, -12.0f, 1e-5f);
    CHECK_NEAR(loaded.max_gain_db, 10.0f, 1e-5f);
    CHECK_NEAR(loaded.release_ms, 900.0f, 1e-5f);
}
