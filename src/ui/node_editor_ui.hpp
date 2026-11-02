#pragma once

#include "graph/audio_graph.hpp"
#include "paths.hpp"

#include <cstdio>
#include <string>

struct NodeEditorUiState {
    char graph_path[512]{};
    std::string status_message;
    bool positions_need_apply = true;
    bool show_diagnostics = false;
    bool show_profiler = false;

    NodeEditorUiState() {
        std::snprintf(graph_path, sizeof(graph_path), "%s", kDefaultPatchPath);
    }
};

void node_editor_init();
void node_editor_shutdown();
void node_editor_draw(AudioGraph& graph, NodeEditorUiState& state);
void node_editor_publish(AudioGraph& graph);
// Loads a patch and applies its device choices. Success is reported in state.status_message;
// a failure goes to the message box and leaves the graph untouched.
bool node_editor_load(AudioGraph& graph, NodeEditorUiState& state, const std::string& path);
