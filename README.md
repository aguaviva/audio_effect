# Guitar Pedal Audio Processor

A cross-platform desktop audio processor for electric guitar built with [miniaudio](https://miniaud.io), C++17, Dear ImGui, [imnodes](https://github.com/Nelarius/imnodes), GLFW, and OpenGL (Linux and Windows). Patch sources, effects, and sinks freely in a fullscreen node editor. Live audio follows the compiled graph (DAG): outputs fan out, each input takes one link (combine signals with a Mixer), cycles are rejected.

This project models a small set of pedal-style effects; it is not an amplifier or speaker-cabinet simulator, and its simplified models are not exact circuit emulations.

## Features

- Fullscreen ImGui **node editor** (imnodes) for free wiring of audio nodes.
- **Right-click** the canvas to add nodes from the inventory below.
- Effects and sources have a title-bar **Byp** toggle (dry pass-through; a bypassed source is silent). Delete selected nodes/links with Delete.
- **Load / save** the graph (node positions, types, parameters, and connections) as JSON via an ImGui file browser ([AirGuanZ/imgui-filebrowser](https://github.com/AirGuanZ/imgui-filebrowser)); WAV load uses the same picker.
- Live capture and playback via miniaudio (interleaved stereo 32-bit float at 48 kHz, float end to end).
- WAVs are read and written with miniaudio: any PCM or float WAV loads (converted/resampled to stereo 48 kHz); the recorder writes stereo 32-bit float.
- Optional queue/XRUN diagnostics; per-node Oscilloscope visualizer on the graph.

## Node inventory

### Sources / generators

Generators (all but Audio Input) have a play/pause button; pausing keeps their place.

- **Audio Input** — live capture
- **WAV Player** — plays its own WAV file (loop optional) with a seekable progress bar; a missing file plays silence
- **Chord Generator** — synthetic voicings for all 12 roots × 12 qualities (144 chords)
- **White Noise**
- **Timeline** — six tracks of WAV clips in a zoomable timeline window (**Show timeline** on the node), one output pin per track; drag a waveform to move it in time (it snaps to the start or end of other clips), click the ruler to place the cursor, and Play starts from the cursor

### Effects

- **Echo**
- **Preamp**
- **High-Pass Filter**
- **Drive Pedal** (Tube Screamer / RAT / Big Muff models)
- **Low-Pass Filter**
- **Valve Compressor**
- **Noise Gate**
- **3-Band EQ** (low shelf / mid peak / high shelf)
- **Reverb** (lightweight Schroeder-style)
- **AGC** — automatic gain control toward a target level

### Utility

- **Mixer** — several inputs auto-detected from connections (grows/shrinks, max 8), one output; equal-weight average of connected input blocks (`mixer` in saved graphs; links use `to_slot`)
- **Dry/Wet Mix** — two inputs (`dry` / `wet`) with a wet blend control

### Visualizers

- **VU Meter** — peak / RMS meters
- **Oscilloscope** — waveform view of the current block
- **Spectrum** — inexpensive 32-bin magnitude spectrum
- **Pitch Detector** — estimated frequency (Hz) and note name

### Sinks

- **WAV Recorder** — pass-through file writer; its `path` param defaults to `assets/recordings/capture.wav` (Arm Record)
- **Output** — device playback

Visualizers have an input and no outputs: link the signal you want to see into them, alongside wherever else it goes.

## Graph topology

Connections are **not** limited to a fixed linear chain. Any acyclic wiring is allowed:

- One source can feed many effects (**fan-out**).
- Each input pin takes exactly one link; use a **Mixer** to combine several signals.
- Multiple sources and multiple outputs may exist; all `Output` sinks are summed to the device.

### Buffer contract

The graph uses **one global block size** (audio period × channels). Every link owns a buffer of that size, with its own id: its source node writes it and its destination node reads it. Before each block, every module's output pins are pointed at buffers (`set_output_buffer`): a pin writes the buffer of its first link, the block is copied to the pin's other links, and a pin with no link gets no buffer and outputs nothing. Each output pin shows a small level meter.

You can delete links/nodes, add stages from the context menu, and rewire arbitrarily. The audio thread loads a compiled snapshot of the graph each period so parameter and topology edits apply live.

## Load / save

**File > Save Graph** / **Load Graph** read and write a small JSON document containing:

- `version`: currently `2`
- `nodes[]`: `id`, `type`, `x`, `y`, `params`. `params` is a JSON object holding
  `bypassed`, optional `muted` (Mixer), and the module's own named settings, e.g.
  `{"bypassed":false,"delay_ms":320,"feedback":0.35,"mix":0.35}` for Echo.
- `links[]`: `id`, `from`, `from_slot` (output pin, 0 when missing), `to`, `to_slot`

Node and link ids must be unique. A new node or link gets the largest existing id + 1.
Only version 2 files load.

## Layout

```text
assets/
  audio/                   # sample WAVs
  patches/                 # default_patch.json, demo_patch.json
src/
  main.cpp                 # GLFW / ImGui bootstrap
  paths.hpp                # default asset path constants
  audio/pipeline.*         # miniaudio I/O + queues + threads
  graph/                   # graph model, compile, I/O, DSP execution
  ui/node_editor_ui.*      # imnodes editor
  dsp/                     # effect / source implementations
tests/                     # headless unit tests (./build/graph_tests)
third_party/
  imgui/                   # submodule (ocornut/imgui)
  imnodes/                 # submodule (Nelarius/imnodes)
  imgui-filebrowser/       # submodule (AirGuanZ/imgui-filebrowser)
  miniaudio/               # submodule (mackron/miniaudio @ 0.11.25)
```

## Build and Run

### Requirements

- C++17 compiler (MSVC, g++, or clang++).
- GLFW and OpenGL development libraries.
- On Linux: `libglfw3-dev` (miniaudio uses ALSA/Pulse/JACK via `dlopen`; no `libasound` link required).
- On Windows: GLFW (vcpkg `glfw3` or system install).

### Commands (CMake — preferred)

```bash
git clone --recurse-submodules <this-repo-url>
# or, if already cloned:
git submodule update --init --recursive

cmake -S . -B build
cmake --build build
./build/audio_effect_gui          # Linux
# build\Debug\audio_effect_gui.exe  # Windows (MSVC)
./build/graph_tests               # unit tests
```

On launch the app loads `assets/patches/default_patch.json`. Pass another graph path as the first argument to override: `./build/audio_effect_gui assets/patches/demo_patch.json`. **File > Reset Default** reloads `assets/patches/default_patch.json`. Run from the repo root so relative asset paths resolve.

### Web build (Emscripten)

```bash
source <emsdk>/emsdk_env.sh
emcmake cmake -S . -B build-web
cmake --build build-web
python3 -m http.server -d build-web 8080   # open http://localhost:8080/audio_effect_gui.html
```

Only `assets/patches/` is preloaded into the page; WAVs are not bundled. A patch's WAV Player stays silent until you press its **...** button and upload a WAV from your machine. The file is copied into the page's in-memory file system under `/uploads/`. There are no threads on the web: the playback callback renders each block itself. The browser starts audio on the first click. The microphone is only opened when a patch uses Audio Input. Load/Save Graph and WAV Recorder still use the in-page file system, so they can't reach your own files yet.
