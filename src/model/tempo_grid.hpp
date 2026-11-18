#pragma once

#include <string>
#include <vector>

// From beat on, each beat lasts seconds_per_beat.
struct TempoGridChange {
    double beat = 0.0;
    double seconds_per_beat = 0.5;
};

// A time signature and tempo map: where beats and bars fall in seconds, and the snap grid
// that editing rounds to. Plain data, UI thread only.
class TempoGrid {
public:
    static constexpr int kDefaultDivision = 4; // a sixteenth in 4/4, as the piano roll

    void set_time_signature(int numerator, int denominator);
    int numerator() const { return numerator_; }
    int denominator() const { return denominator_; }
    // In beats (quarter notes): 3 for 3/4, 3 for 6/8.
    double beats_per_bar() const;

    // One tempo for the whole song, dropping any map.
    void set_bpm(double bpm);
    // The tempo at the start.
    double bpm() const;
    // Sorted by beat; the first change's tempo also applies before it. Empty keeps the tempo.
    void set_tempo_map(std::vector<TempoGridChange> changes);
    const std::vector<TempoGridChange>& tempo_map() const { return changes_; }
    bool has_tempo_changes() const { return changes_.size() > 1; }

    double seconds_at_beat(double beat) const;
    double beat_at_seconds(double seconds) const;

    // Grid lines per beat that editing snaps to.
    void set_division(int divisions_per_beat);
    int division() const { return division_; }
    double snap_beat(double beat) const;
    // seconds moved onto the nearest grid line, never before 0.
    double snap_seconds(double seconds) const;

    // A JSON object; load() leaves fields it does not find as they are.
    std::string save() const;
    bool load(const std::string& json);

private:
    int numerator_ = 4;
    int denominator_ = 4;
    int division_ = kDefaultDivision;
    std::vector<TempoGridChange> changes_{{0.0, 0.5}};
};

// The project's grid: the timeline ruler and every track's snapping use it. Loading a MIDI file
// sets it from the file; the patch saves it.
TempoGrid& song_grid();
