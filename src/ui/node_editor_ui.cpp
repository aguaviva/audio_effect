#include "node_editor_ui.hpp"

#include "audio/audio_devices.hpp"
#include "audio/pipeline.hpp"
#include "dsp/audio_input.hpp"
#include "dsp/audio_output.hpp"
#include "dsp/mixer.hpp"
#include "dsp/wav_player.hpp"
#include "graph/graph_io.hpp"
#include "graph/node_registry.hpp"
#include "graph/pins.hpp"
#include "paths.hpp"

#include "imgui.h"
#include "imfilebrowser.h"
#include "imnodes.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Opens the browser's file picker and copies the chosen file into dir in the page's file system.
EM_JS(void, upload_file_to, (const char* dir, const char* accept), {
    const d = UTF8ToString(dir);
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = UTF8ToString(accept);
    input.onchange = async () => {
        const file = input.files[0];
        if (!file) return;
        const bytes = new Uint8Array(await file.arrayBuffer());
        FS.mkdirTree(d);
        FS.writeFile(d + '/' + file.name, bytes);
    };
    input.click();
});
#endif

namespace {

// ImNodes sizes a node to its widest row. Every body row (params + "out") must
// share this width so the right-aligned "out" sits against the output pin.
constexpr float kNodeWidth = 140.0f;

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

#ifdef __EMSCRIPTEN__
// Directory the pending upload is written to; a fresh one per upload, so a re-uploaded
// file with the same name is not served from the WAV cache.
std::string& wav_upload_dir() {
    static std::string dir;
    return dir;
}

std::string first_file_in(const std::string& dir) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        return entry.path().string();
    }
    return {};
}
#else
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
#endif

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

void open_wav_picker(int node_id, [[maybe_unused]] const std::string& current_path) {
    wav_picker_node_id() = node_id;
#ifdef __EMSCRIPTEN__
    static int uploads = 0;
    wav_upload_dir() = "/uploads/" + std::to_string(++uploads);
    upload_file_to(wav_upload_dir().c_str(), ".wav");
#else
    open_browser_near_path(wav_open_browser(), current_path.c_str());
#endif
}

void assign_picked_wav(AudioGraph& graph, NodeEditorUiState& state, const std::string& path) {
    const int target = wav_picker_node_id();
    wav_picker_node_id() = -1;
    if (auto* node = graph.find_node(target)) {
        if (auto* p = node->as<WavPlayer>()) {
            p->set_path(path);
            state.status_message = std::string("WAV Player → ") + path;
            node_editor_publish(graph);
        }
    } else {
        state.status_message = "No WAV Player selected for file";
    }
}

} // namespace

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

bool node_editor_load(AudioGraph& graph, NodeEditorUiState& state, const std::string& path) {
    std::string error;
    if (!graph_io::load_from_file(graph, path, error)) {
        state.status_message = error;
        return false;
    }
    std::snprintf(state.graph_path, sizeof(state.graph_path), "%s", path.c_str());
    for (const auto& node : graph.nodes()) {
        std::string err;
        if (const auto* p = node.as<AudioInput>()) {
            g_pipeline.set_capture_device(p->device.c_str(), err);
        }
        if (const auto* p = node.as<AudioOutput>()) {
            g_pipeline.set_playback_device(p->device.c_str(), err);
        }
    }
    state.positions_need_apply = true;
    node_editor_publish(graph);
    state.status_message = "Loaded " + path;
    if (!g_pipeline.last_device_error.empty()) {
        state.status_message += " | " + g_pipeline.last_device_error;
    }
    return true;
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

static void draw_node_params(GraphNode& node, AudioGraph& graph) {
    if (auto* m = node.as<WavPlayer>()) {
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
            open_wav_picker(node.id, m->path());
        }
    } else if (auto* m = node.as<AudioInput>()) {
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
    } else if (auto* m = node.as<AudioOutput>()) {
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
    } else if (auto* m = node.as<Mixer>()) {
        m->ui_slots = graph.input_pin_count(node.id);
    }
    node.module->render();
}

static void apply_node_positions(AudioGraph& graph) {
    for (const auto& node : graph.nodes()) {
        ImNodes::SetNodeGridSpacePos(node.id, ImVec2(node.x, node.y));
    }
}

static void sync_node_positions(AudioGraph& graph) {
    for (auto& node : graph.nodes()) {
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.id);
        node.x = pos.x;
        node.y = pos.y;
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
        node_editor_load(graph, state, kDefaultPatchPath);
    }

    ImGui::Checkbox("Diagnostics", &state.show_diagnostics);

    if (!state.status_message.empty()) {
        ImGui::TextWrapped("%s", state.status_message.c_str());
    }
    ImGui::TextDisabled(
        "Right-click canvas to add nodes. One link per input (combine with a Mixer); outputs can fan out. "
        "Ctrl+drag pin to detach. Title-bar Byp toggles bypass. "
        "Mixer mute checkbox silences a slot. Delete removes selection.");
}

