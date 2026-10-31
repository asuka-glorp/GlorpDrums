# Install rules for GlorpDrums.
#
# Formats are installed into the user's home by default so the plugin is usable
# without root. Pass -DGLORP_INSTALL_PREFIX=/usr to do a system-wide install
# instead, which is what the packaging step wants.

# Platform-specific install destinations
if(GLORP_IS_WINDOWS)
    if(GLORP_INSTALL_PREFIX)
        set(GLORP_DEST "${GLORP_INSTALL_PREFIX}")
    else()
        # Default to Program Files for system-wide, or LOCALAPPDATA for user install
        # When running installer (NSIS), it will set GLORP_INSTALL_PREFIX appropriately
        set(GLORP_DEST "C:/Program Files/GlorpDrums")
    endif()

    # Windows plugin directories
    set(GLORP_VST3_DEST "${GLORP_DEST}/VST3")
    set(GLORP_CLAP_DEST "${GLORP_DEST}/CLAP")
    set(GLORP_BIN_DEST "${GLORP_DEST}/bin")
else()
    if(GLORP_INSTALL_PREFIX)
        set(GLORP_DEST "${GLORP_INSTALL_PREFIX}")
    else()
        set(GLORP_DEST "$ENV{HOME}/.local/share")
    endif()

    set(GLORP_VST3_DEST "${GLORP_DEST}/vst3")
    set(GLORP_CLAP_DEST "${GLORP_DEST}/clap")
    set(GLORP_BIN_DEST "${GLORP_DEST}/bin")
endif()

message(STATUS "GlorpDrums install destination: ${GLORP_DEST}")

# JUCE emits per-format install targets named <target>_<format>.
if(GLORP_BUILD_VST3)
    install(TARGETS GlorpDrums_VST3
        BUNDLE  DESTINATION "${GLORP_VST3_DEST}"
        LIBRARY DESTINATION "${GLORP_VST3_DEST}"
        ARCHIVE DESTINATION "${GLORP_VST3_DEST}")

    if(NOT GLORP_IS_WINDOWS)
        # VST3 bundles are case sensitive about their own location, so a symlink into
        # the system dir is the only way a non-root install reaches every host.
        install(CODE "
            file(MAKE_DIRECTORY \"$ENV{HOME}/.vst3\")
            if(NOT EXISTS \"$ENV{HOME}/.vst3/GlorpDrums.vst3\")
                execute_process(COMMAND \${CMAKE_COMMAND} -E
                    create_symlink \"${GLORP_VST3_DEST}/GlorpDrums.vst3\"
                    \"$ENV{HOME}/.vst3/GlorpDrums.vst3\")
            endif()
        ")
    endif()
endif()

if(GLORP_BUILD_CLAP)
    if(TARGET GlorpDrums_CLAP)
        install(TARGETS GlorpDrums_CLAP
            BUNDLE  DESTINATION "${GLORP_CLAP_DEST}"
            LIBRARY DESTINATION "${GLORP_CLAP_DEST}"
            ARCHIVE DESTINATION "${GLORP_CLAP_DEST}")

        if(NOT GLORP_IS_WINDOWS)
            # CLAP symlink for Linux hosts
            install(CODE "
                file(MAKE_DIRECTORY \"$ENV{HOME}/.clap\")
                if(NOT EXISTS \"$ENV{HOME}/.clap/GlorpDrums.clap\")
                    execute_process(COMMAND \${CMAKE_COMMAND} -E
                        create_symlink \"${GLORP_CLAP_DEST}/GlorpDrums.clap\"
                        \"$ENV{HOME}/.clap/GlorpDrums.clap\")
                endif()
            ")
        endif()
    else()
        message(WARNING "GLORP_BUILD_CLAP is ON but GlorpDrums_CLAP target not found — CLAP not installed")
    endif()
endif()

if(GLORP_BUILD_LV2 AND NOT GLORP_IS_WINDOWS)
    install(TARGETS GlorpDrums_LV2
        BUNDLE  DESTINATION "${GLORP_DEST}/lv2"
        LIBRARY DESTINATION "${GLORP_DEST}/lv2"
        ARCHIVE DESTINATION "${GLORP_DEST}/lv2")

    install(CODE "
        file(MAKE_DIRECTORY \"$ENV{HOME}/.lv2\")
        if(NOT EXISTS \"$ENV{HOME}/.lv2/GlorpDrums.lv2\")
            execute_process(COMMAND \${CMAKE_COMMAND} -E
                create_symlink \"${GLORP_DEST}/lv2/GlorpDrums.lv2\"
                \"$ENV{HOME}/.lv2/GlorpDrums.lv2\")
        endif()
    ")
endif()

if(GLORP_BUILD_STANDALONE)
    install(TARGETS GlorpDrums_Standalone
        RUNTIME DESTINATION "${GLORP_BIN_DEST}")
endif()

if(GLORP_BUILD_KIT_EDITOR)
    install(TARGETS GlorpKitEditor
        RUNTIME DESTINATION "${GLORP_BIN_DEST}")
endif()

# Application Icons & Desktop Launchers
install(FILES "assets/glorpdrums_icon.png" "assets/glorpkit_studio_icon.png"
    DESTINATION "${GLORP_DEST}/icons")

if(NOT GLORP_IS_WINDOWS)
    # Linux .desktop files
    install(CODE "
        file(MAKE_DIRECTORY \"$ENV{HOME}/.local/share/icons/hicolor/256x256/apps\")
        file(MAKE_DIRECTORY \"$ENV{HOME}/.local/share/applications\")
        file(COPY \"\${CMAKE_CURRENT_SOURCE_DIR}/assets/glorpdrums_icon.png\"
             DESTINATION \"$ENV{HOME}/.local/share/icons/hicolor/256x256/apps\")
        file(COPY \"\${CMAKE_CURRENT_SOURCE_DIR}/assets/glorpkit_studio_icon.png\"
             DESTINATION \"$ENV{HOME}/.local/share/icons/hicolor/256x256/apps\")
        file(WRITE \"$ENV{HOME}/.local/share/applications/glorpdrums.desktop\"
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
        file(WRITE \"$ENV{HOME}/.local/share/applications/glorpkit_studio.desktop\"
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
else()
    # Windows: Start Menu shortcuts created by NSIS installer (tools/install_windows.nsi)
    # No CMake install-time shortcut creation needed
    message(STATUS "Windows: Start Menu shortcuts will be created by NSIS installer")
endif()

# User configuration directory. Created on first run either way, but shipping it
# means the MIDI map presets are there before the plugin ever loads.
if(NOT GLORP_IS_WINDOWS)
    install(DIRECTORY DESTINATION "$ENV{HOME}/.config/GlorpDrums")
    install(DIRECTORY presets/ DESTINATION "$ENV{HOME}/.config/GlorpDrums/presets")
else()
    # Windows config dir will be created on first run via JUCE's File::getSpecialLocation
    # NSIS installer can pre-create it if desired
    message(STATUS "Windows: Config directory created on first run via JUCE")
endif()