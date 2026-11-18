#pragma once

#include "model/tempo_grid.hpp"

#include <string>
#include <vector>

class MidiSong;

// A note read from a MIDI file, in quarter-note beats.
struct ImportedNote {
    int pitch = 60;
    double start = 0.0;
    double duration = 1.0;
    int velocity = 100;
    int channel = 0;
    int end_status = 0x80;
    int end_velocity = 0;
};

// What the sequencer does not keep: track names, the song's time signature and tempo map, and
// each track's notes.
struct MidiSongInfo {
    int numerator = 4;
    int denominator = 4;
    // As the sequencer plays it: beat and seconds-per-beat tempo changes.
    std::vector<TempoGridChange> tempo_map;
    std::vector<std::string> track_names;
    std::vector<std::vector<ImportedNote>> notes;
};

// Reads the first max_tracks tracks of a loaded song; the first time signature found applies
// to the whole song. Plays the tracks through to read them, so rewind the song afterwards.
MidiSongInfo read_midi_song(MidiSong& song, int max_tracks);
