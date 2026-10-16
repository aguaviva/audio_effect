# Guitar Pedal Audio Processor

A Linux desktop audio processor for electric guitar built with ALSA, C++17, Dear ImGui, GLFW, and OpenGL. It can process live capture or play a WAV file through a real-time pedal-style effect chain. The interface includes effect controls, a pitch readout, a triggered input/output oscilloscope, and queue/XRUN diagnostics.

This project models a small set of pedal-style effects; it is not an amplifier or speaker-cabinet simulator, and its simplified models are not exact circuit emulations.

## Features

- Live ALSA capture and playback using interleaved stereo `S32_LE` audio at 48 kHz.
- Loads `danano.wav` from the working directory and starts it in loop mode at launch. If it cannot be loaded, the app reports the error and uses live capture.
- WAV loading supports uncompressed PCM, mono or stereo, 16-, 24-, and 32-bit samples. Audio is converted to stereo and linearly resampled to 48 kHz.
- Independent, enableable effect stages with collapsible controls.
- Pedal-style clean boost, compression, overdrive, distortion, and fuzz presets.
- Optional pitch detection over approximately 70-1400 Hz.
- Trigger-aligned oscilloscope showing source input and processed output together.
- Queue depth, live-buffer, capture-drop, and XRUN diagnostics.

## Signal Flow

The source is either the WAV player or live capture. Pitch detection observes the source signal before effects. The audio effect chain is:

```text
Source -> Echo -> Preamp -> High-pass -> Drive pedal -> Low-pass -> Valve-style compressor -> ALSA playback
```

Disabled stages are bypassed. The default stage states are echo off, preamp on at 0 dB, high-pass on at 70 Hz, drive pedal off, low-pass on at 8 kHz, and compressor off.

### Why This Order

1. **Echo first:** Repeats are created from the incoming signal before gain and tone shaping, so repeats are processed along with the original by later stages. This gives the drive and filters a chance to shape the echoes too.
2. **Preamp:** Sets the signal level before filtering and nonlinear pedal processing. Raising it can push the drive pedal harder; the internal float path preserves headroom until conversion to PCM output.
3. **High-pass:** Removes subsonic rumble before distortion. Excess low-frequency energy can make a drive stage sound muddy and can consume headroom.
4. **Drive pedal:** Adds nonlinear overdrive, clipping, or fuzz. It comes after the high-pass so unwanted lows are reduced before they generate distortion harmonics.
5. **Low-pass:** Shapes the brightness after distortion. Clipping creates high-frequency harmonics, so filtering here can tame harshness without removing those harmonics before they are generated.
6. **Valve-style compressor:** Applies envelope-based level reduction followed by asymmetric saturation. At the end, it controls and colors the complete tone-shaped signal. It is a simplified effect, not a physical valve model.

## Presets

The preset selector applies starting values to the available stages:

- **MXR Micro Amp-style Clean Boost:** Preamp gain with the drive pedal and compressor bypassed.
- **Boss CS-3-style Compression:** Compressor enabled with moderate compression and low saturation.
- **Ibanez TS808-style Overdrive:** Tube Screamer-style drive shaping.
- **Pro Co RAT-style Distortion:** Hard-clipping drive shaping.
- **Electro-Harmonix Big Muff Pi-style Fuzz:** Cascaded soft clipping for a denser fuzz character.

These are broad tonal starting points built from the project's available controls, not exact recreations of those pedals. Changing effect controls selects `Custom`.

## WAV Playback

At startup, the application looks for `danano.wav` in the current working directory. Playback starts automatically and loops by default. Use the WAV path field and **Load WAV** button to load another file. **Play WAV source** switches between the loaded clip and live ALSA capture; **Loop WAV** controls whether playback wraps at the end. While the WAV source is selected, ALSA capture is still drained to keep the capture device running without overruns.

Supported files are RIFF/WAVE PCM, mono or stereo, 16-, 24-, or 32-bit. Compressed WAV formats and floating-point WAV are not supported by the built-in loader.

## Build and Run

### Requirements

- Linux with ALSA devices and development headers.
- C++17 compiler (`g++` or `clang++`).
- GLFW and OpenGL development libraries.
- ALSA development library (`libasound2-dev` on Debian/Ubuntu).

### Commands

```bash
make
./alsa_lpf_gui
```

`make run` launches through `chrt` and may require elevated permissions. The selected ALSA `default` device must support the requested 48 kHz stereo `S32_LE` format or provide conversion through its ALSA configuration.

Place `danano.wav` in the working directory from which the executable is launched, or load another file using its path in the application. The WAV is not bundled with the project.

To remove generated build artifacts:

```bash
make clean
```