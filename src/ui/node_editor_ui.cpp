#include "node_editor_ui.hpp"

#include "audio/audio_devices.hpp"
#include "audio/pipeline.hpp"
#include "dsp/chord_catalog.hpp"
#include "dsp/module.hpp"
#include "graph/graph_io.hpp"
#include "graph/visualizer_bus.hpp"
#include "paths.hpp"

#include "imgui.h"
#include "imfilebrowser.h"
#include "imnodes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

// ImNodes sizes a node to its widest row. Every body row (params + "out") must
// share this width so the right-aligned "out" sits against the output pin.
constexpr float kNodeWidth = 140.0f;

// Right-align "out" on a fixed-width row (Dummy + SameLine). Use ImGui text so it
// stays on the imnodes node foreground channel (raw DrawList can land under chrome).
void draw_output_label(float node_width) {
    const float label_w = ImGui::CalcTextSize("out").x;
    ImGui::Dummy(ImVec2(std::max(0.0f, node_width - label_w), ImGui::GetTextLineHeight()));
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::TextUnformatted("out");
}

ImGui::FileBrowser& graph_open_browser() {
    static ImGui::FileBrowser browser(ImGuiFileBrowserFlags_CloseOnEsc |
                                      ImGuiFileBrowserFlags_ConfirmOnEnter |
                                      ImGuiFileBrowserFlags_EditPathString);
    static bool configured = false;
    if (!configured) {
        browser.SetTitle("Load Graph");
        browser.SetTypeFilters({".json"});
        configured = true;
    }
    return browser;
}

ImGui::FileBrowser& graph_save_browser() {
    static ImGui::FileBrowser browser(ImGuiFileBrowserFlags_EnterNewFilename |
                                      ImGuiFileBrowserFlags_CreateNewDir |
                                      ImGuiFileBrowserFlags_CloseOnEsc |
                                      ImGuiFileBrowserFlags_ConfirmOnEnter |
                                      ImGuiFileBrowserFlags_EditPathString);
    static bool configured = false;
    if (!configured) {
        browser.SetTitle("Save Graph");
        browser.SetTypeFilters({".json"});
        configured = true;
    }
    return browser;
}

ImGui::FileBrowser& wav_open_browser() {
    static ImGui::FileBrowser browser(ImGuiFileBrowserFlags_CloseOnEsc |
                                      ImGuiFileBrowserFlags_ConfirmOnEnter |
                                      ImGuiFileBrowserFlags_EditPathString);
    static bool configured = false;
    if (!configured) {
        browser.SetTitle("Load WAV");
        browser.SetTypeFilters({".wav", ".WAV"});
        configured = true;
    }
    return browser;
}

// Target node id for the WAV file picker (-1 = none / legacy toolbar unused).
int& wav_picker_node_id() {
    static int id = -1;
    return id;
}

void copy_path_to_buf(char* buf, size_t n, const std::filesystem::path& path) {
    std::snprintf(buf, n, "%s", path.string().c_str());
}

void open_browser_near_path(ImGui::FileBrowser& browser, const char* path_hint,
                            bool enter_filename = false) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path hint = path_hint ? fs::path(path_hint) : fs::path{};
    if (!hint.empty()) {
        if (fs::is_directory(hint, ec)) {
            browser.SetDirectory(hint);
        } else if (hint.has_parent_path() && fs::is_directory(hint.parent_path(), ec)) {
            browser.SetDirectory(hint.parent_path());
        }
        if (enter_filename && hint.has_filename() && !fs::is_directory(hint, ec)) {
            browser.SetInputName(hint.filename().string());
        }
    }
    browser.Open();
}

} // namespace

static float linear_to_db(float linear) {
    const float clamped = std::max(linear, 1.0e-6f);
    return 20.0f * std::log10(clamped);
}

