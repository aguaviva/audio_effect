#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

// Programmatic guitar-style voicings: every root × common qualities.
inline constexpr int kChordRootCount = 12;
inline constexpr int kChordQualityCount = 12;
inline constexpr int kChordCount = kChordRootCount * kChordQualityCount;
inline constexpr int kChordVoices = 6;

enum class ChordQuality : int {
    major = 0,
    minor,
    dominant7,
    major7,
    minor7,
    diminished,
    augmented,
    sus2,
    sus4,
    add9,
    minor7flat5,
    diminished7,
    count
};

inline const char* chord_root_name(int root) {
    static constexpr const char* kNames[kChordRootCount] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    root = std::clamp(root, 0, kChordRootCount - 1);
    return kNames[root];
}

inline const char* chord_quality_name(ChordQuality quality) {
    switch (quality) {
    case ChordQuality::major: return "Major";
    case ChordQuality::minor: return "Minor";
    case ChordQuality::dominant7: return "7";
    case ChordQuality::major7: return "Maj7";
    case ChordQuality::minor7: return "Min7";
    case ChordQuality::diminished: return "Dim";
    case ChordQuality::augmented: return "Aug";
    case ChordQuality::sus2: return "Sus2";
    case ChordQuality::sus4: return "Sus4";
    case ChordQuality::add9: return "Add9";
    case ChordQuality::minor7flat5: return "m7b5";
    case ChordQuality::diminished7: return "Dim7";
    default: return "?";
    }
}

inline int chord_index(int root, ChordQuality quality) {
    root = std::clamp(root, 0, kChordRootCount - 1);
    const int q = std::clamp(static_cast<int>(quality), 0, kChordQualityCount - 1);
    return root * kChordQualityCount + q;
}

inline void chord_decode(int index, int& root, ChordQuality& quality) {
    index = std::clamp(index, 0, kChordCount - 1);
    root = index / kChordQualityCount;
    quality = static_cast<ChordQuality>(index % kChordQualityCount);
}

inline std::string chord_display_name(int index) {
    int root = 0;
    ChordQuality quality = ChordQuality::major;
    chord_decode(index, root, quality);
    return std::string(chord_root_name(root)) + " " + chord_quality_name(quality);
}

inline const char* const* chord_combo_items() {
    static std::array<std::string, kChordCount> storage{};
    static std::array<const char*, kChordCount> pointers{};
    static bool initialized = false;
    if (!initialized) {
        for (int i = 0; i < kChordCount; ++i) {
            storage[i] = chord_display_name(i);
            pointers[i] = storage[i].c_str();
        }
        initialized = true;
    }
    return pointers.data();
}

inline std::array<int, kChordVoices> chord_intervals(ChordQuality quality) {
    switch (quality) {
    case ChordQuality::major: return {{0, 4, 7, 12, 16, 19}};
    case ChordQuality::minor: return {{0, 3, 7, 12, 15, 19}};
    case ChordQuality::dominant7: return {{0, 4, 7, 10, 12, 16}};
    case ChordQuality::major7: return {{0, 4, 7, 11, 12, 16}};
    case ChordQuality::minor7: return {{0, 3, 7, 10, 12, 15}};
    case ChordQuality::diminished: return {{0, 3, 6, 12, 15, 18}};
    case ChordQuality::augmented: return {{0, 4, 8, 12, 16, 20}};
    case ChordQuality::sus2: return {{0, 2, 7, 12, 14, 19}};
    case ChordQuality::sus4: return {{0, 5, 7, 12, 17, 19}};
    case ChordQuality::add9: return {{0, 4, 7, 12, 14, 16}};
    case ChordQuality::minor7flat5: return {{0, 3, 6, 10, 12, 15}};
    case ChordQuality::diminished7: return {{0, 3, 6, 9, 12, 15}};
    default: return {{0, 4, 7, 12, 16, 19}};
    }
}

inline double midi_note_to_hz(double midi_note) {
    return 440.0 * std::pow(2.0, (midi_note - 69.0) / 12.0);
}

// Six-voice voicing built from C2..B2 roots upward.
inline std::array<double, kChordVoices> chord_note_frequencies(int index) {
    int root = 0;
    ChordQuality quality = ChordQuality::major;
    chord_decode(index, root, quality);
    const int root_midi = 36 + root; // C2 .. B2
    const auto intervals = chord_intervals(quality);
    std::array<double, kChordVoices> frequencies{};
    for (int voice = 0; voice < kChordVoices; ++voice) {
        frequencies[voice] = midi_note_to_hz(root_midi + intervals[voice]);
    }
    return frequencies;
}

inline size_t full_chord_period_samples(int chord, uint32_t sample_rate) {
    const auto frequencies = chord_note_frequencies(chord);
    constexpr size_t max_root_cycles = 8192;
    constexpr double phase_tolerance_cycles = 0.05;
    const double root_frequency = frequencies[0];
    double best_error = 1.0;
    size_t best_period = static_cast<size_t>(std::llround(sample_rate / root_frequency));

    for (size_t root_cycles = 1; root_cycles <= max_root_cycles; ++root_cycles) {
        const size_t candidate = static_cast<size_t>(std::llround(
            root_cycles * static_cast<double>(sample_rate) / root_frequency));
        double worst_note_error = 0.0;
        for (double frequency : frequencies) {
            const double cycles = frequency * candidate / sample_rate;
            const double fractional_cycle = cycles - std::round(cycles);
            worst_note_error = std::max(worst_note_error, std::abs(fractional_cycle));
        }
        if (worst_note_error < best_error) {
            best_error = worst_note_error;
            best_period = candidate;
        }
        if (worst_note_error <= phase_tolerance_cycles) {
            return candidate;
        }
    }
    return best_period;
}
