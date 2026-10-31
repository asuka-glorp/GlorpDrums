cmake_minimum_required(VERSION 3.22)

project(GlorpDrums VERSION 0.2.0 LANGUAGES C CXX)

#==================================================================================
# Options
#==================================================================================

option(GLORP_BUILD_VST3        "Build the VST3 target"        ON)
option(GLORP_BUILD_CLAP        "Build the CLAP target (experimental)" OFF)
option(GLORP_BUILD_LV2         "Build the LV2 target (Linux only)" ON)
option(GLORP_BUILD_STANDALONE  "Build the standalone app"    ON)
option(GLORP_BUILD_KIT_EDITOR  "Build standalone GlorpKit editor application" ON)
option(GLORP_BUILD_TESTS       "Build the headless test suite" ON)

# Platform detection
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(GLORP_IS_WINDOWS 1)
    message(STATUS "Platform: Windows")
else()
    set(GLORP_IS_WINDOWS 0)
    message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
endif()

# Number of stereo output buses reserved for instruments.
#
# These cannot be added or removed at runtime: an LV2 bundle's port list is baked
# into its .ttl when the host scans the plugin, and JUCE's LV2 wrapper has no way to
# renegotiate it afterwards. VST3 *could* change at runtime, but keeping the two
# formats identical matters more than the tidiness of unused buses.
#
# 32 covers the largest kits in common use (CrocellKit_full has 27 instruments).
# Instruments past this many share the final bus rather than being dropped, so a
# bigger kit is never unplayable, just less separable.
#
# Rebuild with e.g. -DGLORP_MAX_INSTRUMENT_BUSES=8 for a leaner alternative if a
# host struggles with many outputs.
set(GLORP_MAX_INSTRUMENT_BUSES 32 CACHE STRING "Stereo output buses reserved for instruments")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

#==================================================================================
# JUCE
#==================================================================================

# Build requirements on Linux, listed here so a new machine is set up in one go:
#   libX11-devel libXinerama-devel libXcursor-devel libXi-devel
#   libXrandr-devel libXcomposite-devel alsa-lib-devel
# JUCE's X11 code includes headers from the first three by name, so they are not
# optional. Everything below builds against the real X11 feature set.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    add_subdirectory(libs/JUCE)
else()
    include(FetchContent)
    message(STATUS "Local libs/JUCE not found; downloading JUCE 8.0.6 via FetchContent...")
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.3.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

#==================================================================================
# Plugin targets
#==================================================================================

set(GLORP_PLUGIN_FORMATS "")

if(GLORP_BUILD_VST3)
    list(APPEND GLORP_PLUGIN_FORMATS VST3)
endif()

if(GLORP_BUILD_CLAP)
    list(APPEND GLORP_PLUGIN_FORMATS CLAP)
endif()

# LV2 is Linux-only; disable on Windows
if(GLORP_BUILD_LV2 AND NOT GLORP_IS_WINDOWS)
    list(APPEND GLORP_PLUGIN_FORMATS LV2)
elseif(GLORP_BUILD_LV2 AND GLORP_IS_WINDOWS)
    message(WARNING "LV2 is not supported on Windows; disabling GLORP_BUILD_LV2")
endif()

if(GLORP_BUILD_STANDALONE)
    list(APPEND GLORP_PLUGIN_FORMATS Standalone)
endif()

if(NOT GLORP_PLUGIN_FORMATS)
    message(FATAL_ERROR "At least one of GLORP_BUILD_VST3 / GLORP_BUILD_CLAP / GLORP_BUILD_LV2 / GLORP_BUILD_STANDALONE must be ON")
endif()

#==================================================================================
# Bundled assets
#==================================================================================

