cmake_minimum_required(VERSION 3.16)
project(audio_effect LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

if(EMSCRIPTEN)
  option(AUDIO_EFFECT_BUILD_TESTS "Build headless graph unit tests" OFF)
else()
  option(AUDIO_EFFECT_BUILD_TESTS "Build headless graph unit tests" OFF)
  find_package(OpenGL REQUIRED)
  find_package(glfw3 CONFIG QUIET)
endif()
if(NOT EMSCRIPTEN AND NOT glfw3_FOUND)
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
  src/graph/node_registry.cpp
  src/ui/node_editor_ui.cpp
  src/ui/file_picker.cpp
  src/ui/message_box.cpp
  src/dsp/preamp.cpp
  src/dsp/dynamic_high_pass_filter.cpp
  src/dsp/dynamic_low_pass_filter.cpp
  src/dsp/echo_effect.cpp
  src/dsp/guitar_pedals.cpp
  src/dsp/valve_compressor.cpp
  src/dsp/noise_gate.cpp
  src/dsp/eq_three_band.cpp
  src/dsp/simple_reverb.cpp
  src/dsp/agc.cpp
  src/dsp/wav_recorder.cpp
  src/dsp/wav_player.cpp
  src/dsp/wav_generator.cpp
  src/dsp/audio_input.cpp
  src/dsp/audio_output.cpp
  src/dsp/chord_generator.cpp
  src/dsp/gen_midi.cpp
  src/dsp/white_noise.cpp
  src/dsp/timeline.cpp
  src/dsp/dry_wet_mix.cpp
  src/dsp/vu_meter.cpp
  src/dsp/oscilloscope.cpp
  src/dsp/spectrum.cpp
  src/dsp/pitch_detector_module.cpp
  src/dsp/mixer.cpp
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_glfw.cpp
  third_party/imgui/backends/imgui_impl_opengl3.cpp
  third_party/imnodes/imnodes.cpp
  third_party/miniaudio/miniaudio.c
  third_party/SimpleMidi/midi.cpp
  third_party/SimpleMidi/inst-piano.cpp
  third_party/SimpleMidi/misc.cpp
)

add_executable(audio_effect_gui ${AUDIO_EFFECT_SOURCES})
target_compile_definitions(audio_effect_gui PRIVATE MA_NO_MP3 MA_NO_FLAC)

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
  third_party/SimpleMidi
)

if(MSVC)
  target_compile_options(audio_effect_gui PRIVATE /W3)
else()
  target_compile_options(audio_effect_gui PRIVATE -Wall -Wextra)
endif()

if(EMSCRIPTEN)
  # Patches are preloaded under /assets and the working directory is /, so relative paths
  # resolve. WAVs are not bundled: the WAV Player uploads them from the user's machine.
  set(EMSCRIPTEN_FLAGS --use-port=contrib.glfw3)
  target_compile_options(audio_effect_gui PRIVATE ${EMSCRIPTEN_FLAGS})
  target_link_options(audio_effect_gui PRIVATE ${EMSCRIPTEN_FLAGS}
    -sUSE_WEBGL2=1 -sMAX_WEBGL_VERSION=2 -sALLOW_MEMORY_GROWTH=1
    --preload-file ${CMAKE_SOURCE_DIR}/assets/patches@/assets/patches
    --shell-file ${CMAKE_SOURCE_DIR}/third_party/imgui/examples/libs/emscripten/shell_minimal.html)
  set_target_properties(audio_effect_gui PROPERTIES SUFFIX ".html")
  set_source_files_properties(third_party/miniaudio/miniaudio.c PROPERTIES
    COMPILE_OPTIONS -Wno-unused-parameter) # miniaudio's Web Audio backend
elseif(TARGET glfw)
  target_link_libraries(audio_effect_gui PRIVATE OpenGL::GL glfw)
elseif(TARGET glfw3)
  target_link_libraries(audio_effect_gui PRIVATE OpenGL::GL glfw3)
elseif(GLFW3_FOUND)
  target_include_directories(audio_effect_gui PRIVATE ${GLFW3_INCLUDE_DIRS})
  target_link_libraries(audio_effect_gui PRIVATE OpenGL::GL ${GLFW3_LIBRARIES})
else()
  target_include_directories(audio_effect_gui PRIVATE ${GLFW_INCLUDE_DIR})
  target_link_libraries(audio_effect_gui PRIVATE OpenGL::GL ${GLFW_LIBRARY})
endif()

if(WIN32)
  target_link_libraries(audio_effect_gui PRIVATE gdi32 user32 shell32 ole32)
elseif(APPLE)
  find_library(COCOA_LIBRARY Cocoa REQUIRED)
  find_library(IOKIT_LIBRARY IOKit REQUIRED)
  find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
  target_link_libraries(audio_effect_gui PRIVATE ${COCOA_LIBRARY} ${IOKIT_LIBRARY} ${COREVIDEO_LIBRARY})
elseif(NOT EMSCRIPTEN)
  # miniaudio loads ALSA/Pulse/JACK via dlopen; only pthread/m/dl are required.
  find_package(Threads REQUIRED)
  target_link_libraries(audio_effect_gui PRIVATE Threads::Threads dl m)
endif()

if(AUDIO_EFFECT_BUILD_TESTS)
  set(TEST_SOURCES
    tests/test_main.cpp
    tests/test_node_registry.cpp
    tests/test_chord_catalog.cpp
    tests/test_audio_graph.cpp
    tests/test_graph_io.cpp
    tests/test_graph_processor.cpp
    tests/test_pitch_detector.cpp
    tests/test_new_nodes.cpp
    src/graph/audio_graph.cpp
    src/graph/graph_io.cpp
    src/graph/graph_processor.cpp
    src/graph/node_registry.cpp
    src/dsp/preamp.cpp
    src/dsp/dynamic_high_pass_filter.cpp
    src/dsp/dynamic_low_pass_filter.cpp
    src/dsp/echo_effect.cpp
    src/dsp/guitar_pedals.cpp
    src/dsp/valve_compressor.cpp
    src/dsp/noise_gate.cpp
    src/dsp/eq_three_band.cpp
    src/dsp/simple_reverb.cpp
    src/dsp/agc.cpp
    src/dsp/wav_recorder.cpp
    src/dsp/wav_player.cpp
    src/dsp/wav_generator.cpp
    src/ui/file_picker.cpp
    src/ui/message_box.cpp
    src/dsp/audio_input.cpp
    src/dsp/audio_output.cpp
    src/dsp/chord_generator.cpp
    src/dsp/white_noise.cpp
    src/dsp/timeline.cpp
    src/dsp/dry_wet_mix.cpp
    src/dsp/vu_meter.cpp
    src/dsp/oscilloscope.cpp
    src/dsp/spectrum.cpp
    src/dsp/pitch_detector_module.cpp
    src/dsp/mixer.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/miniaudio/miniaudio.c
  )
  add_executable(graph_tests ${TEST_SOURCES})
  target_compile_definitions(graph_tests PRIVATE MA_NO_MP3 MA_NO_FLAC)
  target_include_directories(graph_tests PRIVATE
    src src/graph src/dsp src/audio tests third_party/imgui third_party/imgui-filebrowser
    third_party/miniaudio third_party/SimpleMidi)
  if(NOT MSVC)
    target_compile_options(graph_tests PRIVATE -Wall -Wextra)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(graph_tests PRIVATE Threads::Threads dl m)
  endif()
  enable_testing()
  add_test(NAME graph_tests COMMAND graph_tests)
endif()
