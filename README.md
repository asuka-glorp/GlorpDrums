# GlorpDrums

**GlorpDrums** is a sample-based acoustic drum plugin and standalone application built with JUCE and modern C++20 for Linux musicians. Designed to serve as the definitive Linux alternative to commercial drum samplers, GlorpDrums delivers zero-copy sample playback, multi-zone articulations, comprehensive e-drum calibration, per-instrument sound shaping, and multi-channel stem routing for professional music production.

![GlorpDrums Standalone Interface](docs/screenshot.png)

---

## Key Features

### 🥁 Native GlorpKit Format (`.glorpkit`)
* **Single-File Distribution**: Eliminates sprawling folders of thousands of loose WAV files by packaging full drum kits into single, standardized `.glorpkit` archives.
* **ZIP64 Architecture**: Fully supports massive multi-gigabyte sample libraries (e.g., CrocellKit 8.4 GB, multi-round-robin kits) without entry limits.
* **Zero-Copy Memory Mapping (`mmap`)**: Samples are streamed directly from the archive into memory buffers without disk extraction, ensuring near-instant kit loading and minimal memory footprint.
* **Legacy DrumGizmo Compatibility**: Seamlessly loads legacy DrumGizmo `drumkit.xml` and `midimap.xml` folder setups as a built-in fallback.

### 🎯 Multi-Zone Acoustic Engine
* **Articulated Pieces**:
  * **Snare**: Head, Rimshot, RimClick (cross-stick), and Rest/Muffled.
  * **Hi-Hat**: Closed, Semi-Open, Open, and Pedal Chick with automatic mutual choking.
  * **Cymbals**: Edge, Bow, Bell, and Choke articulations for crashes and rides.
  * **Toms**: Independent Head and Rim zones across rack and floor toms.
* **Round-Robin & Velocity Layers**: Multi-layer velocity switching and randomized round-robin variations prevent the "machine gun" effect.

### 📈 DrumGizmo-Style Velocity Curves & Preview
* **Dynamic Response Presets**: Choose between **Linear** (1:1 natural response), **Logarithmic** (boosts lighter ghost notes), **Exponential** (tightens hard hits), and **S-Curve** (exaggerates mid-range dynamics).
* **Live Visual Response Graph**: Inset display with coordinate axes (`in` on horizontal, `out` on vertical), reference grid lines, glowing response curve, and anchor points. Click directly on the preview to cycle or select curve presets.

### 🎛️ Sound Shaping & Multi-Bus DAW Routing
* **Per-Instrument Shaping**: Independent controls for **Volume**, **Pan**, **Pitch** transposition ($-12$ to $+12$ semitones with anti-aliased interpolation), and **Dynamics** response.
* **Multi-Bus DAW Stems**: Up to 16 discrete stereo output buses (Master, Kick, Snare, Hi-Hat, Toms, Cymbals, Percussion, Ambience/Room), enabling direct multitrack mixing in Reaper, Ardour, Bitwig, or Carla.

### ⚡ 6-Lane Drum Rhythm Ribbon
* Real-time visual MIDI event stream along the bottom featuring 6 dedicated horizontal lanes (`CYM`, `HH`, `TOM`, `SNR`, `KCK`, `ETC`).
* Simultaneous hits align vertically without visual overlap, with capsule width, color coding, and intensity accents scaling dynamically to hit velocity.

---

## E-Drum Calibration & Setup Wizard

GlorpDrums is purpose-built for electronic drum kits. An integrated **E-Drum Setup Wizard** calibrates pads, cymbals, and pedals in seconds:

![E-Drum Setup Wizard](docs/edrum_wizard_screenshot.png)

* **Hardware Presets**: Pre-configured MIDI mappings for **Roland**, **Yamaha**, **Alesis**, **Millenium**, **Efnote**, **General MIDI (GM)**, **ReaperDrummer**, and standard **DrumGizmo**.
* **Interactive Step-by-Step Calibration**: Simply strike each physical pad when prompted to automatically detect and assign MIDI note numbers.
* **Instant MIDI Learn**: Arm the `LEARN` button next to any instrument or zone to bind incoming MIDI notes on the fly.

---

## Roadmap

GlorpDrums is actively developed following an ambitious multi-phase roadmap:

### Phase 1 — E-Drum Input Engine (Upcoming)
* **Hi-Hat CC#4 Continuity**: Smooth real-time crossfading between Closed, Semi-Open, and Open zones driven by continuous pedal data (CC#4), with a toggle between Continuous Pedal Mode and Discrete Articulation Note Mode.
* **Poly-Pressure Cymbal Chokes**: Choke damping via Polyphonic Key Pressure (`0xA0`) and Channel Pressure (`0xD0`) with an anti-click fast-release envelope.

### Phase 2 — Full Per-Mic Mixer Tab (Upcoming)
* **Dual-View UI**: Seamless switching between **[KIT VIEW]** and **[MIXER VIEW]**.
* **Per-Mic Channel Strips**: Individual faders, mute, solo, and phase inversion for all recorded kit mics (Close, Room, Overhead Left/Right, Ambience, Bleed).
* **Built-In DSP FX**: 3-band parametric EQ, VCA drum compressor, warm tape/tube saturation, transient shaper, and algorithmic reverb sends.
* **Mixer Presets**: Save and load `.glorpmix` presets for different production styles.

---

## Requirements and Build Instructions

### Prerequisites (Linux)
* CMake 3.22 or higher
* C++20 compatible compiler (GCC 11+ or Clang 14+)
* Linux development headers (ALSA, JACK, FreeType, X11, OpenGL):
  ```bash
  sudo apt install build-essential cmake libasound2-dev libjack-jackd2-dev \
                   libfreetype-dev libx11-dev libxcomposite-dev libxcursor-dev \
                   libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
  ```

GlorpDrums supports **ALSA**, **JACK**, and **PipeWire** (via PipeWire-JACK or PipeWire-ALSA). For live e-drum performance, JACK / PipeWire-JACK is recommended for minimum latency.

### Building

1. Configure the build with CMake:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   ```
2. Compile binaries:
   ```bash
   cmake --build build --config Release -j$(nproc)
   ```

Outputs in `build/GlorpDrums_artefacts/Release/`:
* `Standalone/GlorpDrums` — Standalone executable
* `VST3/GlorpDrums.vst3` — VST3 plugin bundle
* `LV2/GlorpDrums.lv2` — LV2 plugin bundle

### Installation

To copy plugin bundles and the standalone binary to your standard user directories (`~/.vst3/`, `~/.lv2/`, and `~/.local/bin/`):
```bash
./tools/install.sh
```

Or to quickly update only the DAW plugin scan folders:
```bash
./tools/install_plugins.sh
```

---

## Drum Kit Locations

GlorpDrums automatically scans the following directories for `.glorpkit` files and DrumGizmo XML manifests:
* `~/.glorpdrums/kits/` (Recommended)
* `~/.config/GlorpDrums/kits/`
* `~/DrumGizmo/kits/`
* `~/.local/share/drumgizmo/kits/`
* `~/Documents/drumgizmo/kits/`
* Custom directories configured via **FILE $\to$ Scan Custom Folder...** or the `GLORP_KITS_PATH` environment variable.

---

## License

GlorpDrums is open-source software licensed under the GNU General Public License v3 (GPLv3).
