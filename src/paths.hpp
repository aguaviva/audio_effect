#pragma once

// Default on-disk locations relative to the process working directory
// (normally the repo root). Keep these in sync with assets/.
inline constexpr const char* kDefaultWavPath = "assets/audio/danano.wav";
inline constexpr const char* kPreferredStartupWavPath = "assets/audio/solo_1.wav";
inline constexpr const char* kDefaultPatchPath = "assets/patches/default_patch.json";
inline constexpr const char* kDefaultRecordPath = "assets/recordings/capture.wav";
