# Guitar Pedal Audio Processor

A cross-platform desktop audio processor for electric guitar built with [miniaudio](https://miniaud.io), C++17, Dear ImGui, [imnodes](https://github.com/Nelarius/imnodes), GLFW, and OpenGL (Linux and Windows). Patch sources, effects, and sinks freely in a fullscreen node editor. Live audio follows the compiled graph (DAG): fan-out duplicates, fan-in mixes (sums), cycles are rejected.

This project models a small set of pedal-style effects; it is not an amplifier or speaker-cabinet simulator, and its simplified models are not exact circuit emulations.

## Features

- Fullscreen ImGui **node editor** (imnodes) for free wiring of audio nodes.
- **Right-click** the canvas to add nodes from the inventory below.
- Each node has a title-bar **Byp** toggle (dry pass-through / mute sources & outputs). Delete selected nodes/links with Delete.
- **Load / save** the graph (node positions, types, parameters, and connections) as JSON via an ImGui file browser ([AirGuanZ/imgui-filebrowser](https://github.com/AirGuanZ/imgui-filebrowser)); WAV load uses the same picker.
- Live capture and playback via miniaudio (interleaved stereo 32-bit PCM at 48 kHz).
- WAV loading supports uncompressed PCM, mono or stereo, 16-, 24-, and 32-bit samples (converted/resampled to stereo 48 kHz).
- Optional queue/XRUN diagnostics; per-node Oscilloscope visualizer on the graph.

## Node inventory

### Sources / generators

- **Audio Input** — live capture
- **WAV Player** — loaded WAV clip (loop optional)
- **Chord Generator** — synthetic voicings for all 12 roots × 12 qualities (144 chords)

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

### Utility

- **Mixer** — several inputs auto-detected from connections (grows/shrinks, max 8), one output; equal-weight average of connected input blocks (`mixer` in saved graphs; links use `to_slot`)
- **Dry/Wet Mix** — two inputs (`dry` / `wet`) with a wet blend control

### Visualizers (pass-through)

- **VU Meter** — peak / RMS meters (does not alter audio)
- **Oscilloscope** — waveform view of the current block
- **Spectrum** — inexpensive 32-bin magnitude spectrum
- **Pitch Detector** — estimated frequency (Hz) and note name (pass-through)

### Sinks

- **WAV Recorder** — pass-through file writer (`assets/recordings/capture.wav` by default; Arm Record)
- **Output** — device playback

Visualizer nodes sit on the graph like effects (in + out) but only analyze the block and pass it through unchanged.

## Graph topology

Connections are **not** limited to a fixed linear chain. Any acyclic wiring is allowed:

- One source can feed many effects (**fan-out**).
- Several nodes can feed one input (**fan-in**, summed on normal nodes; **averaged** on Mixer).
- Multiple sources and multiple outputs may exist; all `Output` sinks are summed to the device.

### Buffer contract

The graph uses **one global block size** (audio period × channels). Every edge carries a buffer of that size: sources write it, effects read/write the same size in place, sinks consume it. There is no per-edge size negotiation.

A default seed graph is created on launch for convenience:

```text
WAV Player -> Preamp -> High-pass -> Low-pass -> Output
```

You can delete links/nodes, add stages from the context menu, and rewire arbitrarily. The audio thread loads a compiled snapshot of the graph each period so parameter and topology edits apply live.

## Load / save

Toolbar **Save Graph** / **Load Graph** read and write a small JSON document containing:

- `nodes[]`: `id`, `type`, `x`, `y`, `params` (`f` / `i` / `b` arrays)
- `links[]`: `id`, `from`, `to`
- `wav_path`, `next_node_id`, `next_link_id`, `version`

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
tests/                     # headless unit tests (make test)
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
```

### Commands (Makefile — Linux convenience)

```bash
make
./audio_effect_gui
make test
```

On launch the app loads `assets/patches/default_patch.json`. Pass another graph path as the first argument to override: `./audio_effect_gui assets/patches/demo_patch.json`. **Reset Default** restores the built-in default and rewrites `assets/patches/default_patch.json`. Run from the repo root so relative asset paths resolve.

```bash
make clean
```
