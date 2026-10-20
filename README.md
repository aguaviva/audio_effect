# Guitar Pedal Audio Processor

A cross-platform desktop audio processor for electric guitar built with [miniaudio](https://miniaud.io), C++17, Dear ImGui, [imnodes](https://github.com/Nelarius/imnodes), GLFW, and OpenGL (Linux and Windows). Patch sources, effects, and sinks freely in a fullscreen node editor. Live audio follows the compiled graph (DAG): outputs fan out, each input takes one link (combine signals with a Mixer), cycles are rejected.

This project models a small set of pedal-style effects; it is not an amplifier or speaker-cabinet simulator, and its simplified models are not exact circuit emulations.

## Features

- Fullscreen ImGui **node editor** (imnodes) for free wiring of audio nodes.
- **Right-click** the canvas to add nodes from the inventory below.
- Effects, visualizers, and sources have a title-bar **Byp** toggle (dry pass-through; a bypassed source is silent). Delete selected nodes/links with Delete.
- **Load / save** the graph (node positions, types, parameters, and connections) as JSON via an ImGui file browser ([AirGuanZ/imgui-filebrowser](https://github.com/AirGuanZ/imgui-filebrowser)); WAV load uses the same picker.
- Live capture and playback via miniaudio (interleaved stereo 32-bit float at 48 kHz, float end to end).
- WAV loading supports uncompressed PCM, mono or stereo, 16-, 24-, and 32-bit samples (converted/resampled to stereo 48 kHz).
- Optional queue/XRUN diagnostics; per-node Oscilloscope visualizer on the graph.

## Node inventory

### Sources / generators

- **Audio Input** — live capture
- **WAV Player** — plays its own WAV file (loop optional); a missing file plays silence
- **Chord Generator** — synthetic voicings for all 12 roots × 12 qualities (144 chords)
- **White Noise**

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

### Visualizers (pass-through)

- **VU Meter** — peak / RMS meters (does not alter audio)
- **Oscilloscope** — waveform view of the current block
- **Spectrum** — inexpensive 32-bin magnitude spectrum
- **Pitch Detector** — estimated frequency (Hz) and note name (pass-through)

### Sinks

- **WAV Recorder** — pass-through file writer; its `path` param defaults to `assets/recordings/capture.wav` (Arm Record)
- **Output** — device playback

Visualizer nodes sit on the graph like effects (in + out) but only analyze the block and pass it through unchanged.

## Graph topology

Connections are **not** limited to a fixed linear chain. Any acyclic wiring is allowed:

- One source can feed many effects (**fan-out**).
- Each input pin takes exactly one link; use a **Mixer** to combine several signals.
- Multiple sources and multiple outputs may exist; all `Output` sinks are summed to the device.

### Buffer contract

The graph uses **one global block size** (audio period × channels). Every link owns a buffer of that size: its source node writes it and its destination node reads it. A node with several outgoing links writes the first and the block is copied to the others.

You can delete links/nodes, add stages from the context menu, and rewire arbitrarily. The audio thread loads a compiled snapshot of the graph each period so parameter and topology edits apply live.

## Load / save

Toolbar **Save Graph** / **Load Graph** read and write a small JSON document containing:

- `version`: currently `2`
- `nodes[]`: `id`, `type`, `x`, `y`, `params`. `params` is a JSON object holding
  `bypassed`, optional `muted` (Mixer), and the module's own named settings, e.g.
  `{"bypassed":false,"delay_ms":320,"feedback":0.35,"mix":0.35}` for Echo.
- `links[]`: `id`, `from`, `to`, `to_slot`

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

On launch the app loads `assets/patches/default_patch.json`. Pass another graph path as the first argument to override: `./build/audio_effect_gui assets/patches/demo_patch.json`. **Reset Default** reloads `assets/patches/default_patch.json`. Run from the repo root so relative asset paths resolve.