static void draw_vu_widget(int node_id) {
    float peak = 0.0f;
    float rms = 0.0f;
    g_visualizer_bus.read_vu(node_id, peak, rms);
    const float peak_db = linear_to_db(peak);
    const float rms_db = linear_to_db(rms);
    ImGui::Text("Peak %.1f dB", peak_db);
    ImGui::ProgressBar(std::clamp(peak, 0.0f, 1.0f), ImVec2(kNodeWidth, 0.0f), "");
    ImGui::Text("RMS  %.1f dB", rms_db);
    ImGui::ProgressBar(std::clamp(rms * 1.5f, 0.0f, 1.0f), ImVec2(kNodeWidth, 0.0f), "");
}

static void draw_scope_widget(int node_id) {
    float samples[kVisualizerScopePoints]{};
    g_visualizer_bus.read_scope(node_id, samples, kVisualizerScopePoints);
    ImGui::PlotLines("##scope", samples, kVisualizerScopePoints, 0, nullptr, -1.0f, 1.0f,
                     ImVec2(kNodeWidth, 48.0f));
}

static void draw_spectrum_widget(int node_id) {
    float bins[kVisualizerSpectrumBins]{};
    g_visualizer_bus.read_spectrum(node_id, bins, kVisualizerSpectrumBins);
    ImGui::PlotHistogram("##spectrum", bins, kVisualizerSpectrumBins, 0, nullptr, 0.0f, 1.0f,
                         ImVec2(kNodeWidth, 48.0f));
}

static void draw_pitch_widget(int node_id) {
    float frequency_hz = 0.0f;
    g_visualizer_bus.read_pitch(node_id, frequency_hz);
    char note[16];
    pitch_frequency_to_note(frequency_hz, note, sizeof(note));
    if (frequency_hz > 0.0f) {
        ImGui::Text("Freq  %.1f Hz", frequency_hz);
        ImGui::Text("Note  %s", note);
    } else {
        ImGui::TextUnformatted("Freq  --");
        ImGui::TextUnformatted("Note  --");
    }
}

void node_editor_init() {
    ImNodes::CreateContext();
    ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;
    ImNodes::GetStyle().Flags |= ImNodesStyleFlags_GridLines;
    // Tight pad: "out" ends at content Max.x; pin is at Max.x + PinOffset.
    // A 1px inward pull closes the optical gap without covering the label.
    ImNodes::GetStyle().NodePadding = ImVec2(2.0f, 8.0f);
    ImNodes::GetStyle().PinOffset = -1.0f;
}

void node_editor_shutdown() {
    ImNodes::DestroyContext();
}

void node_editor_publish(AudioGraph& graph) {
    graph.publish(graph.compile());
}

static bool draw_device_combo(const char* label, std::string& device,
                              const std::vector<AudioDevice>& devices,
                              std::vector<const char*>& labels) {
    if (labels.size() != devices.size()) {
        labels.clear();
        labels.reserve(devices.size());
        for (const auto& d : devices) {
            labels.push_back(d.label.c_str());
        }
    }
    int current = 0;
    for (size_t i = 0; i < devices.size(); ++i) {
        if (devices[i].id == device) {
            current = static_cast<int>(i);
            break;
        }
    }
    if (devices.empty()) {
        return false;
    }
    if (ImGui::Combo(label, &current, labels.data(), static_cast<int>(labels.size()))) {
        device = devices[static_cast<size_t>(current)].id;
        return true;
    }
    return false;
}

static void draw_device_error(const char* stream_prefix) {
    const std::string& error = g_pipeline.last_device_error;
    if (error.rfind(stream_prefix, 0) != 0) {
        return;
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kNodeWidth);
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", error.c_str());
    ImGui::PopTextWrapPos();
}

static void render_module(Module& module, int node_id) {
    ImGui::PushID(node_id);
    ImGui::PushItemWidth(kNodeWidth);
    module.render();
    ImGui::PopItemWidth();
    ImGui::PopID();
}