# The logo and fonts, as a static library so the plugin binaries and the
# standalone executable can share them. Defined before the plugin target links it.
juce_add_binary_data(GlorpDrumsAssets
    COMPANY_NAME  "GlorpDrums"
    BINARY_NAME   "GlorpDrumsAssets"
    HEADER_NAME   "GlorpDrumsAssets.h"
    NAMESPACE     GlorpAssets
    SOURCES
        resources/logo.svg
        resources/fonts/PatrickHand-Regular.ttf
        resources/fonts/LilitaOne-Regular.ttf
        assets/kick.png
        assets/snare.png
        assets/hihat.png
        assets/tom_1.png
        assets/tom_2.png
        assets/f_tom.png
        assets/crash_1.png
        assets/crash_2.png
        assets/ride.png
        assets/logo.png
        assets/glorpdrums_icon.png
        assets/glorpkit_studio_icon.png
)

#==================================================================================
# Plugin target
#==================================================================================

juce_add_plugin(GlorpDrums
    PRODUCT_NAME            "GlorpDrums"
    COMPANY_NAME            "GlorpDrums"
    BUNDLE_ID               "org.glorpdrums.GlorpDrums"
    PLUGIN_MANUFACTURER_CODE Glrp
    PLUGIN_CODE             Glrp
    FORMATS                ${GLORP_PLUGIN_FORMATS}
    LV2URI                  "https://github.com/glorpdrums/GlorpDrums"
    VST3_CATEGORIES         "Instrument" "Sampler" "Drum"
    COPY_PLUGIN_AFTER_BUILD FALSE
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    ICON_BIG                assets/glorpdrums_icon.png
)

string(REPLACE ";" "," GLORP_PLUGIN_FORMATS_STR "${GLORP_PLUGIN_FORMATS}")

juce_generate_juce_header(GlorpDrums)

#==================================================================================
# Shared sources
#==================================================================================

target_sources(GlorpDrums PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/ParameterManager.cpp
    src/plugin/MidiRouter.cpp
    src/plugin/AudioEngine.cpp
    src/plugin/PluginEditor.h
    src/core/XmlLite.cpp
    src/core/DrumKit.cpp
    src/core/KitScanner.cpp
    src/core/SampleCache.cpp
    src/core/Sampler.cpp
    src/core/MidiMap.cpp
    src/core/Settings.cpp
    src/core/GlorpKit.cpp
    src/core/GlorpKitWriter.cpp
    src/core/ChannelFX.cpp
    src/core/BleedMatrix.cpp
    src/core/AcousticCrosstalk.cpp
    src/practice/TempoDetector.cpp
    src/practice/Metronome.cpp
    src/practice/AudioRecorder.cpp
    src/practice/MidiRecorder.cpp
    src/practice/BackingTrackPlayer.cpp
    src/practice/SongDatabase.cpp
    src/practice/SetlistPlayer.cpp
    src/ui/Editor.cpp
    src/ui/DrumKitView.cpp
    src/ui/CalloutBox.cpp
    src/ui/Knob.cpp
    src/ui/Scope.cpp
    src/ui/EDrumWizard.cpp
    src/ui/MixerView.cpp
    src/ui/RecordPracticeView.cpp
)

# Bundled logo and fonts, as a static library so both the VST3/LV2 binaries and
# the standalone executable can share them.
target_link_libraries(GlorpDrums PRIVATE GlorpDrumsAssets)

target_include_directories(GlorpDrums PRIVATE src src/core src/plugin src/ui src/practice)

target_compile_definitions(GlorpDrums PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_OGGVORBIS=1
    JUCE_USE_FLAC=1
    GLORP_MAX_INSTRUMENT_BUSES=${GLORP_MAX_INSTRUMENT_BUSES}
)

