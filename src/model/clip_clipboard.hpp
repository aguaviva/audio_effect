#pragma once

#include "model/clip_arrangement.hpp"

// One copied clip, shared by every node with WAV clips, so clips can be copied between them.
// UI thread only.
namespace clip_clipboard {

void copy(const ArrangedClip& clip);
// The copied clip, or nullptr.
const ArrangedClip* get();

} // namespace clip_clipboard
