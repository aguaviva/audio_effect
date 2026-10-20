#include "audio/pipeline.hpp"
#include "graph/audio_graph.hpp"
#include "graph/graph_io.hpp"
#include "paths.hpp"
#include "ui/node_editor_ui.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    AudioGraph graph;
    g_pipeline.graph = &graph;

    std::string error;
    if (!g_pipeline.open_audio("default", error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        // Continue — GUI still starts; offline mode keeps visualizers alive.
    }

    // Prefer solo / danano under assets/audio like the previous startup behavior.
    g_pipeline.load_wav(kPreferredStartupWavPath);
    if (g_pipeline.loaded_wav) {
        graph.set_wav_path(kPreferredStartupWavPath);
    } else {
        g_pipeline.load_wav(kDefaultWavPath);
        if (g_pipeline.loaded_wav) {
            graph.set_wav_path(kDefaultWavPath);
        }
    }

    // CLI path wins, otherwise assets/patches/default_patch.json.
    const char* graph_path = (argc > 1) ? argv[1] : kDefaultPatchPath;
    NodeEditorUiState ui_state;
    if (!node_editor_load(graph, ui_state, graph_path)) {
        std::fprintf(stderr, "%s\n", ui_state.status_message.c_str());
    }
    std::snprintf(ui_state.graph_path, sizeof(ui_state.graph_path), "%s", graph_path);

    if (g_pipeline.playback_ready) {
        g_pipeline.start();
    } else {
        // No sound card (CI / VM): still run the graph so WAV + visualizers animate.
        g_pipeline.start_offline();
        ui_state.status_message += " | Offline audio (no device)";
    }

    if (!glfwInit()) {
        std::fprintf(stderr, "Failed to initialize GLFW\n");
        g_pipeline.stop();
        return 1;
    }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    int width = mode ? mode->width : 1280;
    int height = mode ? mode->height : 800;

    // Prefer a large window; skip exclusive/maximized fullscreen when DEMO_WINDOW=1
    // so screenshots on virtual displays are predictable.
    const bool demo_window = std::getenv("DEMO_WINDOW") != nullptr;
    if (!demo_window) {
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    } else {
        width = 1600;
        height = 900;
    }
    GLFWwindow* window = glfwCreateWindow(width, height, "Audio Effect — Node Graph", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        g_pipeline.stop();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    node_editor_init();

    std::snprintf(ui_state.wav_path, sizeof(ui_state.wav_path), "%s", graph.wav_path().c_str());

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        node_editor_draw(graph, ui_state);

        ImGui::Render();
        int display_w = 0, display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    g_pipeline.stop();

    node_editor_shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
