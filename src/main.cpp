#include <alsa/asoundlib.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <iostream>
#include <thread>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

// ImGui & Graphics Headers
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "audio_input_sources.hpp"
#include "dynamic_high_pass_filter.hpp"
#include "dynamic_low_pass_filter.hpp"
#include "echo_effect.hpp"
#include "guitar_pedals.hpp"
#include "preamp.hpp"
#include "pitch_detector.hpp"
#include "wav_generator.hpp"
#include "valve_compressor.hpp"
#include <GLFW/glfw3.h>

#define SAMPLE_RATE 48000
#define CHANNELS 2
//#define PERIOD_SIZE 64
//#define SAMPLES_PER_PERIOD (PERIOD_SIZE * CHANNELS) // Exactly 128 samples
#define QUEUE_CAPACITY 16
#define WAVEFORM_POINTS 1024

snd_pcm_uframes_t PERIOD_SIZE = 0;
snd_pcm_uframes_t SAMPLES_PER_PERIOD = 0;
std::atomic<size_t> g_live_chunks{0};

// Shared Controls
std::atomic<bool> g_preamp_enabled{true};
std::atomic<float> g_preamp_gain_db{0.0f};
std::atomic<bool> g_low_pass_enabled{true};
std::atomic<float> g_cutoff_freq{8000.0f};
std::atomic<float> g_high_pass_cutoff_freq{70.0f};
std::atomic<bool> g_high_pass_enabled{true};
std::atomic<bool> g_echo_enabled{false};
std::atomic<float> g_echo_delay_ms{320.0f};
std::atomic<float> g_echo_feedback{0.35f};
std::atomic<float> g_echo_mix{0.25f};
std::atomic<bool> g_pedal_drive_enabled{false};
std::atomic<int> g_pedal_drive_model{0};
std::atomic<float> g_pedal_drive_amount{3.0f};
std::atomic<float> g_pedal_drive_tone{0.65f};
std::atomic<float> g_scope_trigger_level{0.0f};
std::atomic<float> g_detected_pitch_hz{0.0f};
std::atomic<bool> g_pitch_detection_enabled{true};
std::atomic<bool> g_valve_compressor_enabled{false};
std::atomic<float> g_compressor_threshold_db{-18.0f};
std::atomic<float> g_compressor_ratio{3.0f};
std::atomic<float> g_compressor_attack_ms{15.0f};
std::atomic<float> g_compressor_release_ms{100.0f};
std::atomic<float> g_compressor_drive{1.25f};
std::atomic<float> g_compressor_makeup_db{0.0f};
std::shared_ptr<const WavClip> g_loaded_wav;
std::atomic<int> g_input_source{static_cast<int>(InputSource::wav_file)};
std::atomic<int> g_chord_selection{0};
std::atomic<bool> g_wav_looping{true};
std::string g_wav_status{"No WAV loaded"};
std::atomic<bool> g_running{true};

// Audio Chunk containing a buffer sized from the negotiated ALSA period
struct AudioChunk {
    explicit AudioChunk(size_t sample_count) : samples(sample_count), work_samples(sample_count) {
        g_live_chunks.fetch_add(1, std::memory_order_relaxed);
    }

    ~AudioChunk() {
        g_live_chunks.fetch_sub(1, std::memory_order_relaxed);
    }

    std::vector<int32_t> samples;
    std::vector<float> work_samples;
};

// Fixed-size, Lock-Free SPSC Queue for AudioChunk ownership
class AudioBufferQueue {
public:
    bool push(std::unique_ptr<AudioChunk>&& chunk) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_acquire);
        if ((head + 1) % QUEUE_CAPACITY == tail) return false; // Full

        ring_[head] = std::move(chunk);
        head_.store((head + 1) % QUEUE_CAPACITY, std::memory_order_release);
        return true;
    }

    std::unique_ptr<AudioChunk> pop() {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t head = head_.load(std::memory_order_acquire);
        if (head == tail) return nullptr; // Empty

        std::unique_ptr<AudioChunk> chunk = std::move(ring_[tail]);
        tail_.store((tail + 1) % QUEUE_CAPACITY, std::memory_order_release);
        return chunk;
    }

    // Zero-overhead count calculation for visualization
    size_t size() const {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (head >= tail) return head - tail;
        return QUEUE_CAPACITY - (tail - head);
    }

private:
    std::array<std::unique_ptr<AudioChunk>, QUEUE_CAPACITY> ring_;
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

// Queue Pipeline Instances
static AudioBufferQueue g_cap_to_filter_q;  // Queue 1: Capture Producer -> Filter
static AudioBufferQueue g_filter_to_play_q; // Queue 2: Filter -> Playback Consumer
static AudioBufferQueue g_free_pool_q;      // Queue 3: Recycled Free Buffers Pool

