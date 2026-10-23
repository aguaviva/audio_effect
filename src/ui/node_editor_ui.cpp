#include "node_editor_ui.hpp"

#include "audio/audio_devices.hpp"
#include "audio/pipeline.hpp"
#include "dsp/audio_input.hpp"
#include "dsp/audio_output.hpp"
#include "dsp/mixer.hpp"
#include "graph/graph_io.hpp"
#include "graph/node_registry.hpp"
#include "graph/pins.hpp"
#include "paths.hpp"
#include "ui/file_picker.hpp"
#include "ui/message_box.hpp"

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

void copy_path_to_buf(char* buf, size_t n, const std::filesystem::path& path) {
    std::snprintf(buf, n, "%s", path.string().c_str());
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
        show_error("Could not load " + path + ":\n" + error);
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
    if (auto* m = node.as<AudioInput>()) {
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

static void draw_menu_bar(AudioGraph& graph, NodeEditorUiState& state) {
    if (!ImGui::BeginMenuBar()) {
        return;
    }
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Load Graph...")) {
            open_browser_near_path(graph_open_browser(), state.graph_path);
        }
        if (ImGui::MenuItem("Save Graph...")) {
            open_browser_near_path(graph_save_browser(), state.graph_path, true);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Default")) {
            node_editor_load(graph, state, kDefaultPatchPath);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Diagnostics", nullptr, &state.show_diagnostics);
        ImGui::MenuItem("Profiler", nullptr, &state.show_profiler);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::TextUnformatted("Right-click the canvas to add nodes.\n"
                               "One link per input (combine with a Mixer); outputs can fan out.\n"
                               "Ctrl+drag a pin to detach its link.\n"
                               "Title-bar Byp toggles bypass.\n"
                               "Mixer mute checkbox silences a slot.\n"
                               "Delete removes the selection.");
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
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
            show_error(std::string("Could not save ") + state.graph_path + ":\n" + error);
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
    ImGui::Text("XRUNs: %llu | dropped captures: %llu",
                static_cast<unsigned long long>(g_pipeline.xrun_count.load()),
                static_cast<unsigned long long>(g_pipeline.dropped_capture_reads.load()));
    ImGui::PlotHistogram("##queues", depths, 3, 0, "Free | Cap->Filt | Filt->Play",
                         0.0f, static_cast<float>(QUEUE_CAPACITY), ImVec2(0, 80.0f));
}

static void draw_profiler(const AudioGraph& graph, NodeEditorUiState& state) {
    if (!state.show_profiler) {
        return;
    }
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("Profiler", &state.show_profiler, flags) &&
        ImGui::BeginTable("profile", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Module");
        ImGui::TableSetupColumn("Time per block");
        ImGui::TableHeadersRow();
        float total = 0.0f;
        for (const auto& node : graph.nodes()) {
            const float us = node.module->process_us();
            total += us;
            ImGui::TableNextColumn();
            ImGui::Text("%s #%d", node_label(*node.module), node.id);
            ImGui::TableNextColumn();
            ImGui::Text("%8.1f us", us);
        }
        ImGui::TableNextColumn();
        ImGui::Separator();
        ImGui::TextUnformatted("Total");
        ImGui::TableNextColumn();
        ImGui::Separator();
        ImGui::Text("%8.1f us", total);
        ImGui::EndTable();
    }
    ImGui::End();
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
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Audio Node Graph", nullptr, flags);
    draw_menu_bar(graph, state);
    if (!state.status_message.empty()) {
        ImGui::TextWrapped("%s", state.status_message.c_str());
    }
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
        if (ImGui::BeginTable("node_layout", 3, ImGuiTableFlags_SizingFixedFit, ImVec2(200.0f, 0.0f)))
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
        std::string why = "Connect an output pin to an input pin.";
        if (is_output_pin(start_attr) && !is_output_pin(end_attr) &&
            graph.add_link(from, to, to_slot, &why)) {
            node_editor_publish(graph);
            state.status_message = "Linked nodes";
        } else {
            show_error(why);
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

    // Modal file browsers and the message box sit above the fullscreen editor window.
    process_file_browsers(graph, state);
    for (auto& node : graph.nodes()) {
        node.module->render_window();
    }
    draw_profiler(graph, state);
    draw_message_box();
}
