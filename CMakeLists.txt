cmake_minimum_required(VERSION 3.16)
project(audio_effect LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

option(AUDIO_EFFECT_BUILD_TESTS "Build headless graph unit tests" ON)

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG QUIET)
if(NOT glfw3_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GLFW3 REQUIRED glfw3)
  else()
    find_path(GLFW_INCLUDE_DIR GLFW/glfw3.h)
    find_library(GLFW_LIBRARY NAMES glfw glfw3)
    if(NOT GLFW_INCLUDE_DIR OR NOT GLFW_LIBRARY)
      message(FATAL_ERROR "GLFW not found. Install glfw3 / libglfw3-dev.")
    endif()
  endif()
endif()

set(AUDIO_EFFECT_SOURCES
  src/main.cpp
  src/audio/pipeline.cpp
  src/audio/audio_devices.cpp
  src/graph/audio_graph.cpp
  src/graph/graph_io.cpp
  src/graph/graph_processor.cpp
  src/graph/node_state.cpp
  src/graph/visualizer_bus.cpp
  src/ui/node_editor_ui.cpp
  src/dsp/preamp.cpp
  src/dsp/dynamic_high_pass_filter.cpp
  src/dsp/dynamic_low_pass_filter.cpp
  src/dsp/echo_effect.cpp
  src/dsp/guitar_pedals.cpp
  src/dsp/valve_compressor.cpp
  src/dsp/noise_gate.cpp
  src/dsp/eq_three_band.cpp
  src/dsp/simple_reverb.cpp
  src/dsp/wav_recorder.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_glfw.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp
  third_party/imnodes/imnodes.cpp
  third_party/miniaudio/miniaudio.c
)

add_executable(audio_effect_gui ${AUDIO_EFFECT_SOURCES})

target_include_directories(audio_effect_gui PRIVATE
  src
  src/graph
  src/dsp
  src/audio
  src/ui
  third_party/imgui
  third_party/imgui/backends
  third_party/imnodes
  third_party/imgui-filebrowser
  third_party/miniaudio
)

if(MSVC)
  target_compile_options(audio_effect_gui PRIVATE /W3)
else()
  target_compile_options(audio_effect_gui PRIVATE -Wall -Wextra)
endif()

target_link_libraries(audio_effect_gui PRIVATE OpenGL::GL)

if(TARGET glfw)
  target_link_libraries(audio_effect_gui PRIVATE glfw)
elseif(TARGET glfw3)
  target_link_libraries(audio_effect_gui PRIVATE glfw3)
elseif(GLFW3_FOUND)
  target_include_directories(audio_effect_gui PRIVATE ${GLFW3_INCLUDE_DIRS})
  target_link_libraries(audio_effect_gui PRIVATE ${GLFW3_LIBRARIES})
else()
  target_include_directories(audio_effect_gui PRIVATE ${GLFW_INCLUDE_DIR})
  target_link_libraries(audio_effect_gui PRIVATE ${GLFW_LIBRARY})
endif()

if(WIN32)
  target_link_libraries(audio_effect_gui PRIVATE gdi32 user32 shell32 ole32)
elseif(APPLE)
  find_library(COCOA_LIBRARY Cocoa REQUIRED)
  find_library(IOKIT_LIBRARY IOKit REQUIRED)
  find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
  target_link_libraries(audio_effect_gui PRIVATE ${COCOA_LIBRARY} ${IOKIT_LIBRARY} ${COREVIDEO_LIBRARY})
else()
  # miniaudio loads ALSA/Pulse/JACK via dlopen; only pthread/m/dl are required.
  find_package(Threads REQUIRED)
  target_link_libraries(audio_effect_gui PRIVATE Threads::Threads dl m)
endif()

if(AUDIO_EFFECT_BUILD_TESTS)
  set(TEST_SOURCES
    tests/test_main.cpp
    tests/test_node_state.cpp
    tests/test_chord_catalog.cpp
    tests/test_audio_graph.cpp
    tests/test_graph_io.cpp
    tests/test_graph_processor.cpp
    tests/test_pitch_detector.cpp
    tests/test_new_nodes.cpp
    src/graph/audio_graph.cpp
    src/graph/graph_io.cpp
    src/graph/graph_processor.cpp
    src/graph/node_state.cpp
    src/graph/visualizer_bus.cpp
    src/dsp/preamp.cpp
    src/dsp/dynamic_high_pass_filter.cpp
    src/dsp/dynamic_low_pass_filter.cpp
    src/dsp/echo_effect.cpp
    src/dsp/guitar_pedals.cpp
    src/dsp/valve_compressor.cpp
    src/dsp/noise_gate.cpp
    src/dsp/eq_three_band.cpp
    src/dsp/simple_reverb.cpp
    src/dsp/wav_recorder.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
  )
  add_executable(graph_tests ${TEST_SOURCES})
  target_include_directories(graph_tests PRIVATE
    src src/graph src/dsp src/audio tests third_party/imgui)
  if(NOT MSVC)
    target_compile_options(graph_tests PRIVATE -Wall -Wextra)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(graph_tests PRIVATE Threads::Threads dl m)
  endif()
  enable_testing()
  add_test(NAME graph_tests COMMAND graph_tests)
endif()