// Double-Buffered Waveform Visualizer
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
                     size_t requested_span_samples = WAVEFORM_POINTS) {
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

    void get_latest_frame(float* input_destination, float* output_destination) {
        size_t src = ready_read_index.load(std::memory_order_acquire);
        std::memcpy(input_destination, input_buffer[src], sizeof(float) * WAVEFORM_POINTS);
        std::memcpy(output_destination, output_buffer[src], sizeof(float) * WAVEFORM_POINTS);
    }
};

static DoubleBufferedWaveform g_waveform;
static float g_ui_input_waveform[WAVEFORM_POINTS]{};
static float g_ui_output_waveform[WAVEFORM_POINTS]{};

#include <atomic>
#include <cstdint>

// Global or shared atomic counter for tracking XRUNs/dropouts
std::atomic<uint64_t> g_xrun_count{0};
std::atomic<uint64_t> g_dropped_capture_reads{0};

// --- Producer Thread ---
void capture_producer(snd_pcm_t* capture_handle) {
    std::vector<int32_t> discard_buffer(SAMPLES_PER_PERIOD);
    AudioInputSources input_sources(PERIOD_SIZE, CHANNELS);

    while (g_running.load(std::memory_order_relaxed)) {
        // 1. Try to pop a free chunk
        auto chunk = g_free_pool_q.pop();
        int32_t* samples = chunk ? chunk->samples.data() : discard_buffer.data();

        InputSource source = static_cast<InputSource>(g_input_source.load(std::memory_order_relaxed));
        auto clip = std::atomic_load_explicit(&g_loaded_wav, std::memory_order_acquire);
        int failed_read = 0;
        int recovery_error = 0;
        if (!input_sources.read_period(source, clip,
                                       g_wav_looping.load(std::memory_order_relaxed),
                                       g_chord_selection.load(std::memory_order_relaxed),
                                       capture_handle, samples, PERIOD_SIZE, SAMPLE_RATE,
                                       g_xrun_count, failed_read, recovery_error)) {
            fprintf(stderr, "Fatal ALSA capture read error (%s), recovery failed (%s)\n",
                    snd_strerror(failed_read), snd_strerror(recovery_error));
            g_running.store(false, std::memory_order_relaxed);
            return;
        }

        if (!chunk) {
            g_dropped_capture_reads.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // 3. Push to downstream queue. If the filter queue is full, spin-yield until space opens.
        while (g_running.load(std::memory_order_relaxed) && !g_cap_to_filter_q.push(std::move(chunk))) {
            std::this_thread::yield();
        }
    }
}

// --- Filter Thread ---
void filter_processor() {
    static EchoEffect echo(SAMPLE_RATE + 1);
    static Preamp preamp;
    static DynamicHighPassFilter hpf;
    static DynamicLowPassFilter lpf;
    static ValveCompressor compressor;
    static guitar_pedals::DrivePedal drive_pedal;
    static PitchDetector pitch_detector{SAMPLE_RATE, 70.0f, 1400.0f};
    bool high_pass_was_enabled = false;
    bool low_pass_was_enabled = false;
    bool echo_was_enabled = false;
    bool drive_pedal_was_enabled = false;
    int previous_drive_model = -1;
    bool pitch_detection_was_enabled = false;

    while (g_running.load(std::memory_order_relaxed)) {
        auto chunk = g_cap_to_filter_q.pop();
        if (!chunk) {
            std::this_thread::yield();
            continue;
        }

        float* samples = chunk->work_samples.data();
        for (size_t i = 0; i < chunk->samples.size(); ++i) {
            samples[i] = static_cast<float>(chunk->samples[i]) / 2147483648.0f;
        }

        bool pitch_detection_enabled = g_pitch_detection_enabled.load(std::memory_order_relaxed);
        if (pitch_detection_enabled) {
            if (!pitch_detection_was_enabled) {
                pitch_detector.reset();
            }
            float detected_pitch_hz = 0.0f;
            if (pitch_detector.process(samples, chunk->samples.size(), CHANNELS, detected_pitch_hz)) {
                g_detected_pitch_hz.store(detected_pitch_hz, std::memory_order_relaxed);
            }
        } else if (pitch_detection_was_enabled) {
            pitch_detector.reset();
            g_detected_pitch_hz.store(0.0f, std::memory_order_relaxed);
        }
        pitch_detection_was_enabled = pitch_detection_enabled;

        bool echo_enabled = g_echo_enabled.load(std::memory_order_relaxed);
        if (echo_enabled) {
            if (!echo_was_enabled) {
                echo.reset();
            }
            echo.process(samples, chunk->samples.size(),
                         g_echo_delay_ms.load(std::memory_order_relaxed),
                         g_echo_feedback.load(std::memory_order_relaxed),
                         g_echo_mix.load(std::memory_order_relaxed), SAMPLE_RATE);
        }
        echo_was_enabled = echo_enabled;

        if (g_preamp_enabled.load(std::memory_order_relaxed)) {
            preamp.process(samples, chunk->samples.size(),
                           g_preamp_gain_db.load(std::memory_order_relaxed));
        }

        bool high_pass_enabled = g_high_pass_enabled.load(std::memory_order_relaxed);
        if (high_pass_enabled) {
            if (!high_pass_was_enabled) {
                hpf.reset();
            }
            float high_pass_cutoff = g_high_pass_cutoff_freq.load(std::memory_order_relaxed);
            hpf.process(samples, chunk->samples.size(), high_pass_cutoff, SAMPLE_RATE);
        }
        high_pass_was_enabled = high_pass_enabled;

        bool drive_pedal_enabled = g_pedal_drive_enabled.load(std::memory_order_relaxed);
        int drive_model = g_pedal_drive_model.load(std::memory_order_relaxed);
        if (drive_pedal_enabled) {
            if (!drive_pedal_was_enabled || drive_model != previous_drive_model) {
                drive_pedal.reset();
            }
            drive_pedal.process(
                samples, chunk->samples.size(),
                static_cast<guitar_pedals::DriveModel>(drive_model),
                g_pedal_drive_amount.load(std::memory_order_relaxed),
                g_pedal_drive_tone.load(std::memory_order_relaxed), SAMPLE_RATE);
        }
        drive_pedal_was_enabled = drive_pedal_enabled;
        previous_drive_model = drive_model;

        bool low_pass_enabled = g_low_pass_enabled.load(std::memory_order_relaxed);
        if (low_pass_enabled) {
            if (!low_pass_was_enabled) {
                lpf.reset();
            }
            float current_cutoff = g_cutoff_freq.load(std::memory_order_relaxed);
            lpf.process(samples, chunk->samples.size(), current_cutoff, SAMPLE_RATE);
        }
        low_pass_was_enabled = low_pass_enabled;

        if (g_valve_compressor_enabled.load(std::memory_order_relaxed)) {
            compressor.process(
            samples, chunk->samples.size(),
                g_compressor_threshold_db.load(std::memory_order_relaxed),
                g_compressor_ratio.load(std::memory_order_relaxed),
                g_compressor_attack_ms.load(std::memory_order_relaxed),
                g_compressor_release_ms.load(std::memory_order_relaxed),
                g_compressor_drive.load(std::memory_order_relaxed),
                g_compressor_makeup_db.load(std::memory_order_relaxed), SAMPLE_RATE);
        }

        constexpr double pcm_scale = 2147483648.0;
        float trigger_level = g_scope_trigger_level.load(std::memory_order_relaxed);
        size_t scope_span_samples = WAVEFORM_POINTS;
        if (g_input_source.load(std::memory_order_relaxed) ==
            static_cast<int>(InputSource::chord_generator)) {
            int chord = std::clamp(g_chord_selection.load(std::memory_order_relaxed), 0, 2);
            size_t chord_period_samples = full_chord_period_samples(chord, SAMPLE_RATE);
            scope_span_samples = 2 * chord_period_samples + 1;
        }
        for (size_t i = 0; i < chunk->samples.size(); i += CHANNELS) {
            float input_sample = static_cast<float>(chunk->samples[i]) / 2147483648.0f;
            g_waveform.push_sample(input_sample, samples[i], trigger_level, scope_span_samples);

            for (size_t channel = 0; channel < CHANNELS; ++channel) {
                size_t sample_index = i + channel;
                double output = std::clamp(static_cast<double>(samples[sample_index]), -1.0, 1.0);
                double pcm_value = std::clamp(output * pcm_scale, -2147483648.0, 2147483647.0);
                chunk->samples[sample_index] = static_cast<int32_t>(std::llround(pcm_value));
            }
        }

        while (g_running.load(std::memory_order_relaxed) && !g_filter_to_play_q.push(std::move(chunk))) {
            std::this_thread::yield();
        }
    }
}

// --- Consumer Thread ---
void playback_consumer(snd_pcm_t* playback_handle) {
    while (g_running.load(std::memory_order_relaxed)) {
        auto chunk = g_filter_to_play_q.pop();
        if (!chunk) {
            std::this_thread::yield();
            continue;
        }

        int frames = snd_pcm_writei(playback_handle, chunk->samples.data(), PERIOD_SIZE);
        if (frames < 0) {
            // Track underruns (-EPIPE) or power suspend states (-ESTRPIPE)
            if (frames == -EPIPE || frames == -ESTRPIPE) {
                g_xrun_count.fetch_add(1, std::memory_order_relaxed);
            }

            // Use snd_pcm_recover instead of a blind snd_pcm_prepare
            int err = snd_pcm_recover(playback_handle, frames, 0);
            if (err < 0) {
                fprintf(stderr, "Fatal ALSA playback write error (%s), recovery failed (%s)\n",
                        snd_strerror(frames), snd_strerror(err));
                g_running.store(false, std::memory_order_relaxed);
                return;
            }
        }

        // Return the used chunk back to the free pool so the producer can reuse it.
        // Wrapped in a loop with yield() to handle a temporarily full free pool safely.
        while (g_running.load(std::memory_order_relaxed) && !g_free_pool_q.push(std::move(chunk))) {
            std::this_thread::yield();
        }
    }
}

int main() {
    // 1. Setup ALSA
    snd_pcm_t *capture_handle = nullptr;
    snd_pcm_t *playback_handle = nullptr;
    //const char *device = "plughw:CARD=Device,DEV=0";
    //const char *device = "hw:CARD=TT,DEV=0";
    const char *device = "default";
    int err = snd_pcm_open(&capture_handle, device, SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        fprintf(stderr, "Failed to open ALSA capture device %s: %s\n", device, snd_strerror(err));
        return 1;
    }

    err = snd_pcm_open(&playback_handle, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "Failed to open ALSA playback device %s: %s\n", device, snd_strerror(err));
        snd_pcm_close(capture_handle);
        return 1;
    }

    err = snd_pcm_set_params(capture_handle, SND_PCM_FORMAT_S32_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                             CHANNELS, SAMPLE_RATE, 1, 10000);
    if (err < 0) {
        fprintf(stderr, "Failed to configure ALSA capture as S32_LE, %d Hz, %d channels: %s\n",
                SAMPLE_RATE, CHANNELS, snd_strerror(err));
        snd_pcm_close(capture_handle);
        snd_pcm_close(playback_handle);
        return 1;
    }

    err = snd_pcm_set_params(playback_handle, SND_PCM_FORMAT_S32_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                             CHANNELS, SAMPLE_RATE, 1, 10000);
    if (err < 0) {
        fprintf(stderr, "Failed to configure ALSA playback as S32_LE, %d Hz, %d channels: %s\n",
                SAMPLE_RATE, CHANNELS, snd_strerror(err));
        snd_pcm_close(capture_handle);
        snd_pcm_close(playback_handle);
        return 1;
    }

    snd_pcm_uframes_t buffer_size = 0;

    // Query playback handle parameters
    err = snd_pcm_get_params(playback_handle, &buffer_size, &PERIOD_SIZE);
    if (err < 0) {
        fprintf(stderr, "Failed to get PCM parameters: %s\n", snd_strerror(err));
        snd_pcm_close(capture_handle);
        snd_pcm_close(playback_handle);
        return 1;
    } else {
        std::printf("[ALSA Playback] Negotiated Buffer Size: %lu frames, Period Size: %lu frames\n", 
                    buffer_size, PERIOD_SIZE);
    }
    SAMPLES_PER_PERIOD = (PERIOD_SIZE * CHANNELS);

    // Preallocate buffers after ALSA reports the actual period size.
    for (size_t i = 0; i < QUEUE_CAPACITY - 1; ++i) {
        g_free_pool_q.push(std::make_unique<AudioChunk>(SAMPLES_PER_PERIOD));
    }

    std::string wav_error;
    //const char *wavefile = "danano.wav";
    //const char *wavefile = "rhythm.wav";
    const char *wavefile = "solo_1.wav";
    auto startup_wav = WavGenerator::load(wavefile, SAMPLE_RATE, wav_error);
    if (startup_wav) {
        size_t startup_frames = startup_wav->frame_count();
        std::atomic_store_explicit(&g_loaded_wav, startup_wav, std::memory_order_release);
        g_input_source.store(static_cast<int>(InputSource::wav_file), std::memory_order_relaxed);
        std::printf("Loaded danano.wav: %.2f seconds, looping playback enabled\n",
                    static_cast<double>(startup_frames) / SAMPLE_RATE);
        g_wav_status = "Playing danano.wav (looping)";
    } else {
        std::fprintf(stderr, "Could not load danano.wav: %s\n", wav_error.c_str());
        g_input_source.store(static_cast<int>(InputSource::live_audio_in), std::memory_order_relaxed);
        g_wav_status = "danano.wav not loaded; using live capture";
    }

    err = snd_pcm_start(capture_handle);
    if (err < 0) {
        fprintf(stderr, "Failed to start ALSA capture: %s\n", snd_strerror(err));
        snd_pcm_close(capture_handle);
        snd_pcm_close(playback_handle);
        return 1;
    }

    // 2. Start Pipeline Threads
    std::thread producer(capture_producer, capture_handle);
    std::thread filter(filter_processor);
    std::thread consumer(playback_consumer, playback_handle);

    // 4. GUI Loop
    if (!glfwInit()) return 1;
    GLFWwindow* window = glfwCreateWindow(780, 560, "SPSC Queue Pipeline & Depth Histogram", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    int display_w = 0, display_h = 0;
    float queue_depths[3] = {0.0f, 0.0f, 0.0f};
    bool show_queue_diagnostics = true;
    
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(760, 520), ImGuiCond_Once);

        ImGui::Begin("Audio Control & Queue Histogram");

        static const char* input_source_names[] = {
            "Live Audio In", "WAV File", "Chord Generator"
        };
        int input_source = g_input_source.load(std::memory_order_relaxed);
        if (ImGui::Combo("Input Source", &input_source,
                         input_source_names, IM_ARRAYSIZE(input_source_names))) {
            g_input_source.store(input_source, std::memory_order_relaxed);
        }

        if (input_source == static_cast<int>(InputSource::wav_file)) {
            static char wav_path[512] = "danano.wav";
            ImGui::InputText("WAV file path", wav_path, sizeof(wav_path));
            ImGui::SameLine();
            if (ImGui::Button("Load WAV")) {
                std::string error;
                auto clip = WavGenerator::load(wav_path, SAMPLE_RATE, error);
                if (clip) {
                    std::atomic_store_explicit(&g_loaded_wav, clip, std::memory_order_release);
                    g_input_source.store(static_cast<int>(InputSource::wav_file), std::memory_order_relaxed);
                    input_source = static_cast<int>(InputSource::wav_file);
                    g_wav_status = "Loaded and playing " + std::string(wav_path);
                } else {
                    g_wav_status = "Load failed: " + error;
                }
            }
            ImGui::Text("%s", g_wav_status.c_str());
            bool wav_looping = g_wav_looping.load(std::memory_order_relaxed);
            if (ImGui::Checkbox("Loop WAV", &wav_looping)) {
                g_wav_looping.store(wav_looping, std::memory_order_relaxed);
            }
        } else if (input_source == static_cast<int>(InputSource::chord_generator)) {
            static const char* chord_names[] = {"E Major", "A Minor", "G Major"};
            int chord = g_chord_selection.load(std::memory_order_relaxed);
            if (ImGui::Combo("Generated Chord", &chord, chord_names, IM_ARRAYSIZE(chord_names))) {
                g_chord_selection.store(chord, std::memory_order_relaxed);
            }
            size_t full_period_samples = full_chord_period_samples(chord, SAMPLE_RATE);
            ImGui::Text("Scope: 2 full-chord phase-aligned cycles (%.2f s)",
                        2.0 * full_period_samples / SAMPLE_RATE);
        }

        // 1. Waveform Plot
        g_waveform.get_latest_frame(g_ui_input_waveform, g_ui_output_waveform);
        ImGui::Text("Input");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.25f, 0.8f, 0.95f, 1.0f), "Input");
        ImGui::SameLine();
        ImGui::Text("  |  ");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Output");

        ImGui::InvisibleButton("Waveform##scope", ImVec2(-1.0f, 140.0f));
        ImVec2 plot_min = ImGui::GetItemRectMin();
        ImVec2 plot_max = ImGui::GetItemRectMax();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(plot_min, plot_max, IM_COL32(18, 24, 29, 255));
        for (int division = 0; division <= 4; ++division) {
            float x = plot_min.x + (plot_max.x - plot_min.x) * division / 4.0f;
            draw_list->AddLine(ImVec2(x, plot_min.y), ImVec2(x, plot_max.y), IM_COL32(55, 65, 72, 255));
        }
        for (int division = 0; division <= 4; ++division) {
            float y = plot_min.y + (plot_max.y - plot_min.y) * division / 4.0f;
            ImU32 grid_color = division == 2 ? IM_COL32(95, 105, 112, 255) : IM_COL32(55, 65, 72, 255);
            draw_list->AddLine(ImVec2(plot_min.x, y), ImVec2(plot_max.x, y), grid_color);
        }

        draw_list->PushClipRect(plot_min, plot_max, true);
        float plot_height = plot_max.y - plot_min.y;
        for (size_t i = 1; i < WAVEFORM_POINTS; ++i) {
            float x0 = plot_min.x + (plot_max.x - plot_min.x) * (i - 1) / (WAVEFORM_POINTS - 1);
            float x1 = plot_min.x + (plot_max.x - plot_min.x) * i / (WAVEFORM_POINTS - 1);
            float input_y0 = plot_min.y + (1.0f - g_ui_input_waveform[i - 1]) * 0.5f * plot_height;
            float input_y1 = plot_min.y + (1.0f - g_ui_input_waveform[i]) * 0.5f * plot_height;
            float output_y0 = plot_min.y + (1.0f - g_ui_output_waveform[i - 1]) * 0.5f * plot_height;
            float output_y1 = plot_min.y + (1.0f - g_ui_output_waveform[i]) * 0.5f * plot_height;
            draw_list->AddLine(ImVec2(x0, input_y0), ImVec2(x1, input_y1), IM_COL32(64, 204, 242, 255), 1.5f);
            draw_list->AddLine(ImVec2(x0, output_y0), ImVec2(x1, output_y1), IM_COL32(255, 140, 64, 255), 1.5f);
        }
        draw_list->PopClipRect();
