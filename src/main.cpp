#include "audio/pipeline.hpp"
#include "graph/audio_graph.hpp"
#include "graph/graph_io.hpp"
#include "paths.hpp"
#include "ui/node_editor_ui.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct App {
    GLFWwindow* window = nullptr;
    AudioGraph graph;
    NodeEditorUiState ui_state;
};

void draw_frame(App& app) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    node_editor_draw(app.graph, app.ui_state);

    ImGui::Render();
    int display_w = 0, display_h = 0;
    glfwGetFramebufferSize(app.window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(app.window);
}

} // namespace

int main(int argc, char** argv) {
    // Static: on the web main() returns and the browser keeps calling draw_frame().
    static App app;
    AudioGraph& graph = app.graph;
    NodeEditorUiState& ui_state = app.ui_state;
    g_pipeline.graph = &graph;

    std::string error;
    if (!g_pipeline.open_audio("default", error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        // Continue — GUI still starts; offline mode keeps visualizers alive.
    }

    // CLI path wins, otherwise assets/patches/default_patch.json.
    const char* graph_path = (argc > 1) ? argv[1] : kDefaultPatchPath;
    if (!node_editor_load(graph, ui_state, graph_path)) {
        std::fprintf(stderr, "%s\n", ui_state.status_message.c_str());
    }

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

    int width = 1280;
    int height = 800;
#ifdef __EMSCRIPTEN__
    const char* glsl_version = nullptr; // the backend picks GLSL ES 3.00 for WebGL 2
#else
    const char* glsl_version = "#version 130";
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr) {
        width = mode->width;
        height = mode->height;
    }
    // Prefer a large window; skip exclusive/maximized fullscreen when DEMO_WINDOW=1
    // so screenshots on virtual displays are predictable.
    if (std::getenv("DEMO_WINDOW") == nullptr) {
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    } else {
        width = 1600;
        height = 900;
    }
#endif
    app.window = glfwCreateWindow(width, height, "Audio Effect — Node Graph", nullptr, nullptr);
    if (!app.window) {
        std::fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        g_pipeline.stop();
        return 1;
    }
    glfwMakeContextCurrent(app.window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(app.window, true);
#ifdef __EMSCRIPTEN__
    ImGui_ImplGlfw_InstallEmscriptenCallbacks(app.window, "#canvas");
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);
    node_editor_init();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg([](void* p) { draw_frame(*static_cast<App*>(p)); }, &app, 0, false);
#else
    while (!glfwWindowShouldClose(app.window)) {
        draw_frame(app);
    }

    g_pipeline.stop();

    node_editor_shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(app.window);
    glfwTerminate();
#endif
    return 0;
}