target_link_libraries(GlorpDrums
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

if(GLORP_BUILD_STANDALONE)
    if(GLORP_IS_WINDOWS)
        # WASAPI for Windows standalone audio
        target_compile_definitions(GlorpDrums PRIVATE JUCE_WASAPI=1)
    else()
        # ALSA + JACK for Linux standalone audio
        # JACK is enabled for live performance and low-latency monitoring.
        target_compile_definitions(GlorpDrums PRIVATE JUCE_ALSA=1 JUCE_JACK=1)
    endif()
    target_link_libraries(GlorpDrums PRIVATE juce::juce_audio_devices)
endif()

#==================================================================================
# GlorpKit Editor Standalone Application
#==================================================================================

if(GLORP_BUILD_KIT_EDITOR)
    juce_add_gui_app(GlorpKitEditor
        PRODUCT_NAME "GlorpKitEditor"
        COMPANY_NAME "GlorpDrums"
        BUNDLE_ID "org.glorpdrums.GlorpKitEditor"
        ICON_BIG assets/glorpkit_studio_icon.png
    )

    target_sources(GlorpKitEditor PRIVATE
        src/editor_app/Main.cpp
        src/editor_app/GlorpKitEditorWindow.cpp
        src/editor_app/GlorpKitProject.cpp
        src/core/GlorpKitWriter.cpp
        src/core/GlorpKit.cpp
        src/core/DrumKit.cpp
        src/core/XmlLite.cpp
        src/core/SampleCache.cpp
        src/core/Sampler.cpp
        src/core/PitchShifter.cpp
        src/core/BleedMatrix.cpp
        src/core/AcousticCrosstalk.cpp
        src/core/ChannelFX.cpp
    )

    target_include_directories(GlorpKitEditor PRIVATE
        src
        src/core
        src/ui
        src/practice
        src/editor_app
    )

    target_compile_definitions(GlorpKitEditor PRIVATE
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        GLORP_BUILD_DATE="${GLORP_BUILD_DATE}"
        GLORP_MAX_INSTRUMENT_BUSES=${GLORP_MAX_INSTRUMENT_BUSES}
    )

    if(GLORP_IS_WINDOWS)
        target_compile_definitions(GlorpKitEditor PRIVATE JUCE_WASAPI=1)
    else()
        target_compile_definitions(GlorpKitEditor PRIVATE JUCE_ALSA=1 JUCE_JACK=1)
    endif()

    target_link_libraries(GlorpKitEditor PRIVATE
        GlorpDrumsAssets
        juce::juce_core
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_warning_flags
    )
endif()

#==================================================================================
# Tests
#==================================================================================

if(GLORP_BUILD_TESTS)
    enable_testing()

    # Core tests deliberately avoid JUCE entirely, so format bugs are found
    # without a 10 minute plugin build.
    add_executable(glorp_core_tests test/core_tests.cpp)
    target_sources(glorp_core_tests PRIVATE
        src/core/XmlLite.cpp
        src/core/GlorpKit.cpp
        src/core/GlorpKitWriter.cpp
        src/core/DrumKit.cpp
        src/core/KitScanner.cpp
        src/core/PitchShifter.cpp
        src/core/BleedMatrix.cpp
        src/core/AcousticCrosstalk.cpp
    )
    target_include_directories(glorp_core_tests PRIVATE src/core)
    target_compile_definitions(glorp_core_tests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(glorp_core_tests PRIVATE juce::juce_core juce::juce_recommended_warning_flags)
    add_test(NAME core COMMAND glorp_core_tests)

    foreach(ui_test_target test_editor test_zones_knobs diagnose_crocell_knobs verify_fixes)
        add_executable(${ui_test_target} EXCLUDE_FROM_ALL test/${ui_test_target}.cpp
            src/editor_app/GlorpKitProject.cpp
            src/editor_app/GlorpKitEditorWindow.cpp
        )
        target_include_directories(${ui_test_target} PRIVATE
            src src/core src/plugin src/ui src/practice src/editor_app
            $<TARGET_PROPERTY:GlorpDrums,INCLUDE_DIRECTORIES>
        )
        target_compile_definitions(${ui_test_target} PRIVATE
            $<TARGET_PROPERTY:GlorpDrums,COMPILE_DEFINITIONS>
        )
        target_link_libraries(${ui_test_target} PRIVATE
            GlorpDrums
            GlorpDrumsAssets
            juce::juce_recommended_warning_flags
        )
    endforeach()
endif()

#==================================================================================
# Install
#==================================================================================

include(cmake/install.cmake)
