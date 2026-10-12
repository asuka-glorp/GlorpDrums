#!/usr/bin/env bash
set -e

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_DIR/build/GlorpDrums_artefacts/Release"

echo "Installing GlorpDrums plugins to user directories..."

mkdir -p ~/.vst3 ~/.lv2

if [ -d "$BUILD_DIR/VST3/GlorpDrums.vst3" ]; then
    rm -rf ~/.vst3/GlorpDrums.vst3
    cp -r "$BUILD_DIR/VST3/GlorpDrums.vst3" ~/.vst3/
    echo "-> Updated ~/.vst3/GlorpDrums.vst3"
fi

if [ -d "$BUILD_DIR/LV2/GlorpDrums.lv2" ]; then
    rm -rf ~/.lv2/GlorpDrums.lv2
    cp -r "$BUILD_DIR/LV2/GlorpDrums.lv2" ~/.lv2/
    echo "-> Updated ~/.lv2/GlorpDrums.lv2"
fi

echo "Done! Please rescan plugins in your DAW."