/*
        bool pitch_detection_enabled = g_pitch_detection_enabled.load(std::memory_order_relaxed);
        if (ImGui::Checkbox("Pitch Detection", &pitch_detection_enabled)) {
            g_pitch_detection_enabled.store(pitch_detection_enabled, std::memory_order_relaxed);
            if (!pitch_detection_enabled) {
                g_detected_pitch_hz.store(0.0f, std::memory_order_relaxed);
            }
        }

        float pitch_hz = g_detected_pitch_hz.load(std::memory_order_relaxed);
        if (pitch_detection_enabled && pitch_hz > 0.0f) {
            ImGui::Text("Pitch: %.1f Hz", pitch_hz);
        } else if (pitch_detection_enabled) {
            ImGui::Text("Pitch: --");
        } else {
            ImGui::Text("Pitch: disabled");
        }
*/
        float trigger_level = g_scope_trigger_level.load(std::memory_order_relaxed);
        if (ImGui::SliderFloat("Trigger Level", &trigger_level, -0.9f, 0.9f, "%.2f")) {
            g_scope_trigger_level.store(trigger_level, std::memory_order_relaxed);
        }

        ImGui::Separator();
        ImGui::Checkbox("Queue diagnostics", &show_queue_diagnostics);

        if (show_queue_diagnostics) {
            queue_depths[0] = static_cast<float>(g_free_pool_q.size());
            queue_depths[1] = static_cast<float>(g_cap_to_filter_q.size());
            queue_depths[2] = static_cast<float>(g_filter_to_play_q.size());
            size_t queued_chunks = static_cast<size_t>(queue_depths[0] + queue_depths[1] + queue_depths[2]);
            size_t live_chunks = g_live_chunks.load(std::memory_order_relaxed);
            size_t unqueued_chunks = live_chunks > queued_chunks ? live_chunks - queued_chunks : 0;

            uint64_t current_xruns = g_xrun_count.load(std::memory_order_relaxed);
            uint64_t dropped_capture_reads = g_dropped_capture_reads.load(std::memory_order_relaxed);
            ImGui::Text("XRUNs: %llu", static_cast<unsigned long long>(current_xruns));
            ImGui::Text("Capture reads dropped (pool empty): %llu",
                static_cast<unsigned long long>(dropped_capture_reads));
            ImGui::Text("Buffers: %zu alive | %zu queued | %zu held by workers",
                live_chunks, queued_chunks, unqueued_chunks);

            ImGui::Text("Queue Capacities (Max per Queue: %d):", QUEUE_CAPACITY);
            ImGui::PlotHistogram("##QueueHistogram", queue_depths, 3, 0,
                     "0: Free Pool  |  1: Capture->Filter  |  2: Filter->Playback",
                     0.0f, static_cast<float>(QUEUE_CAPACITY), ImVec2(0, 120.0f));

            ImGui::Text("Live Counts -> Free Pool: %.0f | Cap->Filt: %.0f | Filt->Play: %.0f",
                queue_depths[0], queue_depths[1], queue_depths[2]);

            ImGui::Separator();
        }
        static int selected_guitar_preset = 0;
        static int selected_echo_preset = 0;
        static int selected_preamp_preset = 1;
        static int selected_high_pass_preset = 1;
        static int selected_drive_preset = 0;
        static int selected_low_pass_preset = 3;
        static int selected_compressor_preset = 0;
        bool controls_changed = false;
        bool compressor_controls_changed = false;
        {
        static const char* guitar_preset_names[] = {
            "Custom",
            "MXR Micro Amp-Style Clean Boost",
            "Boss CS-3-Style Compression",
            "Ibanez TS808-Style Overdrive",
            "Pro Co RAT-Style Distortion",
            "Electro-Harmonix Big Muff Pi-Style Fuzz"
        };
        if (ImGui::Combo("Pedal Preset", &selected_guitar_preset,
                         guitar_preset_names, IM_ARRAYSIZE(guitar_preset_names))) {
            switch (selected_guitar_preset) {
            case 1:
                selected_echo_preset = 0;
                selected_preamp_preset = 2;
                selected_high_pass_preset = 1;
                selected_drive_preset = 0;
                selected_low_pass_preset = 1;
                selected_compressor_preset = 0;
                g_echo_enabled.store(false, std::memory_order_relaxed);
                g_preamp_enabled.store(true, std::memory_order_relaxed);
                g_preamp_gain_db.store(6.0f, std::memory_order_relaxed);
                g_high_pass_enabled.store(true, std::memory_order_relaxed);
                g_high_pass_cutoff_freq.store(70.0f, std::memory_order_relaxed);
                g_low_pass_enabled.store(true, std::memory_order_relaxed);
                g_cutoff_freq.store(12000.0f, std::memory_order_relaxed);
                g_pedal_drive_enabled.store(false, std::memory_order_relaxed);
                g_valve_compressor_enabled.store(false, std::memory_order_relaxed);
                g_compressor_threshold_db.store(-22.0f, std::memory_order_relaxed);
                g_compressor_ratio.store(2.0f, std::memory_order_relaxed);
                g_compressor_attack_ms.store(20.0f, std::memory_order_relaxed);
                g_compressor_release_ms.store(120.0f, std::memory_order_relaxed);
                g_compressor_drive.store(1.1f, std::memory_order_relaxed);
                g_compressor_makeup_db.store(0.0f, std::memory_order_relaxed);
                break;
            case 2:
                selected_echo_preset = 0;
                selected_preamp_preset = 1;
                selected_high_pass_preset = 1;
                selected_drive_preset = 0;
                selected_low_pass_preset = 2;
                selected_compressor_preset = 2;
                g_echo_enabled.store(false, std::memory_order_relaxed);
                g_preamp_enabled.store(true, std::memory_order_relaxed);
                g_preamp_gain_db.store(0.0f, std::memory_order_relaxed);
                g_high_pass_enabled.store(true, std::memory_order_relaxed);
                g_high_pass_cutoff_freq.store(70.0f, std::memory_order_relaxed);
                g_low_pass_enabled.store(true, std::memory_order_relaxed);
                g_cutoff_freq.store(10000.0f, std::memory_order_relaxed);
                g_pedal_drive_enabled.store(false, std::memory_order_relaxed);
                g_valve_compressor_enabled.store(true, std::memory_order_relaxed);
                g_compressor_threshold_db.store(-26.0f, std::memory_order_relaxed);
                g_compressor_ratio.store(4.0f, std::memory_order_relaxed);
                g_compressor_attack_ms.store(12.0f, std::memory_order_relaxed);
                g_compressor_release_ms.store(160.0f, std::memory_order_relaxed);
                g_compressor_drive.store(1.2f, std::memory_order_relaxed);
                g_compressor_makeup_db.store(3.0f, std::memory_order_relaxed);
                break;
            case 3:
                selected_echo_preset = 0;
                selected_preamp_preset = 2;
                selected_high_pass_preset = 2;
                selected_drive_preset = 1;
                selected_low_pass_preset = 4;
                selected_compressor_preset = 0;
                g_echo_enabled.store(false, std::memory_order_relaxed);
                g_preamp_enabled.store(true, std::memory_order_relaxed);
                g_preamp_gain_db.store(6.0f, std::memory_order_relaxed);
                g_high_pass_enabled.store(true, std::memory_order_relaxed);
                g_high_pass_cutoff_freq.store(100.0f, std::memory_order_relaxed);
                g_low_pass_enabled.store(true, std::memory_order_relaxed);
                g_cutoff_freq.store(6500.0f, std::memory_order_relaxed);
                g_pedal_drive_enabled.store(true, std::memory_order_relaxed);
                g_pedal_drive_model.store(static_cast<int>(guitar_pedals::DriveModel::tube_screamer), std::memory_order_relaxed);
                g_pedal_drive_amount.store(3.5f, std::memory_order_relaxed);
                g_pedal_drive_tone.store(0.65f, std::memory_order_relaxed);
                g_valve_compressor_enabled.store(false, std::memory_order_relaxed);
                g_compressor_threshold_db.store(-20.0f, std::memory_order_relaxed);
                g_compressor_ratio.store(2.0f, std::memory_order_relaxed);
                g_compressor_attack_ms.store(15.0f, std::memory_order_relaxed);
                g_compressor_release_ms.store(100.0f, std::memory_order_relaxed);
                g_compressor_drive.store(1.5f, std::memory_order_relaxed);
                g_compressor_makeup_db.store(1.0f, std::memory_order_relaxed);
                break;
            case 4:
                selected_echo_preset = 0;
                selected_preamp_preset = 3;
                selected_high_pass_preset = 2;
                selected_drive_preset = 2;
                selected_low_pass_preset = 5;
                selected_compressor_preset = 0;
                g_echo_enabled.store(false, std::memory_order_relaxed);
                g_preamp_enabled.store(true, std::memory_order_relaxed);
                g_preamp_gain_db.store(9.0f, std::memory_order_relaxed);
                g_high_pass_enabled.store(true, std::memory_order_relaxed);
                g_high_pass_cutoff_freq.store(100.0f, std::memory_order_relaxed);
                g_low_pass_enabled.store(true, std::memory_order_relaxed);
                g_cutoff_freq.store(5500.0f, std::memory_order_relaxed);
                g_pedal_drive_enabled.store(true, std::memory_order_relaxed);
                g_pedal_drive_model.store(static_cast<int>(guitar_pedals::DriveModel::rat), std::memory_order_relaxed);
                g_pedal_drive_amount.store(4.0f, std::memory_order_relaxed);
                g_pedal_drive_tone.store(0.5f, std::memory_order_relaxed);
                g_valve_compressor_enabled.store(false, std::memory_order_relaxed);
                g_compressor_threshold_db.store(-24.0f, std::memory_order_relaxed);
                g_compressor_ratio.store(3.5f, std::memory_order_relaxed);
                g_compressor_attack_ms.store(6.0f, std::memory_order_relaxed);
                g_compressor_release_ms.store(90.0f, std::memory_order_relaxed);
                g_compressor_drive.store(2.5f, std::memory_order_relaxed);
                g_compressor_makeup_db.store(0.5f, std::memory_order_relaxed);
                break;
            case 5:
                selected_echo_preset = 0;
                selected_preamp_preset = 4;
                selected_high_pass_preset = 1;
                selected_drive_preset = 3;
                selected_low_pass_preset = 6;
                selected_compressor_preset = 0;
                g_echo_enabled.store(false, std::memory_order_relaxed);
                g_preamp_enabled.store(true, std::memory_order_relaxed);
                g_preamp_gain_db.store(12.0f, std::memory_order_relaxed);
                g_high_pass_enabled.store(true, std::memory_order_relaxed);
                g_high_pass_cutoff_freq.store(70.0f, std::memory_order_relaxed);
                g_low_pass_enabled.store(true, std::memory_order_relaxed);
                g_cutoff_freq.store(5000.0f, std::memory_order_relaxed);
                g_pedal_drive_enabled.store(true, std::memory_order_relaxed);
                g_pedal_drive_model.store(static_cast<int>(guitar_pedals::DriveModel::big_muff), std::memory_order_relaxed);
                g_pedal_drive_amount.store(5.5f, std::memory_order_relaxed);
                g_pedal_drive_tone.store(0.45f, std::memory_order_relaxed);
                g_valve_compressor_enabled.store(false, std::memory_order_relaxed);
                g_compressor_threshold_db.store(-28.0f, std::memory_order_relaxed);
                g_compressor_ratio.store(4.5f, std::memory_order_relaxed);
                g_compressor_attack_ms.store(3.0f, std::memory_order_relaxed);
                g_compressor_release_ms.store(120.0f, std::memory_order_relaxed);
                g_compressor_drive.store(3.5f, std::memory_order_relaxed);
                g_compressor_makeup_db.store(0.0f, std::memory_order_relaxed);
                break;
            }
        }

        }

        ImGui::Separator();

        EchoEffect::render(g_echo_enabled, g_echo_delay_ms, g_echo_feedback, g_echo_mix,
                           selected_echo_preset, controls_changed);

        Preamp::render(g_preamp_enabled, g_preamp_gain_db,
                       selected_preamp_preset, controls_changed);
        DynamicHighPassFilter::render(g_high_pass_enabled, g_high_pass_cutoff_freq,
                                      selected_high_pass_preset, controls_changed);

        guitar_pedals::DrivePedal::render(g_pedal_drive_enabled, g_pedal_drive_model,
                                          g_pedal_drive_amount, g_pedal_drive_tone,
                                          selected_drive_preset, controls_changed);
        DynamicLowPassFilter::render(g_low_pass_enabled, g_cutoff_freq,
                                     selected_low_pass_preset, controls_changed);

        ValveCompressor::render(g_valve_compressor_enabled,
                                g_compressor_threshold_db, g_compressor_ratio,
                                g_compressor_attack_ms, g_compressor_release_ms,
                                g_compressor_drive, g_compressor_makeup_db,
                                selected_compressor_preset, controls_changed,
                                compressor_controls_changed);
        if (compressor_controls_changed) {
            selected_compressor_preset = 0;
        }
        if (controls_changed) {
            selected_guitar_preset = 0;
        }

        ImGui::End();

        ImGui::Render();
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    g_running.store(false, std::memory_order_relaxed);
    producer.join();
    filter.join();
    consumer.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    snd_pcm_close(capture_handle);
    snd_pcm_close(playback_handle);

    return 0;
}