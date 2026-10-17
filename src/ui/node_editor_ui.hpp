#pragma once

#include "graph/audio_graph.hpp"
#include "paths.hpp"

#include <cstdio>
#include <string>

struct NodeEditorUiState {
    char graph_path[512]{};
    char wav_path[512]{};
    std::string status_message;
    bool positions_need_apply = true;
    bool show_diagnostics = false;

    NodeEditorUiState() {
        std::snprintf(graph_path, sizeof(graph_path), "%s", kDefaultPatchPath);
        std::snprintf(wav_path, sizeof(wav_path), "%s", kDefaultWavPath);
    }
};

void node_editor_init();
void node_editor_shutdown();
void node_editor_draw(AudioGraph& graph, NodeEditorUiState& state);
void node_editor_publish(AudioGraph& graph);
