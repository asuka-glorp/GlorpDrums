# Install rules for GlorpDrums.
#
# Formats are installed into the user's home by default so the plugin is usable
# without root. Pass -DGLORP_INSTALL_PREFIX=/usr to do a system-wide install
# instead, which is what the packaging step wants.

if(GLORP_INSTALL_PREFIX)
    set(GLORP_DEST "${GLORP_INSTALL_PREFIX}")
else()
    set(GLORP_DEST "$ENV{HOME}/.local/share")
endif()

message(STATUS "GlorpDrums install destination: ${GLORP_DEST}")

# JUCE emits per-format install targets named <target>_<format>.
if(GLORP_BUILD_VST3)
    install(TARGETS GlorpDrums_VST3
        BUNDLE  DESTINATION "${GLORP_DEST}/vst3"
        LIBRARY DESTINATION "${GLORP_DEST}/vst3"
        ARCHIVE DESTINATION "${GLORP_DEST}/vst3")

    # VST3 bundles are case sensitive about their own location, so a symlink into
    # the system dir is the only way a non-root install reaches every host.
    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{HOME}/.vst3\")
        if(NOT EXISTS \"\$ENV{HOME}/.vst3/GlorpDrums.vst3\")
            execute_process(COMMAND \${CMAKE_COMMAND} -E
                create_symlink \"${GLORP_DEST}/vst3/GlorpDrums.vst3\"
                \"\$ENV{HOME}/.vst3/GlorpDrums.vst3\")
        endif()
    ")
endif()

if(GLORP_BUILD_LV2)
    install(TARGETS GlorpDrums_LV2
        BUNDLE  DESTINATION "${GLORP_DEST}/lv2"
        LIBRARY DESTINATION "${GLORP_DEST}/lv2"
        ARCHIVE DESTINATION "${GLORP_DEST}/lv2")

    install(CODE "
        file(MAKE_DIRECTORY \"\$ENV{HOME}/.lv2\")
        if(NOT EXISTS \"\$ENV{HOME}/.lv2/GlorpDrums.lv2\")
            execute_process(COMMAND \${CMAKE_COMMAND} -E
                create_symlink \"${GLORP_DEST}/lv2/GlorpDrums.lv2\"
                \"\$ENV{HOME}/.lv2/GlorpDrums.lv2\")
        endif()
    ")
endif()

if(GLORP_BUILD_STANDALONE)
    install(TARGETS GlorpDrums_Standalone
        RUNTIME DESTINATION "${GLORP_DEST}/bin")
endif()

if(GLORP_BUILD_KIT_EDITOR)
    install(TARGETS GlorpKitEditor
        RUNTIME DESTINATION "${GLORP_DEST}/bin")
endif()

# Application Icons & Desktop Launchers
install(FILES "assets/glorpdrums_icon.png" "assets/glorpkit_studio_icon.png"
    DESTINATION "${GLORP_DEST}/icons")
install(CODE "
    file(MAKE_DIRECTORY \"\$ENV{HOME}/.local/share/icons/hicolor/256x256/apps\")
    file(MAKE_DIRECTORY \"\$ENV{HOME}/.local/share/applications\")
    file(COPY \"\${CMAKE_CURRENT_SOURCE_DIR}/assets/glorpdrums_icon.png\"
         DESTINATION \"\$ENV{HOME}/.local/share/icons/hicolor/256x256/apps\")
    file(COPY \"\${CMAKE_CURRENT_SOURCE_DIR}/assets/glorpkit_studio_icon.png\"
         DESTINATION \"\$ENV{HOME}/.local/share/icons/hicolor/256x256/apps\")
    file(WRITE \"\$ENV{HOME}/.local/share/applications/glorpdrums.desktop\"
\"[Desktop Entry]
Name=GlorpDrums
Comment=Acoustic Drum Sampler
Exec=GlorpDrums
Icon=glorpdrums_icon
Terminal=false
Type=Application
Categories=AudioVideo;Audio;
StartupWMClass=GlorpDrums
\")
    file(WRITE \"\$ENV{HOME}/.local/share/applications/glorpkit_studio.desktop\"
\"[Desktop Entry]
Name=GlorpKit Studio
Comment=.glorpkit Drum Kit Creator & Editor
Exec=GlorpKitEditor
Icon=glorpkit_studio_icon
Terminal=false
Type=Application
Categories=AudioVideo;Audio;
StartupWMClass=GlorpKitEditor
\")
")

# User configuration directory. Created on first run either way, but shipping it
# means the MIDI map presets are there before the plugin ever loads.
install(DIRECTORY DESTINATION "$ENV{HOME}/.config/GlorpDrums")
install(DIRECTORY presets/ DESTINATION "$ENV{HOME}/.config/GlorpDrums/presets")
