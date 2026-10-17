#include "audio_devices.hpp"

#include "miniaudio.h"

#include <cstring>

namespace {

std::vector<AudioDevice> list_devices(ma_device_type type) {
    std::vector<AudioDevice> devices;
    devices.push_back({"default", "default"});

    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
        return devices;
    }

    ma_device_info* playback_infos = nullptr;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_infos = nullptr;
    ma_uint32 capture_count = 0;
    if (ma_context_get_devices(&context, &playback_infos, &playback_count, &capture_infos,
                              &capture_count) != MA_SUCCESS) {
        ma_context_uninit(&context);
        return devices;
    }

    const ma_device_info* infos =
        (type == ma_device_type_capture) ? capture_infos : playback_infos;
    const ma_uint32 count =
        (type == ma_device_type_capture) ? capture_count : playback_count;

    for (ma_uint32 i = 0; i < count; ++i) {
        const char* name = infos[i].name;
        if (!name || name[0] == '\0') {
            continue;
        }
        if (std::strcmp(name, "default") == 0) {
            continue;
        }
        devices.push_back({name, name});
    }

    ma_context_uninit(&context);
    return devices;
}

} // namespace

std::vector<AudioDevice> list_capture_devices() {
    return list_devices(ma_device_type_capture);
}

std::vector<AudioDevice> list_playback_devices() {
    return list_devices(ma_device_type_playback);
}
