# Contributing to GlorpDrums

Thank you for your interest in contributing to GlorpDrums! This document provides guidelines and instructions for contributing.

## Table of Contents
- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Making Changes](#making-changes)
- [Testing](#testing)
- [Submitting Changes](#submitting-changes)
- [Code Style](#code-style)
- [Architecture Overview](#architecture-overview)

## Code of Conduct

Please read and follow our [Code of Conduct](CODE_OF_CONDUCT.md).

## Getting Started

### Prerequisites

- Linux (Ubuntu 22.04+, Fedora 38+, Arch, etc.)
- CMake 3.22+
- C++20 compatible compiler (GCC 11+ or Clang 14+)
- JUCE 8.0.6 (fetched automatically via CMake)
- Audio development headers:
  ```bash
  sudo apt install build-essential cmake pkg-config \
                   libasound2-dev libjack-jackd2-dev \
                   libfreetype-dev libfontconfig1-dev libgl1-mesa-dev \
                   libx11-dev libxcomposite-dev libxcursor-dev \
                   libxext-dev libxinerama-dev libxrandr-dev libxrender-dev
  ```

### Building

```bash
# Clone the repository
git clone https://github.com/glorpdrums/GlorpDrums.git
cd GlorpDrums

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run tests
ctest --test-dir build --output-on-failure
```

## Development Setup

### IDE Configuration

#### VS Code
The repository includes `.vscode/` settings. Install the "C/C++" extension and "CMake Tools" extension.

#### CLion
Open the project root as a CMake project. CLion will detect the CMakeLists.txt automatically.

### Code Formatting

We use `clang-format` for consistent code style. Before committing:

```bash
# Check formatting
clang-format --dry-run --Werror src/

# Apply formatting
clang-format -i src/**/*.h src/**/*.cpp
```

### Static Analysis

We use `clang-tidy` for static analysis:

```bash
# Run clang-tidy (requires compiled build for compile_commands.json)
clang-tidy -p build src/**/*.cpp
```

## Making Changes

### Branch Naming

- `feature/<description>` — New features
- `fix/<description>` — Bug fixes
- `refactor/<description>` — Code improvements
- `docs/<description>` — Documentation updates
- `test/<description>` — Test additions

### Commit Messages

Follow conventional commits format:

```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

Types: `feat`, `fix`, `refactor`, `docs`, `test`, `chore`, `style`, `perf`

Examples:
```
feat(sampler): add round-robin voice allocation
fix(mixer): resolve bleed matrix memory leak
refactor(core): centralize constants in Config.h
docs(readme): update build instructions for Fedora
```

### Pull Request Process

1. Fork the repository
2. Create a feature branch
3. Make your changes with tests
4. Ensure all CI checks pass
5. Submit a PR with a clear description

## Testing

### Running Tests

```bash
# All tests
ctest --test-dir build --output-on-failure

# Specific test
./build/glorp_core_tests
```

### Writing Tests

- Core engine tests go in `test/core_tests.cpp`
- UI tests go in `test/test_editor.cpp`, `test/test_zones_knobs.cpp`, etc.
- New test executables are added in `CMakeLists.txt` under `GLORP_BUILD_TESTS`

### Test Guidelines

- Test audio thread code with mock time, not real audio
- Use deterministic random seeds for reproducible tests
- Keep tests fast (< 100ms each)
- Test edge cases: empty kits, max voices, zero channels

## Code Style

### C++ Guidelines

- **Standard**: C++20
- **Formatting**: `.clang-format` (based on JUCE style)
- **Analysis**: `.clang-tidy` (modern C++ checks)
- **Naming**:
  - Classes/Structs: `PascalCase`
  - Functions/Methods: `camelCase`
  - Variables: `camelCase`
  - Private members: `camelCase_` (trailing underscore)
  - Constants: `SCREAMING_SNAKE_CASE`
  - Namespaces: `lowercase`

### JUCE-Specific

- Use JUCE containers (`juce::Array`, `juce::String`, `juce::OwnedArray`)
- Prefer `juce::ReferenceCountedObject` for shared ownership
- Audio thread: **no allocations, no locks, no exceptions**
- Use `juce::CriticalSection` for message-thread synchronization

### Architecture Principles

- **Single Responsibility**: Each class has one clear purpose
- **Audio Thread Safety**: Never allocate or lock on audio thread
- **Zero-Copy**: Prefer `mmap` and view-based access over copying
- **Testability**: Depend on interfaces, not concrete implementations

## Architecture Overview

### Core Modules (`src/core/`)

| Module | Purpose |
|--------|---------|
| `Sampler` | Voice allocation, sample playback, varispeed pitch |
| `DrumKit` | In-memory kit model (DrumGizmo + GlorpKit) |
| `GlorpKit` | `.glorpkit` ZIP64 reader/writer |
| `SampleCache` | Memory-mapped sample streaming |
| `ChannelFX` | Per-channel DSP (EQ, compressor, saturation) |
| `BleedMatrix` | Per-mic bleed gain matrix |
| `AcousticCrosstalk` | Physical bleed modeling |
| `MidiMap` | Note/CC mapping with presets |
| `Settings` | Persistent user preferences |

### Plugin (`src/plugin/`)

- `PluginProcessor` — JUCE `AudioProcessor` bridge
- `PluginEditor` — JUCE `AudioProcessorEditor` bridge

### UI (`src/ui/`)

- `Editor` — Main plugin window (Kit/Mixer/Practice views)
- `DrumKitView` — Hand-drawn kit visualization
- `MixerView` — Per-mic console with FX drawers
- `RecordPracticeView` — Practice suite UI
- `Theme` — Dual theme system (Sketch/Dark)

### Practice Suite (`src/practice/`)

- `BackingTrackPlayer` — Audio playback with waveform
- `TempoDetector` — Spectral flux BPM detection
- `Metronome` — Sample-accurate click engine
- `AudioRecorder` / `MidiRecorder` — Dual recording
- `SongDatabase` — Persistent song library
- `SetlistPlayer` — Sequential setlist with count-in

### GlorpKitEditor (`src/editor_app/`)

- Standalone `.glorpkit` v2.0 creator/editor
- 5-panel workflow: Header, Z-Layers, Canvas, Matrix, Inspector
- WYSIWYG piece positioning with custom PNG artwork
- Lossless round-trip editing

## Getting Help

- **Issues**: [GitHub Issues](https://github.com/glorpdrums/GlorpDrums/issues)
- **Discussions**: [GitHub Discussions](https://github.com/glorpdrums/GlorpDrums/discussions)
- **E-Drum Testing**: We need testers with physical e-drum kits!

## License

By contributing, you agree that your contributions will be licensed under the GPLv3 (same as the project).