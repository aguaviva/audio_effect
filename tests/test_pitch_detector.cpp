#include "test_harness.hpp"

#include "dsp/pitch_detector.hpp"
#include "graph/node_types.hpp"

#include <cmath>
#include <vector>

TEST(pitch_note_names_known_frequencies) {
    CHECK_STREQ(pitch_frequency_to_note_string(440.0f), "A4");
    CHECK_STREQ(pitch_frequency_to_note_string(261.63f), "C4");
    CHECK_STREQ(pitch_frequency_to_note_string(0.0f), "--");
    CHECK_STREQ(pitch_frequency_to_note_string(-1.0f), "--");
}

TEST(pitch_detector_estimates_sine_near_a4) {
    constexpr float sr = 48000.0f;
    constexpr float hz = 440.0f;
    PitchDetector detector(sr);

    // Feed enough stereo frames to fill the analysis window + one hop.
    constexpr size_t frames = 4096;
    std::vector<float> interleaved(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        const float s = 0.25f * std::sin(2.0f * 3.14159265f * hz * static_cast<float>(i) / sr);
        interleaved[i * 2] = s;
        interleaved[i * 2 + 1] = s;
    }

    float detected = 0.0f;
    bool updated = false;
    // Process in blocks like the graph processor.
    constexpr size_t block_frames = 256;
    for (size_t offset = 0; offset + block_frames <= frames; offset += block_frames) {
        float hz_out = 0.0f;
        if (detector.process(interleaved.data() + offset * 2, block_frames * 2, 2, hz_out)) {
            detected = hz_out;
            updated = true;
        }
    }
    CHECK(updated);
    CHECK(detected > 0.0f);
    CHECK_NEAR(detected, hz, 5.0f);
    CHECK_STREQ(pitch_frequency_to_note_string(detected), "A4");
}

TEST(pitch_detector_is_visualizer_node_type) {
    CHECK(node_is_visualizer(NodeType::pitch_detector));
    CHECK_STREQ(node_type_key(NodeType::pitch_detector), "pitch_detector");
    NodeType parsed = NodeType::output;
    CHECK(try_parse_node_type("pitch_detector", parsed));
    CHECK(parsed == NodeType::pitch_detector);
}
