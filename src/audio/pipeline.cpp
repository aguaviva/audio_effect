#include "pipeline.hpp"

#include "dsp/denormals.hpp"
#include "dsp/wav_generator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

AudioPipeline g_pipeline;

namespace {

bool find_device_id(ma_context* context, ma_device_type type, const std::string& name,
                    ma_device_id* out_id) {
    if (name.empty() || name == "default") {
        return false; // use default device (nullptr id)
    }

    ma_device_info* playback_infos = nullptr;
    ma_uint32 playback_count = 0;
    ma_device_info* capture_infos = nullptr;
    ma_uint32 capture_count = 0;
    if (ma_context_get_devices(context, &playback_infos, &playback_count, &capture_infos,
                               &capture_count) != MA_SUCCESS) {
        return false;
    }

    const ma_device_info* infos =
        (type == ma_device_type_capture) ? capture_infos : playback_infos;
    const ma_uint32 count =
        (type == ma_device_type_capture) ? capture_count : playback_count;

    for (ma_uint32 i = 0; i < count; ++i) {
        if (infos[i].name && name == infos[i].name) {
            *out_id = infos[i].id;
            return true;
        }
    }
    return false;
}

void capture_data_callback(ma_device* device, void* /*output*/, const void* input,
                           ma_uint32 frame_count) {
    auto* pipe = static_cast<AudioPipeline*>(device->pUserData);
    if (!pipe || !input || frame_count == 0) {
        return;
    }

    const auto* in = static_cast<const int32_t*>(input);
    const size_t samples = static_cast<size_t>(frame_count) * CHANNELS;
    pipe->capture_staging.insert(pipe->capture_staging.end(), in, in + samples);

    while (pipe->capture_staging.size() >= pipe->samples_per_period) {
        auto chunk = pipe->free_pool.pop();
        if (!chunk) {
            pipe->dropped_capture_reads.fetch_add(1, std::memory_order_relaxed);
            pipe->capture_staging.erase(pipe->capture_staging.begin(),
                                        pipe->capture_staging.begin() +
                                            static_cast<std::ptrdiff_t>(pipe->samples_per_period));
            continue;
        }
        std::memcpy(chunk->samples.data(), pipe->capture_staging.data(),
                    pipe->samples_per_period * sizeof(int32_t));
        pipe->capture_staging.erase(pipe->capture_staging.begin(),
                                    pipe->capture_staging.begin() +
                                        static_cast<std::ptrdiff_t>(pipe->samples_per_period));
        if (!pipe->cap_to_filter.push(std::move(chunk))) {
            pipe->dropped_capture_reads.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void playback_data_callback(ma_device* device, void* output, const void* /*input*/,
                            ma_uint32 frame_count) {
    auto* pipe = static_cast<AudioPipeline*>(device->pUserData);
    auto* out = static_cast<int32_t*>(output);
    const size_t needed = static_cast<size_t>(frame_count) * CHANNELS;
    if (!pipe || !out || needed == 0) {
        return;
    }

    while (pipe->playback_staging.size() < needed) {
        auto chunk = pipe->filter_to_play.pop();
        if (!chunk) {
            break;
        }
        pipe->playback_staging.insert(pipe->playback_staging.end(), chunk->samples.begin(),
                                      chunk->samples.end());
        // Return buffer to free pool; drop if full (should be rare).
        if (!pipe->free_pool.push(std::move(chunk))) {
            pipe->xrun_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    if (pipe->playback_staging.size() >= needed) {
        std::memcpy(out, pipe->playback_staging.data(), needed * sizeof(int32_t));
        pipe->playback_staging.erase(pipe->playback_staging.begin(),
                                     pipe->playback_staging.begin() +
                                         static_cast<std::ptrdiff_t>(needed));
    } else {
        const size_t have = pipe->playback_staging.size();
        if (have > 0) {
            std::memcpy(out, pipe->playback_staging.data(), have * sizeof(int32_t));
            pipe->playback_staging.clear();
        }
        std::memset(out + have, 0, (needed - have) * sizeof(int32_t));
        pipe->xrun_count.fetch_add(1, std::memory_order_relaxed);
    }
}

bool init_device(AudioPipeline& pipe, ma_device_type type, const std::string& name,
                 ma_device* device, std::string& error) {
    ma_device_config config = ma_device_config_init(type);
    config.capture.format = ma_format_s32;
    config.capture.channels = CHANNELS;
    config.playback.format = ma_format_s32;
    config.playback.channels = CHANNELS;
    config.sampleRate = SAMPLE_RATE;
    config.periodSizeInFrames = AUDIO_PERIOD_FRAMES;
    config.pUserData = &pipe;

    if (type == ma_device_type_capture) {
        config.dataCallback = capture_data_callback;
    } else {
        config.dataCallback = playback_data_callback;
    }

    ma_device_id id{};
    ma_device_id* id_ptr = nullptr;
    if (find_device_id(&pipe.context, type, name, &id)) {
        id_ptr = &id;
    }

    if (type == ma_device_type_capture) {
        config.capture.pDeviceID = id_ptr;
    } else {
        config.playback.pDeviceID = id_ptr;
    }

    ma_result result = ma_device_init(&pipe.context, &config, device);
    if (result != MA_SUCCESS) {
        error = std::string("miniaudio device init failed (") +
                ((type == ma_device_type_capture) ? "capture" : "playback") + ")";
        return false;
    }
    return true;
}

// Sleeps until start + n * period, so per-wakeup lateness does not accumulate.
class PeriodClock {
public:
    explicit PeriodClock(size_t frames_per_period)
        : frames_per_period_(frames_per_period), start_(Clock::now()) {}

    void wait_next() {
        ++periods_;
        const auto due = start_ + elapsed(periods_);
        const auto now = Clock::now();
        if (now > due + elapsed(kMaxLagPeriods)) {
            // Stalled (debugger, suspend): restart instead of bursting to catch up.
            start_ = now;
            periods_ = 0;
            return;
        }
        std::this_thread::sleep_until(due);
    }

private:
    using Clock = std::chrono::steady_clock;
    static constexpr uint64_t kMaxLagPeriods = 8;

    std::chrono::nanoseconds elapsed(uint64_t periods) const {
        constexpr uint64_t kNsPerSecond = 1000000000ull;
        const uint64_t frames = periods * frames_per_period_;
        return std::chrono::nanoseconds((frames / SAMPLE_RATE) * kNsPerSecond +
                                        (frames % SAMPLE_RATE) * kNsPerSecond / SAMPLE_RATE);
    }

    uint64_t frames_per_period_;
    Clock::time_point start_;
    uint64_t periods_ = 0;
};

void filter_processor() {
    enable_flush_denormals_to_zero();
    GraphProcessor processor(static_cast<float>(SAMPLE_RATE));
    const size_t block_size = g_pipeline.samples_per_period;
    processor.set_block_size(block_size);

    while (g_pipeline.running.load(std::memory_order_relaxed)) {
        auto chunk = g_pipeline.cap_to_filter.pop();
        if (!chunk) {
            std::this_thread::yield();
            continue;
        }

        if (chunk->samples.size() != block_size || chunk->work_samples.size() != block_size) {
            fprintf(stderr, "Audio chunk size mismatch with graph block size (%zu vs %zu)\n",
                    chunk->samples.size(), block_size);
            g_pipeline.running.store(false, std::memory_order_relaxed);
            return;
        }

        auto compiled = g_pipeline.graph ? g_pipeline.graph->latest() : nullptr;
        auto wav = std::atomic_load_explicit(&g_pipeline.loaded_wav, std::memory_order_acquire);
        processor.set_loaded_wav(wav);
        if (g_pipeline.graph) {
            processor.set_record_path(g_pipeline.graph->record_path());
        }

        float* out = chunk->work_samples.data();
        bool produced = false;
        if (compiled) {
            produced = processor.process(*compiled, chunk->samples.data(), out, block_size);
        }
        if (!produced) {
            std::fill(out, out + block_size, 0.0f);
        }

        constexpr double pcm_scale = 2147483648.0;
        float trigger = g_pipeline.scope_trigger_level.load(std::memory_order_relaxed);
        for (size_t i = 0; i < block_size; i += CHANNELS) {
            float input_sample = static_cast<float>(chunk->samples[i]) / 2147483648.0f;
            g_pipeline.waveform.push_sample(input_sample, out[i], trigger);

            for (size_t ch = 0; ch < CHANNELS; ++ch) {
                size_t idx = i + ch;
                double sample = std::clamp(static_cast<double>(out[idx]), -1.0, 1.0);
                double pcm = std::clamp(sample * pcm_scale, -2147483648.0, 2147483647.0);
                chunk->samples[idx] = static_cast<int32_t>(std::llround(pcm));
            }
        }

        while (g_pipeline.running.load(std::memory_order_relaxed) &&
               !g_pipeline.filter_to_play.push(std::move(chunk))) {
            std::this_thread::yield();
        }
    }
}

void offline_processor() {
    enable_flush_denormals_to_zero();
    GraphProcessor processor(static_cast<float>(SAMPLE_RATE));
    const size_t block_size = g_pipeline.samples_per_period;
    processor.set_block_size(block_size);
    std::vector<int32_t> live(block_size, 0);
    std::vector<float> out(block_size, 0.0f);

    PeriodClock clock(g_pipeline.period_size);
    while (g_pipeline.running.load(std::memory_order_relaxed)) {
        auto compiled = g_pipeline.graph ? g_pipeline.graph->latest() : nullptr;
        auto wav = std::atomic_load_explicit(&g_pipeline.loaded_wav, std::memory_order_acquire);
        processor.set_loaded_wav(wav);
        if (g_pipeline.graph) {
            processor.set_record_path(g_pipeline.graph->record_path());
        }
        if (compiled) {
            processor.process(*compiled, live.data(), out.data(), block_size);
        }
        clock.wait_next();
    }
}

void silent_capture_producer() {
    PeriodClock clock(g_pipeline.period_size);
    while (g_pipeline.running.load(std::memory_order_relaxed) &&
           g_pipeline.silent_running.load(std::memory_order_relaxed)) {
        auto chunk = g_pipeline.free_pool.pop();
        if (chunk) {
            std::fill(chunk->samples.begin(), chunk->samples.end(), 0);
            if (!g_pipeline.cap_to_filter.push(std::move(chunk))) {
                g_pipeline.dropped_capture_reads.fetch_add(1, std::memory_order_relaxed);
            }
        }
        clock.wait_next();
    }
}

} // namespace

AudioChunk::AudioChunk(size_t sample_count) : samples(sample_count), work_samples(sample_count) {
    g_pipeline.live_chunks.fetch_add(1, std::memory_order_relaxed);
}

AudioChunk::~AudioChunk() {
    g_pipeline.live_chunks.fetch_sub(1, std::memory_order_relaxed);
}

bool AudioBufferQueue::push(std::unique_ptr<AudioChunk>&& chunk) {
    size_t head = head_.load(std::memory_order_relaxed);
    size_t tail = tail_.load(std::memory_order_acquire);
    if ((head + 1) % QUEUE_CAPACITY == tail) return false;
    ring_[head] = std::move(chunk);
    head_.store((head + 1) % QUEUE_CAPACITY, std::memory_order_release);
    return true;
}

std::unique_ptr<AudioChunk> AudioBufferQueue::pop() {
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t head = head_.load(std::memory_order_acquire);
    if (head == tail) return nullptr;
    std::unique_ptr<AudioChunk> chunk = std::move(ring_[tail]);
    tail_.store((tail + 1) % QUEUE_CAPACITY, std::memory_order_release);
    return chunk;
}

size_t AudioBufferQueue::size() const {
    size_t head = head_.load(std::memory_order_relaxed);
    size_t tail = tail_.load(std::memory_order_relaxed);
    if (head >= tail) return head - tail;
    return QUEUE_CAPACITY - (tail - head);
}

void DoubleBufferedWaveform::push_sample(float input_sample, float output_sample, float trigger_level,
                                         size_t requested_span_samples) {
    if (!capturing_frame) {
        bool crossed_trigger = has_previous_sample &&
            previous_sample < trigger_level && input_sample >= trigger_level;
        previous_sample = input_sample;
        has_previous_sample = true;
        if (!crossed_trigger) {
            return;
        }
        capturing_frame = true;
        capture_span_samples = std::max(requested_span_samples, static_cast<size_t>(WAVEFORM_POINTS));
        sample_counter = 0;
        raw_samples_in_frame = 0;
        next_display_point = 1;
        input_buffer[active_write_index][sample_counter] = input_sample;
        output_buffer[active_write_index][sample_counter] = output_sample;
        ++sample_counter;
        ++raw_samples_in_frame;
        return;
    }

    ++raw_samples_in_frame;
    previous_sample = input_sample;
    if (next_display_point * (capture_span_samples - 1) <=
        (raw_samples_in_frame - 1) * (WAVEFORM_POINTS - 1)) {
        input_buffer[active_write_index][sample_counter] = input_sample;
        output_buffer[active_write_index][sample_counter] = output_sample;
        ++sample_counter;
        ++next_display_point;

        if (sample_counter >= WAVEFORM_POINTS) {
            sample_counter = 0;
            size_t completed = active_write_index;
            active_write_index = 1 - active_write_index;
            ready_read_index.store(completed, std::memory_order_release);
            capturing_frame = false;
        }
    }
}

void DoubleBufferedWaveform::get_latest_frame(float* input_destination, float* output_destination) {
    size_t src = ready_read_index.load(std::memory_order_acquire);
    std::memcpy(input_destination, input_buffer[src], sizeof(float) * WAVEFORM_POINTS);
    std::memcpy(output_destination, output_buffer[src], sizeof(float) * WAVEFORM_POINTS);
}

bool AudioPipeline::open_audio(const char* capture_device, const char* playback_device,
                               std::string& error) {
    std::lock_guard<std::mutex> lock(device_mutex);
    capture_device_ = (capture_device && capture_device[0] != '\0') ? capture_device : "default";
    playback_device_ = (playback_device && playback_device[0] != '\0') ? playback_device : "default";

    if (!context_ready) {
        if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
            error = "Failed to initialize miniaudio context";
            return false;
        }
        context_ready = true;
    }

    period_size = AUDIO_PERIOD_FRAMES;
    samples_per_period = period_size * CHANNELS;
    capture_staging.clear();
    playback_staging.clear();

    // Drain any leftover free-pool entries from a previous open, then refill.
    while (free_pool.pop()) {
    }
    for (size_t i = 0; i < QUEUE_CAPACITY - 1; ++i) {
        free_pool.push(std::make_unique<AudioChunk>(samples_per_period));
    }

    std::string capture_error;
    capture_ready = init_device(*this, ma_device_type_capture, capture_device_, &capture,
                               capture_error);

    std::string playback_error;
    playback_ready = init_device(*this, ma_device_type_playback, playback_device_, &playback,
                                playback_error);

    if (!playback_ready) {
        if (capture_ready) {
            ma_device_uninit(&capture);
            capture_ready = false;
        }
        error = playback_error.empty() ? "Failed to open playback device" : playback_error;
        return false;
    }

    if (!capture_ready) {
        // Playback-only is OK; silent capture producer fills the graph live input.
        std::fprintf(stderr, "%s — continuing with silent capture\n", capture_error.c_str());
    }

    std::printf("[miniaudio] period=%zu frames, capture=%s, playback=%s\n", period_size,
                capture_ready ? capture_device_.c_str() : "(silent)", playback_device_.c_str());
    return true;
}

void AudioPipeline::start_silent_capture() {
    if (silent_capture.joinable()) {
        return;
    }
    silent_running.store(true, std::memory_order_relaxed);
    silent_capture = std::thread(silent_capture_producer);
}

void AudioPipeline::stop_silent_capture() {
    silent_running.store(false, std::memory_order_relaxed);
    if (silent_capture.joinable()) {
        silent_capture.join();
    }
}

bool AudioPipeline::open_stream(ma_device_type type, const std::string& name, std::string& error) {
    const bool is_capture = type == ma_device_type_capture;
    ma_device* device = is_capture ? &capture : &playback;
    if (!init_device(*this, type, name, device, error)) {
        return false;
    }
    if (running.load(std::memory_order_relaxed) && ma_device_start(device) != MA_SUCCESS) {
        error = is_capture ? "Failed to start capture device" : "Failed to start playback device";
        ma_device_uninit(device);
        return false;
    }
    return true;
}

bool AudioPipeline::set_capture_device(const char* device, std::string& error) {
    const std::string name = (device && device[0] != '\0') ? device : "default";
    std::lock_guard<std::mutex> lock(device_mutex);
    const std::string previous = capture_device_;
    capture_device_ = name;
    if (offline_mode || !context_ready) {
        return true;
    }

    stop_silent_capture();
    if (capture_ready) {
        ma_device_stop(&capture);
        ma_device_uninit(&capture);
        capture_ready = false;
    }
    capture_staging.clear();

    capture_ready = open_stream(ma_device_type_capture, name, error);
    if (!capture_ready && previous != name) {
        std::string ignored;
        capture_ready = open_stream(ma_device_type_capture, previous, ignored);
        if (capture_ready) {
            capture_device_ = previous;
        }
    }
    if (!capture_ready && running.load(std::memory_order_relaxed)) {
        start_silent_capture();
    }
    if (!capture_ready || capture_device_ != name) {
        last_device_error = "Capture '" + name + "': " + error;
        return false;
    }
    if (last_device_error.rfind("Capture", 0) == 0) {
        last_device_error.clear();
    }
    return true;
}

bool AudioPipeline::set_playback_device(const char* device, std::string& error) {
    const std::string name = (device && device[0] != '\0') ? device : "default";
    std::lock_guard<std::mutex> lock(device_mutex);
    const std::string previous = playback_device_;
    playback_device_ = name;
    if (offline_mode || !context_ready) {
        return true;
    }

    if (playback_ready) {
        ma_device_stop(&playback);
        ma_device_uninit(&playback);
        playback_ready = false;
    }
    playback_staging.clear();

    playback_ready = open_stream(ma_device_type_playback, name, error);
    if (!playback_ready && previous != name) {
        std::string ignored;
        playback_ready = open_stream(ma_device_type_playback, previous, ignored);
        if (playback_ready) {
            playback_device_ = previous;
        }
    }
    if (!playback_ready || playback_device_ != name) {
        last_device_error = "Playback '" + name + "': " + error;
        return false;
    }
    if (last_device_error.rfind("Playback", 0) == 0) {
        last_device_error.clear();
    }
    return true;
}

void AudioPipeline::start() {
    offline_mode = false;
    running.store(true, std::memory_order_relaxed);
    filter = std::thread(filter_processor);

    std::lock_guard<std::mutex> lock(device_mutex);
    if (capture_ready) {
        if (ma_device_start(&capture) != MA_SUCCESS) {
            std::fprintf(stderr, "Failed to start capture; using silent capture\n");
            ma_device_uninit(&capture);
            capture_ready = false;
        }
    }
    if (!capture_ready) {
        start_silent_capture();
    }

    if (playback_ready) {
        if (ma_device_start(&playback) != MA_SUCCESS) {
            std::fprintf(stderr, "Failed to start playback\n");
            running.store(false, std::memory_order_relaxed);
        }
    }
}

void AudioPipeline::start_offline() {
    offline_mode = true;
    running.store(true, std::memory_order_relaxed);
    period_size = AUDIO_PERIOD_FRAMES;
    samples_per_period = period_size * CHANNELS;
    filter = std::thread(offline_processor);
}

void AudioPipeline::stop() {
    running.store(false, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(device_mutex);
        if (capture_ready) {
            ma_device_stop(&capture);
            ma_device_uninit(&capture);
            capture_ready = false;
        }
        if (playback_ready) {
            ma_device_stop(&playback);
            ma_device_uninit(&playback);
            playback_ready = false;
        }
        if (context_ready) {
            ma_context_uninit(&context);
            context_ready = false;
        }
    }

    stop_silent_capture();
    if (filter.joinable()) {
        filter.join();
    }
}

void AudioPipeline::load_wav(const std::string& path) {
    std::string error;
    auto clip = WavGenerator::load(path, SAMPLE_RATE, error);
    if (clip) {
        std::atomic_store_explicit(&loaded_wav, clip, std::memory_order_release);
        wav_status = "Loaded " + path;
        if (graph) {
            graph->set_wav_path(path);
        }
    } else {
        wav_status = "Load failed: " + error;
    }
}
