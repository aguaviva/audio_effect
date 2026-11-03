#include "wav_library.hpp"

const WavLibrary::Entry& WavLibrary::get(const std::string& path) {
    Entry& e = entries_[path];
    if (!e.wav) {
        e.error.clear();
        e.wav = WavGenerator::load(path, sample_rate_, e.error);
        e.peaks = e.wav ? std::make_shared<const WaveformPeaks>(*e.wav) : nullptr;
        if (!e.wav && e.error.empty()) {
            e.error = "could not be read";
        }
    }
    return e;
}