static void process_file_browsers(AudioGraph& graph, NodeEditorUiState& state) {
    auto& open_graph = graph_open_browser();
    open_graph.Display();
    if (open_graph.HasSelected()) {
        const std::string selected = open_graph.GetSelected().string();
        open_graph.ClearSelected();
        node_editor_load(graph, state, selected);
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

#ifdef __EMSCRIPTEN__
    if (!wav_upload_dir().empty()) {
        const std::string uploaded = first_file_in(wav_upload_dir());
        if (!uploaded.empty()) {
            wav_upload_dir().clear();
            assign_picked_wav(graph, state, uploaded);
        }
    }
#else
    auto& open_wav = wav_open_browser();
    open_wav.Display();
    if (open_wav.HasSelected()) {
        const std::string selected = open_wav.GetSelected().string();
        open_wav.ClearSelected();
        assign_picked_wav(graph, state, selected);
    }
#endif
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
    ImGui::Text("XRUNs: %llu | dropped captures: %llu",
                static_cast<unsigned long long>(g_pipeline.xrun_count.load()),
                static_cast<unsigned long long>(g_pipeline.dropped_capture_reads.load()));
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
        const char* category = nullptr;
        for (const auto& kind : node_kinds()) {
            if (!category || std::strcmp(category, kind.category) != 0) {
                if (category) {
                    ImGui::Separator();
                }
                category = kind.category;
                ImGui::TextDisabled("%s", category);
            }
            if (ImGui::MenuItem(kind.label)) {
                const int id = graph.add_node(kind.make()->name(), 0.0f, 0.0f);
                ImNodes::SetNodeScreenSpacePos(id, screen);
                const ImVec2 grid = ImNodes::GetNodeGridSpacePos(id);
                if (auto* node = graph.find_node(id)) {
                    node->x = grid.x;
                    node->y = grid.y;
                }
                node_editor_publish(graph);
                state.status_message = std::string("Added ") + kind.label;
            }
        }
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
        ImNodes::BeginNode(node.id);

        const char* title = node_label(*node.module);

        ImNodes::BeginNodeTitleBar();
        // Measure the real title-row width (Text + optional Byp) instead of estimating.
        ImGui::BeginGroup();
        ImGui::TextUnformatted(title);
        if (node.module->supports_bypass()) {
            ImGui::PushID(node.id);
            bool& bypassed = node.bypassed;
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
            node.bypassed = false;
        }
        ImGui::EndGroup();
        ImNodes::EndNodeTitleBar();

        ImGui::PushID(node.id);
        ImGui::BeginChild("child", ImVec2(200.0f, 0.0f), ImGuiChildFlags_AutoResizeY);

        if (ImGui::BeginTable("node_layout", 3, ImGuiTableFlags_SizingFixedFit))
        {
            // Setup column widths: column 0 and 2 wrap the pins, column 1 takes the rest (-1.0f)
            ImGui::TableSetupColumn("In", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Center", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Out", ImGuiTableColumnFlags_WidthFixed);

            ImGui::TableNextRow();

            // --- Column 0: Input Pin ---
            ImGui::TableSetColumnIndex(0);
            {
                int inputs=0;
                const char* const* name;
                node.module->get_inputs(&inputs, &name);
                inputs = std::min(inputs, graph.input_pin_count(node.id));

                for (int slot = 0; slot < inputs; ++slot) {
                    ImNodes::BeginInputAttribute(input_pin_id(node.id, slot));
                    ImGui::PushID(node.id * 32 + slot);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(name[slot]);
                    ImGui::PopID();
                    ImNodes::EndInputAttribute();
                }
            }

            // --- Column 1: Slider (with your -1.0f width requirement) ---
            ImGui::TableSetColumnIndex(1);        
            ImGui::PushID(node.id);
            draw_node_params(node, graph);
            ImGui::PopID();

            // --- Column 2: Output Pin ---
            ImGui::TableSetColumnIndex(2); 
            {       
                int outputs;
                const char* const* name;
                node.module->get_outputs(&outputs, &name);
                if (outputs>0) {
                    ImNodes::BeginOutputAttribute(output_pin_id(node.id));
                    ImGui::TextUnformatted(name[0]);
                    ImNodes::EndOutputAttribute();
                }
            }

            ImGui::EndTable();        
        }

        ImGui::EndChild();
        ImGui::PopID();

        ImNodes::EndNode();
    }

    for (const auto& link : graph.links()) {
        ImNodes::Link(link.id, output_pin_id(link.from_node),
                      input_pin_id(link.to_node, link.to_slot));
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
            state.status_message = "Link rejected (cycle, bad pins, or input already connected)";
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
