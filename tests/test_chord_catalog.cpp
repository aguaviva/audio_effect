#include "test_harness.hpp"

#include "dsp/chord_catalog.hpp"

TEST(chord_catalog_count_and_names) {
    CHECK_EQ(kChordCount, kChordRootCount * kChordQualityCount);
    CHECK_EQ(kChordCount, 144);
    const int e_major = chord_index(4, ChordQuality::major);
    CHECK_STREQ(chord_display_name(e_major), "E Major");
    const int a_minor = chord_index(9, ChordQuality::minor);
    CHECK_STREQ(chord_display_name(a_minor), "A Minor");
}

TEST(chord_catalog_frequencies_positive_and_ordered_root) {
    const auto freqs = chord_note_frequencies(chord_index(0, ChordQuality::major)); // C Major
    CHECK(freqs[0] > 0.0);
    // Octave voice should be about 2x root.
    CHECK_NEAR(freqs[3] / freqs[0], 2.0f, 1e-3f);
}

TEST(chord_combo_items_stable) {
    const char* const* items = chord_combo_items();
    CHECK(items != nullptr);
    CHECK(items[0] != nullptr);
    CHECK_STREQ(items[0], "C Major");
    CHECK_STREQ(items[kChordCount - 1], "B Dim7");
}
