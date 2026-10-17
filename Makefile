# Convenience Makefile (Linux). Prefer CMake for Windows/Linux:
#   cmake -S . -B build && cmake --build build
CXX      := g++
CC       := gcc
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -pthread \
            -Isrc \
            -Isrc/graph \
            -Isrc/dsp \
            -Isrc/audio \
            -Isrc/ui \
            -Ithird_party/imgui \
            -Ithird_party/imgui/backends \
            -Ithird_party/imnodes \
            -Ithird_party/imgui-filebrowser \
            -Ithird_party/miniaudio
CFLAGS   := -std=c99 -O3 -Wall -Wextra -Ithird_party/miniaudio

LDFLAGS  := -lGL -lglfw -pthread -ldl -lm

TARGET   := audio_effect_gui

SRC_APP  := src/main.cpp \
            src/audio/pipeline.cpp \
            src/audio/audio_devices.cpp \
            src/graph/audio_graph.cpp \
            src/graph/graph_io.cpp \
            src/graph/graph_processor.cpp \
            src/graph/node_state.cpp \
            src/graph/visualizer_bus.cpp \
            src/ui/node_editor_ui.cpp

SRC_EFFECTS := src/dsp/preamp.cpp \
               src/dsp/dynamic_high_pass_filter.cpp \
               src/dsp/dynamic_low_pass_filter.cpp \
               src/dsp/echo_effect.cpp \
               src/dsp/guitar_pedals.cpp \
               src/dsp/valve_compressor.cpp \
               src/dsp/noise_gate.cpp \
               src/dsp/eq_three_band.cpp \
               src/dsp/simple_reverb.cpp \
               src/dsp/wav_recorder.cpp

SRC_IMGUI := third_party/imgui/imgui.cpp \
             third_party/imgui/imgui_draw.cpp \
             third_party/imgui/imgui_tables.cpp \
             third_party/imgui/imgui_widgets.cpp \
             third_party/imgui/backends/imgui_impl_glfw.cpp \
             third_party/imgui/backends/imgui_impl_opengl3.cpp \
             third_party/imnodes/imnodes.cpp

SRC_C    := third_party/miniaudio/miniaudio.c

OBJ := $(SRC_APP:.cpp=.o) $(SRC_EFFECTS:.cpp=.o) $(SRC_IMGUI:.cpp=.o) $(SRC_C:.c=.o)

TEST_TARGET := tests/graph_tests
TEST_SRC := tests/test_main.cpp \
            tests/test_node_state.cpp \
            tests/test_chord_catalog.cpp \
            tests/test_audio_graph.cpp \
            tests/test_graph_io.cpp \
            tests/test_graph_processor.cpp \
            tests/test_pitch_detector.cpp \
            tests/test_new_nodes.cpp
TEST_SUPPORT := src/graph/audio_graph.cpp \
                src/graph/graph_io.cpp \
                src/graph/graph_processor.cpp \
                src/graph/node_state.cpp \
                src/graph/visualizer_bus.cpp \
                $(SRC_EFFECTS)
TEST_IMGUI := third_party/imgui/imgui.cpp \
              third_party/imgui/imgui_draw.cpp \
              third_party/imgui/imgui_tables.cpp \
              third_party/imgui/imgui_widgets.cpp
TEST_CXXFLAGS := -std=c++17 -O0 -g -Wall -Wextra -pthread \
                 -Isrc -Isrc/graph -Isrc/dsp -Isrc/audio -Itests \
                 -Ithird_party/imgui

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_TARGET): $(TEST_SRC) $(TEST_SUPPORT) $(TEST_IMGUI) tests/test_harness.hpp
	$(CXX) $(TEST_CXXFLAGS) $(TEST_SRC) $(TEST_SUPPORT) $(TEST_IMGUI) -o $@ -pthread -ldl -lm

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJ) $(TARGET) $(TEST_TARGET) tests/_tmp_graph.json alsa_lpf_gui

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run test
