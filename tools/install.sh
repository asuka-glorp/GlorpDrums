#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build/cmake/GlorpDrums_artefacts/Release"

echo "=== Installing GlorpDrums ==="

mkdir -p "$HOME/.vst3"
mkdir -p "$HOME/.lv2"
mkdir -p "$HOME/.local/bin"
mkdir -p "$HOME/.config/GlorpDrums/kits"
mkdir -p "$HOME/.config/GlorpDrums/presets"

# 1. VST3 bundle
if [ -d "${BUILD_DIR}/VST3/GlorpDrums.vst3" ]; then
    rm -rf "$HOME/.vst3/GlorpDrums.vst3"
    cp -r "${BUILD_DIR}/VST3/GlorpDrums.vst3" "$HOME/.vst3/"
    echo "  [OK] Installed VST3: $HOME/.vst3/GlorpDrums.vst3"
else
    echo "  [WARN] VST3 bundle not found in build directory"
fi

# 2. LV2 bundle
if [ -d "${BUILD_DIR}/LV2/GlorpDrums.lv2" ]; then
    rm -rf "$HOME/.lv2/GlorpDrums.lv2"
    cp -r "${BUILD_DIR}/LV2/GlorpDrums.lv2" "$HOME/.lv2/"
    echo "  [OK] Installed LV2: $HOME/.lv2/GlorpDrums.lv2"
else
    echo "  [WARN] LV2 bundle not found in build directory"
fi

# 3. Standalone executable
if [ -f "${BUILD_DIR}/Standalone/GlorpDrums" ]; then
    cp "${BUILD_DIR}/Standalone/GlorpDrums" "$HOME/.local/bin/GlorpDrums"
    chmod +x "$HOME/.local/bin/GlorpDrums"
    echo "  [OK] Installed Standalone: $HOME/.local/bin/GlorpDrums"
else
    echo "  [WARN] Standalone binary not found in build directory"
fi

# 4. Built-in Demo Kit
if [ -d "${SCRIPT_DIR}/test/kits/GlorpDemoKit" ]; then
    rm -rf "$HOME/.config/GlorpDrums/kits/GlorpDemoKit"
    cp -r "${SCRIPT_DIR}/test/kits/GlorpDemoKit" "$HOME/.config/GlorpDrums/kits/"
    echo "  [OK] Installed Demo Kit: $HOME/.config/GlorpDrums/kits/GlorpDemoKit"
fi

echo "=== GlorpDrums installation completed successfully! ==="
