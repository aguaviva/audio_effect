#pragma once

#include "graph/audio_graph.hpp"
#include "graph/graph_processor.hpp"

#include "miniaudio.h"

#include <atomic>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define SAMPLE_RATE 48000
#define CHANNELS 2
#define QUEUE_CAPACITY 16
#define WAVEFORM_POINTS 1024
#define AUDIO_PERIOD_FRAMES 256

struct AudioChunk {
    explicit AudioChunk(size_t sample_count);
    ~AudioChunk();

    std::vector<int32_t> samples;
    std::vector<float> work_samples;
};

class AudioBufferQueue {
public:
    bool push(std::unique_ptr<AudioChunk>&& chunk);
    std::unique_ptr<AudioChunk> pop();
    size_t size() const;

private:
    std::array<std::unique_ptr<AudioChunk>, QUEUE_CAPACITY> ring_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

struct DoubleBufferedWaveform {
    float input_buffer[2][WAVEFORM_POINTS]{};
    float output_buffer[2][WAVEFORM_POINTS]{};
    size_t active_write_index = 0;
    size_t sample_counter = 0;
    std::atomic<size_t> ready_read_index{0};
    float previous_sample = 0.0f;
    bool has_previous_sample = false;
    bool capturing_frame = false;
    size_t capture_span_samples = WAVEFORM_POINTS;
    size_t raw_samples_in_frame = 0;
    size_t next_display_point = 1;

    void push_sample(float input_sample, float output_sample, float trigger_level,
                     size_t requested_span_samples = WAVEFORM_POINTS);
    void get_latest_frame(float* input_destination, float* output_destination);
};

struct AudioPipeline {
    size_t period_size = AUDIO_PERIOD_FRAMES; // frames
    size_t samples_per_period = AUDIO_PERIOD_FRAMES * CHANNELS;

    AudioBufferQueue free_pool;
    AudioBufferQueue cap_to_filter;
    AudioBufferQueue filter_to_play;

    DoubleBufferedWaveform waveform;
    float ui_input_waveform[WAVEFORM_POINTS]{};
    float ui_output_waveform[WAVEFORM_POINTS]{};

    std::atomic<bool> running{true};
    std::atomic<size_t> live_chunks{0};
    std::atomic<uint64_t> xrun_count{0};
    std::atomic<uint64_t> dropped_capture_reads{0};
    std::atomic<float> scope_trigger_level{0.0f};

    AudioGraph* graph = nullptr;
    std::shared_ptr<const WavClip> loaded_wav;
    std::string wav_status{"No WAV loaded"};

    ma_context context{};
    bool context_ready = false;
    ma_device capture{};
    ma_device playback{};
    bool capture_ready = false;
    bool playback_ready = false;

    std::thread filter;
    std::thread silent_capture; // used when playback is live but capture failed

    // Open capture + playback via miniaudio. Returns false if either stream fails.
    bool open_audio(const char* capture_device, const char* playback_device, std::string& error);
    bool open_audio(const char* device, std::string& error) {
        return open_audio(device, device, error);
    }
    // Reopen one stream on a different device. Offline: store preference only.
    bool set_capture_device(const char* device, std::string& error);
    bool set_playback_device(const char* device, std::string& error);
    const std::string& capture_device() const { return capture_device_; }
    const std::string& playback_device() const { return playback_device_; }

    void start();
    // Drive the graph from WAV/chord with silent "live" input when devices are unavailable
    // (CI / headless demo). Visualizers and DSP still update in realtime.
    void start_offline();
    void stop();
    void load_wav(const std::string& path);

    bool offline_mode = false;
    std::string capture_device_{"default"};
    std::string playback_device_{"default"};

    // Audio-thread staging (only touched from the matching device callback).
    std::vector<int32_t> capture_staging;
    std::vector<int32_t> playback_staging;
    std::mutex device_mutex;
};

extern AudioPipeline g_pipeline;
