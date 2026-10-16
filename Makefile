CXX      := g++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -pthread \
            -Ithird_party/imgui \
            -Ithird_party/imgui/backends

LDFLAGS  := -lasound -lGL -lglfw

# Target & Source Directories
TARGET   := alsa_lpf_gui
SRC_APP  := src/main.cpp
SRC_EFFECTS := src/preamp.cpp \
               src/dynamic_high_pass_filter.cpp \
               src/dynamic_low_pass_filter.cpp \
               src/echo_effect.cpp \
               src/guitar_pedals.cpp \
               src/valve_compressor.cpp

# ImGui Core & Backend Source Files
SRC_IMGUI := third_party/imgui/imgui.cpp \
             third_party/imgui/imgui_draw.cpp \
             third_party/imgui/imgui_tables.cpp \
             third_party/imgui/imgui_widgets.cpp \
             third_party/imgui/backends/imgui_impl_glfw.cpp \
             third_party/imgui/backends/imgui_impl_opengl3.cpp

# Combine Object Files
OBJ      := $(SRC_APP:.cpp=.o) $(SRC_EFFECTS:.cpp=.o) $(SRC_IMGUI:.cpp=.o)

# Default Rule
all: $(TARGET)

# Link Executable
$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

# Compile C++ Source Files to Object Files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean Build Artifacts
clean:
	rm -f $(OBJ) $(TARGET)

# Run with Real-Time Priority for Minimal Latency
run: $(TARGET)
	sudo chrt -f 99 ./$(TARGET)

.PHONY: all clean run