#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

struct WavClip {
    std::vector<int32_t> interleaved_stereo;

    size_t frame_count() const {
        return interleaved_stereo.size() / 2;
    }
};

class WavGenerator {
public:
    static std::shared_ptr<const WavClip> load(const std::string& path,
                                               uint32_t target_sample_rate,
                                               std::string& error) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            error = "Cannot open WAV file";
            return nullptr;
        }

        std::streamsize file_size = file.tellg();
        if (file_size < 12) {
            error = "File is too small to be a WAV file";
            return nullptr;
        }
        file.seekg(0);
        std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), file_size)) {
            error = "Failed to read WAV file";
            return nullptr;
        }

        if (!fourcc(bytes, 0, "RIFF") || !fourcc(bytes, 8, "WAVE")) {
            error = "Expected a RIFF/WAVE file";
            return nullptr;
        }

        uint16_t format = 0;
        uint16_t channels = 0;
        uint16_t bits_per_sample = 0;
        uint32_t source_sample_rate = 0;
        size_t data_offset = 0;
        size_t data_size = 0;
        bool have_format = false;

        for (size_t offset = 12; offset + 8 <= bytes.size();) {
            uint32_t chunk_size = read_u32(bytes, offset + 4);
            size_t content_offset = offset + 8;
            if (chunk_size > bytes.size() - content_offset) {
                error = "WAV chunk extends beyond end of file";
                return nullptr;
            }

            if (fourcc(bytes, offset, "fmt ")) {
                if (chunk_size < 16) {
                    error = "WAV format chunk is incomplete";
                    return nullptr;
                }
                format = read_u16(bytes, content_offset);
                channels = read_u16(bytes, content_offset + 2);
                source_sample_rate = read_u32(bytes, content_offset + 4);
                bits_per_sample = read_u16(bytes, content_offset + 14);
                have_format = true;
            } else if (fourcc(bytes, offset, "data")) {
                data_offset = content_offset;
                data_size = chunk_size;
            }

            size_t next = content_offset + chunk_size;
            offset = next + (chunk_size & 1u);
        }

        if (!have_format || data_size == 0) {
            error = "WAV is missing a format or audio data chunk";
            return nullptr;
        }
        if (format != 1 || (bits_per_sample != 16 && bits_per_sample != 24 && bits_per_sample != 32)) {
            error = "Supported audio is PCM WAV at 16, 24, or 32 bits";
            return nullptr;
        }
        if ((channels != 1 && channels != 2) || source_sample_rate == 0 || target_sample_rate == 0) {
            error = "WAV must be mono or stereo with a valid sample rate";
            return nullptr;
        }

        size_t bytes_per_sample = bits_per_sample / 8;
        size_t block_align = bytes_per_sample * channels;
        if (data_size < block_align || data_size % block_align != 0) {
            error = "WAV audio data has an invalid frame alignment";
            return nullptr;
        }
        size_t source_frames = data_size / block_align;
        size_t target_frames = static_cast<size_t>(std::llround(
            static_cast<double>(source_frames) * target_sample_rate / source_sample_rate));
        if (target_frames == 0 || target_frames > (static_cast<size_t>(-1) / (2 * sizeof(int32_t)))) {
            error = "WAV duration is invalid or too large";
            return nullptr;
        }

        std::vector<float> source(static_cast<size_t>(source_frames) * 2);
        for (size_t frame = 0; frame < source_frames; ++frame) {
            size_t frame_offset = data_offset + frame * block_align;
            for (size_t channel = 0; channel < 2; ++channel) {
                size_t source_channel = channels == 1 ? 0 : channel;
                const uint8_t* sample = bytes.data() + frame_offset + source_channel * bytes_per_sample;
                source[frame * 2 + channel] = decode_sample(sample, bits_per_sample);
            }
        }

        auto clip = std::make_shared<WavClip>();
        clip->interleaved_stereo.resize(target_frames * 2);
        constexpr double scale = 2147483648.0;
        for (size_t frame = 0; frame < target_frames; ++frame) {
            double source_position = static_cast<double>(frame) * source_sample_rate / target_sample_rate;
            size_t first = std::min(static_cast<size_t>(source_position), source_frames - 1);
            size_t second = std::min(first + 1, source_frames - 1);
            float fraction = static_cast<float>(source_position - first);
            for (size_t channel = 0; channel < 2; ++channel) {
                float a = source[first * 2 + channel];
                float b = source[second * 2 + channel];
                double normalized = std::clamp(a + (b - a) * fraction, -1.0f, 1.0f);
                double pcm = std::clamp(normalized * scale, -2147483648.0, 2147483647.0);
                clip->interleaved_stereo[frame * 2 + channel] = static_cast<int32_t>(std::llround(pcm));
            }
        }
        error.clear();
        return clip;
    }

private:
    static bool fourcc(const std::vector<uint8_t>& bytes, size_t offset, const char* value) {
        return offset + 4 <= bytes.size() && std::memcmp(bytes.data() + offset, value, 4) == 0;
    }

    static uint16_t read_u16(const std::vector<uint8_t>& bytes, size_t offset) {
        return static_cast<uint16_t>(bytes[offset]) |
               static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
    }

    static uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset) {
        return static_cast<uint32_t>(bytes[offset]) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    }

    static float decode_sample(const uint8_t* bytes, uint16_t bits) {
        if (bits == 16) {
            uint16_t raw = static_cast<uint16_t>(bytes[0]) |
                           static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8);
            int16_t value;
            std::memcpy(&value, &raw, sizeof(value));
            return static_cast<float>(value) / 32768.0f;
        }
        if (bits == 24) {
            int32_t value = static_cast<int32_t>(bytes[0]) |
                            (static_cast<int32_t>(bytes[1]) << 8) |
                            (static_cast<int32_t>(bytes[2]) << 16);
            if ((value & 0x800000) != 0) {
                value |= ~0xffffff;
            }
            return static_cast<float>(value) / 8388608.0f;
        }

        uint32_t raw = static_cast<uint32_t>(bytes[0]) |
                       (static_cast<uint32_t>(bytes[1]) << 8) |
                       (static_cast<uint32_t>(bytes[2]) << 16) |
                       (static_cast<uint32_t>(bytes[3]) << 24);
        int32_t value;
        std::memcpy(&value, &raw, sizeof(value));
        return static_cast<float>(static_cast<double>(value) / 2147483648.0);
    }
};
