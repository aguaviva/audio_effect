#include "clip_clipboard.hpp"

#include <optional>

namespace clip_clipboard {
namespace {

std::optional<ArrangedClip>& copied() {
    static std::optional<ArrangedClip> clip;
    return clip;
}

} // namespace

void copy(const ArrangedClip& clip) { copied() = clip; }

const ArrangedClip* get() { return copied() ? &*copied() : nullptr; }

} // namespace clip_clipboard