static void draw_node_params(GraphNode& node, AudioGraph& graph) {
    NodeState& st = node.state();
    switch (node.type()) {
    case NodeType::wav_player:
        if (auto* m = st.as<node_params::WavPlayer>()) {
            // File picker uses shared browser state — keep the "..." button here.
            const char* slash = std::strrchr(m->path().c_str(), '/');
            const char* leaf = slash ? slash + 1 : m->path().c_str();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kNodeWidth - 28.0f);
            ImGui::TextUnformatted(leaf[0] ? leaf : "(no file)");
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered() && !m->path().empty()) {
                ImGui::SetTooltip("%s", m->path().c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("...")) {
                wav_picker_node_id() = node.id();
                open_browser_near_path(wav_open_browser(), m->path().c_str());
            }
            render_module(*m, node.id());
        }
        break;
    case NodeType::audio_input:
        if (auto* m = st.as<node_params::AudioInput>()) {
            static std::vector<AudioDevice> devices = list_capture_devices();
            static std::vector<const char*> labels;
            ImGui::TextUnformatted("Device");
            ImGui::SetNextItemWidth(kNodeWidth);
            if (draw_device_combo("##cap_dev", m->device, devices, labels)) {
                std::string err;
                if (!g_pipeline.set_capture_device(m->device.c_str(), err)) {
                    m->device = g_pipeline.capture_device();
                }
            }
            draw_device_error("Capture");
            if (g_pipeline.offline_mode) {
                ImGui::TextDisabled("offline");
            }
        }
        break;
    case NodeType::chord_generator:
        if (auto* m = st.as<node_params::Chord>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::white_noise:
        if (auto* m = st.as<node_params::WhiteNoise>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::echo:
        if (auto* m = st.as<node_params::Echo>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::preamp:
        if (auto* m = st.as<node_params::Preamp>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::high_pass:
        if (auto* m = st.as<node_params::HighPass>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::drive_pedal:
        if (auto* m = st.as<node_params::Drive>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::low_pass:
        if (auto* m = st.as<node_params::LowPass>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::compressor:
        if (auto* m = st.as<node_params::Compressor>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::noise_gate:
        if (auto* m = st.as<node_params::NoiseGate>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::eq_three_band:
        if (auto* m = st.as<node_params::EqThreeBand>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::reverb:
        if (auto* m = st.as<node_params::Reverb>()) {
            render_module(*m, node.id());
        }
        break;
    case NodeType::dry_wet_mix:
        if (auto* m = st.as<node_params::DryWet>()) {
            render_module(*m, node.id());
        }
        ImGui::TextDisabled("dry / wet");
        break;
    case NodeType::wav_recorder:
        if (auto* m = st.as<node_params::WavRecorder>()) {
            render_module(*m, node.id());
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kNodeWidth);
        ImGui::TextUnformatted(graph.record_path().c_str());
        ImGui::PopTextWrapPos();
        break;
    case NodeType::vu_meter:
        if (auto* m = st.as<node_params::VuMeter>()) {
            render_module(*m, node.id());
        }
        draw_vu_widget(node.id());
        break;
    case NodeType::oscilloscope:
        if (auto* m = st.as<node_params::Oscilloscope>()) {
            render_module(*m, node.id());
        }
        draw_scope_widget(node.id());
        break;
    case NodeType::spectrum:
        if (auto* m = st.as<node_params::Spectrum>()) {
            render_module(*m, node.id());
        }
        draw_spectrum_widget(node.id());
        break;
    case NodeType::pitch_detector:
        if (auto* m = st.as<node_params::PitchDetector>()) {
            render_module(*m, node.id());
        }
        draw_pitch_widget(node.id());
        break;
    case NodeType::mixer:
        if (auto* m = st.as<node_params::Mixer>()) {
            m->ui_slots = std::max(1, graph.mixer_pin_count(node.id()));
            render_module(*m, node.id());
        }
        break;
    case NodeType::output:
        if (auto* m = st.as<node_params::AudioOutput>()) {
            static std::vector<AudioDevice> devices = list_playback_devices();
            static std::vector<const char*> labels;
            ImGui::TextUnformatted("Device");
            ImGui::SetNextItemWidth(kNodeWidth);
            if (draw_device_combo("##play_dev", m->device, devices, labels)) {
                std::string err;
                if (!g_pipeline.set_playback_device(m->device.c_str(), err)) {
                    m->device = g_pipeline.playback_device();
                }
            }
            draw_device_error("Playback");
            if (g_pipeline.offline_mode) {
                ImGui::TextDisabled("offline");
            }
        }
        break;
    default:
        break;
    }
}

static void apply_node_positions(AudioGraph& graph) {
    for (const auto& node : graph.nodes()) {
        ImNodes::SetNodeGridSpacePos(node.id(), ImVec2(node.x(), node.y()));
    }
}

static void sync_node_positions(AudioGraph& graph) {
    for (auto& node : graph.nodes()) {
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.id());
        node.set_position(pos.x, pos.y);
    }
}

static void draw_toolbar(AudioGraph& graph, NodeEditorUiState& state) {
    ImGui::TextUnformatted("Audio Graph");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("##graph_path", state.graph_path, sizeof(state.graph_path));
    ImGui::SameLine();
    if (ImGui::Button("Save Graph")) {
        open_browser_near_path(graph_save_browser(), state.graph_path, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load Graph")) {
        open_browser_near_path(graph_open_browser(), state.graph_path);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Default")) {
        graph.reset_to_default_chain();
        std::string save_error;
        if (graph_io::save_to_file(graph, kDefaultPatchPath, save_error)) {
            std::snprintf(state.graph_path, sizeof(state.graph_path), "%s", kDefaultPatchPath);
            state.status_message =
                std::string("Reset to default graph (saved ") + kDefaultPatchPath + ")";
        } else {
            state.status_message = "Reset to default graph";
        }
        state.positions_need_apply = true;
        node_editor_publish(graph);
    }

    ImGui::Checkbox("Diagnostics", &state.show_diagnostics);

    if (!state.status_message.empty()) {
        ImGui::TextWrapped("%s", state.status_message.c_str());
    }
    ImGui::TextDisabled(
        "Right-click canvas to add nodes. Wire freely (fan-in sums, fan-out duplicates). "
        "Ctrl+drag pin to detach. Title-bar Byp toggles bypass. "
        "Mixer mute checkbox silences a slot. Delete removes selection.");
}

static void process_file_browsers(AudioGraph& graph, NodeEditorUiState& state) {
    auto& open_graph = graph_open_browser();
    open_graph.Display();
    if (open_graph.HasSelected()) {
        copy_path_to_buf(state.graph_path, sizeof(state.graph_path), open_graph.GetSelected());
        open_graph.ClearSelected();
        std::string error;
        if (graph_io::load_from_file(graph, state.graph_path, error)) {
            state.positions_need_apply = true;
            // Apply first Audio Input device if present; WAV paths live on WavPlayer nodes.
            for (const auto& n : graph.nodes()) {
                if (n.type() == NodeType::audio_input) {
                    if (const auto* p = n.state().as<node_params::AudioInput>()) {
                        std::string err;
                        g_pipeline.set_capture_device(p->device.c_str(), err);
                    }
                }
                if (n.type() == NodeType::output) {
                    if (const auto* p = n.state().as<node_params::AudioOutput>()) {
                        std::string err;
                        g_pipeline.set_playback_device(p->device.c_str(), err);
                    }
                }
            }
            node_editor_publish(graph);
            state.status_message = std::string("Loaded ") + state.graph_path;
            if (!g_pipeline.last_device_error.empty()) {
                state.status_message += " | " + g_pipeline.last_device_error;
            }
        } else {
            state.status_message = error;
        }
    }

    auto& save_graph = graph_save_browser();
    save_graph.Display();
    if (save_graph.HasSelected()) {
        copy_path_to_buf(state.graph_path, sizeof(state.graph_path), save_graph.GetSelected());
        save_graph.ClearSelected();
        sync_node_positions(graph);
        std::string error;
        if (graph_io::save_to_file(graph, state.graph_path, error)) {
            state.status_message = std::string("Saved ") + state.graph_path;
        } else {
            state.status_message = error;
        }
    }

    auto& open_wav = wav_open_browser();
    open_wav.Display();
    if (open_wav.HasSelected()) {
        const std::string selected = open_wav.GetSelected().string();
        open_wav.ClearSelected();
        const int target = wav_picker_node_id();
        wav_picker_node_id() = -1;
        if (auto* node = graph.find_node(target)) {
            if (auto* p = node->state().as<node_params::WavPlayer>()) {
                p->set_path(selected);
                graph.set_wav_path(selected);
                state.status_message = std::string("WAV Player → ") + selected;
                node_editor_publish(graph);
            }
        } else {
            state.status_message = "No WAV Player selected for file";
        }
    }
}

static void draw_diagnostics(NodeEditorUiState& state) {
    if (!state.show_diagnostics) {
        return;
    }
    float depths[3] = {
        static_cast<float>(g_pipeline.free_pool.size()),
        static_cast<float>(g_pipeline.cap_to_filter.size()),
        static_cast<float>(g_pipeline.filter_to_play.size())
    };
    ImGui::Text("XRUNs: %llu | dropped captures: %llu | %s",
                static_cast<unsigned long long>(g_pipeline.xrun_count.load()),
                static_cast<unsigned long long>(g_pipeline.dropped_capture_reads.load()),
                g_pipeline.wav_status.c_str());
    ImGui::PlotHistogram("##queues", depths, 3, 0, "Free | Cap->Filt | Filt->Play",
                         0.0f, static_cast<float>(QUEUE_CAPACITY), ImVec2(0, 80.0f));
}

static void context_menu_add_nodes(AudioGraph& graph, NodeEditorUiState& state,
                                  bool open_requested) {
    if (open_requested) {
        ImGui::OpenPopup("node_add_menu");
    }

    if (ImGui::BeginPopup("node_add_menu")) {
        ImVec2 screen = ImGui::GetMousePosOnOpeningCurrentPopup();
        // Approximate editor-space spawn near cursor; imnodes will place via SetNodeScreenSpacePos.
        const struct Entry {
            const char* label;
            NodeType type;
        } sources[] = {
            {"Audio Input (Live)", NodeType::audio_input},
            {"WAV Player", NodeType::wav_player},
            {"Chord Generator", NodeType::chord_generator},
            {"White Noise", NodeType::white_noise},
        };
        const Entry effects[] = {
            {"Echo", NodeType::echo},
            {"Preamp", NodeType::preamp},
            {"High-Pass Filter", NodeType::high_pass},
            {"Drive Pedal", NodeType::drive_pedal},
            {"Low-Pass Filter", NodeType::low_pass},
            {"Valve Compressor", NodeType::compressor},
            {"Noise Gate", NodeType::noise_gate},
            {"3-Band EQ", NodeType::eq_three_band},
            {"Reverb", NodeType::reverb},
            {"Mixer", NodeType::mixer},
            {"Dry/Wet Mix", NodeType::dry_wet_mix},
        };
        const Entry visualizers[] = {
            {"VU Meter", NodeType::vu_meter},
            {"Oscilloscope", NodeType::oscilloscope},
            {"Spectrum", NodeType::spectrum},
            {"Pitch Detector", NodeType::pitch_detector},
        };
        const Entry sinks[] = {
            {"WAV Recorder", NodeType::wav_recorder},
            {"Output", NodeType::output},
        };

        auto add_entries = [&](const char* group, const Entry* entries, int count) {
            ImGui::TextDisabled("%s", group);
            for (int i = 0; i < count; ++i) {
                if (ImGui::MenuItem(entries[i].label)) {
                    int id = graph.add_node(entries[i].type, 0.0f, 0.0f);
                    ImNodes::SetNodeScreenSpacePos(id, screen);
                    ImVec2 grid = ImNodes::GetNodeGridSpacePos(id);
                    if (auto* node = graph.find_node(id)) {
                        node->set_position(grid.x, grid.y);
                    }
                    node_editor_publish(graph);
                    state.status_message = std::string("Added ") + entries[i].label;
                }
            }
            ImGui::Separator();
        };

        add_entries("Sources", sources, IM_ARRAYSIZE(sources));
        add_entries("Effects", effects, IM_ARRAYSIZE(effects));
        add_entries("Visualizers", visualizers, IM_ARRAYSIZE(visualizers));
        add_entries("Sinks", sinks, IM_ARRAYSIZE(sinks));
        ImGui::EndPopup();
    }
}

void node_editor_draw(AudioGraph& graph, NodeEditorUiState& state) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Audio Node Graph", nullptr, flags);
    draw_toolbar(graph, state);
    draw_diagnostics(state);
    ImGui::Separator();

    ImNodes::BeginNodeEditor();

    if (state.positions_need_apply) {
        apply_node_positions(graph);
        state.positions_need_apply = false;
    }

    for (auto& node : graph.nodes()) {
        ImNodes::BeginNode(node.id());

        const char* title = node_type_label(node.type());

        ImNodes::BeginNodeTitleBar();
        // Measure the real title-row width (Text + optional Byp) instead of estimating.
        ImGui::BeginGroup();
        ImGui::TextUnformatted(title);
        if (node_supports_bypass(node.type())) {
            ImGui::PushID(node.id());
            bool& bypassed = node.state().bypassed;
            ImGui::SameLine();
            const bool bypass_style_pushed = bypassed;
            if (bypass_style_pushed) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.35f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.45f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.3f, 0.1f, 1.0f));
            }
            if (ImGui::SmallButton(bypassed ? "BYP" : "Byp")) {
                bypassed = !bypassed;
            }
            if (bypass_style_pushed) {
                ImGui::PopStyleColor(3);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(bypassed ? "Bypassed (click to enable)"
                                           : "Click to bypass");
            }
            ImGui::PopID();
        } else {
            node.state().bypassed = false;
        }
        ImGui::EndGroup();
        const float title_row_w = ImGui::GetItemRectSize().x;
        ImNodes::EndNodeTitleBar();

        // Pin tracks the widest row; Dummy + params + out share this width.
        float node_width = std::max(kNodeWidth, title_row_w);
        {
            const ImVec2 dims = ImNodes::GetNodeDimensions(node.id());
            if (dims.x > 1.0f) {
                node_width =
                    std::max(node_width, dims.x - 2.0f * ImNodes::GetStyle().NodePadding.x);
            }
        }
        ImGui::Dummy(ImVec2(node_width, 0.0f));

        if (node_has_input_pin(node.type())) {
            int slot_count = 1;
            if (node_is_mixer(node.type())) {
                slot_count = graph.mixer_pin_count(node.id());
            } else if (node_is_dry_wet(node.type())) {
                slot_count = 2;
            }
            for (int slot = 0; slot < slot_count; ++slot) {
                ImNodes::BeginInputAttribute(input_pin_id(node.id(), slot));
                if (node_is_mixer(node.type())) {
                    ImGui::PushID(node.id() * 32 + slot);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("in %d", slot + 1);
                    ImGui::SameLine(0.0f, 6.0f);
                    bool& muted = node.state().input_muted[static_cast<size_t>(slot)];
                    ImGui::Checkbox("##mute", &muted);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(muted ? "Muted (click to unmute)" : "Mute this input");
                    }
                    ImGui::PopID();
                } else if (node_is_dry_wet(node.type())) {
                    ImGui::TextUnformatted(slot == 0 ? "dry" : "wet");
                } else {
                    ImGui::TextUnformatted("in");
                }
                ImNodes::EndInputAttribute();
            }
        }

        ImGui::PushID(node.id());
        draw_node_params(node, graph);
        if (node_is_mixer(node.type())) {
            ImGui::TextDisabled("%d in", graph.mixer_connected_count(node.id()));
        }
        ImGui::PopID();

        if (node_has_output_pin(node.type())) {
            ImNodes::BeginOutputAttribute(output_pin_id(node.id()));
            // Prefer last frame's true content width so "out" tracks the pin if
            // anything still stretched the node (title bar, plots, etc.).
            float out_w = node_width;
            const ImVec2 dims = ImNodes::GetNodeDimensions(node.id());
            if (dims.x > 1.0f) {
                out_w = std::max(out_w, dims.x - 2.0f * ImNodes::GetStyle().NodePadding.x);
            }
            draw_output_label(out_w);
            ImNodes::EndOutputAttribute();
        }

        ImNodes::EndNode();
    }

    for (const auto& link : graph.links()) {
        ImNodes::Link(link.id(), output_pin_id(link.from_node()),
                      input_pin_id(link.to_node(), link.to_slot()));
    }

    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
    const bool open_node_menu = ImNodes::IsEditorHovered() &&
                                ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ImNodes::EndNodeEditor();

    context_menu_add_nodes(graph, state, open_node_menu);

    int start_attr = 0;
    int end_attr = 0;
    if (ImNodes::IsLinkCreated(&start_attr, &end_attr)) {
        // Normalize direction: output pin -> input pin
        if (!is_output_pin(start_attr) && is_output_pin(end_attr)) {
            std::swap(start_attr, end_attr);
        }
        const int from = node_id_from_pin(start_attr);
        const int to = node_id_from_pin(end_attr);
        const int to_slot = is_output_pin(end_attr) ? 0 : input_slot_from_pin(end_attr);
        if (is_output_pin(start_attr) && !is_output_pin(end_attr) &&
            graph.add_link(from, to, to_slot)) {
            node_editor_publish(graph);
            state.status_message = "Linked nodes";
        } else {
            state.status_message = "Link rejected (cycle, bad pins, or duplicate)";
        }
    }

    int destroyed = 0;
    if (ImNodes::IsLinkDestroyed(&destroyed)) {
        graph.remove_link(destroyed);
        node_editor_publish(graph);
    }

    // Delete inside a text field or while dragging a slider edits that widget, not the graph.
    const bool delete_pressed = ImGui::IsKeyPressed(ImGuiKey_Delete) &&
                                !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive();
    const int selected_links = ImNodes::NumSelectedLinks();
    if (selected_links > 0 && delete_pressed) {
        static std::vector<int> link_ids;
        link_ids.resize(static_cast<size_t>(selected_links));
        ImNodes::GetSelectedLinks(link_ids.data());
        for (int id : link_ids) {
            graph.remove_link(id);
        }
        // Ids are reused after deletion, so a stale selection would carry over to new links.
        ImNodes::ClearLinkSelection();
        node_editor_publish(graph);
    }

    const int selected_nodes = ImNodes::NumSelectedNodes();
    if (selected_nodes > 0 && delete_pressed) {
        static std::vector<int> node_ids;
        node_ids.resize(static_cast<size_t>(selected_nodes));
        ImNodes::GetSelectedNodes(node_ids.data());
        for (int id : node_ids) {
            graph.remove_node(id);
        }
        ImNodes::ClearNodeSelection();
        ImNodes::ClearLinkSelection();
        node_editor_publish(graph);
        state.positions_need_apply = true;
    }

    // Live param / position updates: republish each frame when graph is dirty-cheap compile.
    sync_node_positions(graph);
    node_editor_publish(graph);

    ImGui::End();

    // Modal file browsers sit above the fullscreen editor window.
    process_file_browsers(graph, state);
}
