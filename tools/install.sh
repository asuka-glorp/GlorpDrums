#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ -d "${SCRIPT_DIR}/build/GlorpDrums_artefacts/Release" ]; then
    BUILD_DIR="${SCRIPT_DIR}/build/GlorpDrums_artefacts/Release"
else
    BUILD_DIR="${SCRIPT_DIR}/build/cmake/GlorpDrums_artefacts/Release"
fi

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

# 3b. GlorpKit Studio Standalone executable
if [ -f "${SCRIPT_DIR}/build/GlorpKitEditor_artefacts/Release/GlorpKitEditor" ]; then
    cp "${SCRIPT_DIR}/build/GlorpKitEditor_artefacts/Release/GlorpKitEditor" "$HOME/.local/bin/GlorpKitEditor"
    chmod +x "$HOME/.local/bin/GlorpKitEditor"
    echo "  [OK] Installed GlorpKitEditor: $HOME/.local/bin/GlorpKitEditor"
fi

# 3c. Application Icons & Desktop Launchers
mkdir -p "$HOME/.local/share/icons/hicolor/256x256/apps"
mkdir -p "$HOME/.local/share/pixmaps"
mkdir -p "$HOME/.local/share/applications"

cp "${SCRIPT_DIR}/assets/glorpdrums_icon.png" "$HOME/.local/share/icons/hicolor/256x256/apps/glorpdrums.png" 2>/dev/null || true
cp "${SCRIPT_DIR}/assets/glorpdrums_icon.png" "$HOME/.local/share/pixmaps/glorpdrums.png" 2>/dev/null || true
cp "${SCRIPT_DIR}/assets/glorpkit_studio_icon.png" "$HOME/.local/share/icons/hicolor/256x256/apps/glorpkit_studio.png" 2>/dev/null || true
cp "${SCRIPT_DIR}/assets/glorpkit_studio_icon.png" "$HOME/.local/share/pixmaps/glorpkit_studio.png" 2>/dev/null || true

cat << 'EOF' > "$HOME/.local/share/applications/glorpdrums.desktop"
[Desktop Entry]
Name=GlorpDrums
Comment=Acoustic Drum Sampler
Exec=GlorpDrums
Icon=glorpdrums
Terminal=false
Type=Application
Categories=AudioVideo;Audio;
StartupWMClass=GlorpDrums
EOF

cat << 'EOF' > "$HOME/.local/share/applications/glorpkit_studio.desktop"
[Desktop Entry]
Name=GlorpKit Studio
Comment=.glorpkit Drum Kit Creator & Editor
Exec=GlorpKitEditor
Icon=glorpkit_studio
Terminal=false
Type=Application
Categories=AudioVideo;Audio;
StartupWMClass=GlorpKitEditor
EOF

# 4. Built-in Demo Kit
if [ -d "${SCRIPT_DIR}/test/kits/GlorpDemoKit" ]; then
    rm -rf "$HOME/.config/GlorpDrums/kits/GlorpDemoKit"
    cp -r "${SCRIPT_DIR}/test/kits/GlorpDemoKit" "$HOME/.config/GlorpDrums/kits/"
    echo "  [OK] Installed Demo Kit: $HOME/.config/GlorpDrums/kits/GlorpDemoKit"
fi

echo "=== GlorpDrums installation completed successfully! ==="
