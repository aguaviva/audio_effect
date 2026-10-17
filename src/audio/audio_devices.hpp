#pragma once

#include <string>
#include <vector>

struct AudioDevice {
    std::string id;    // "default" or backend device name
    std::string label; // UI label
};

// Always includes "default" first. Cross-platform via miniaudio.
std::vector<AudioDevice> list_capture_devices();
std::vector<AudioDevice> list_playback_devices();
