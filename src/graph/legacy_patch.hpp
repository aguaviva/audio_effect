#pragma once

#include <string>

// Rewrites nodes of kinds that no longer exist into their replacements while a patch loads.
// The node keeps its id and position, and pins map one to one, so its links still apply.
namespace legacy_patch {

// A Clip Timeline ("timeline": six tracks of clips) becomes a Multi WAV ("multi_wav") with six
// channels and the same clips, including those saved as one whole file per track
// (trackN_path, trackN_start). type and params (the node's "params" object) are changed in
// place; other kinds are left alone. False when params is not an object.
bool migrate_node(std::string& type, std::string& params);

} // namespace legacy_patch
