cmake_minimum_required(VERSION 3.22)

project(GlorpDrums VERSION 0.1.0 LANGUAGES C CXX)

#==================================================================================
# Options
#==================================================================================

option(GLORP_BUILD_VST3        "Build the VST3 target"        ON)
option(GLORP_BUILD_LV2         "Build the LV2 target"         ON)
option(GLORP_BUILD_STANDALONE  "Build the standalone app"    ON)
option(GLORP_BUILD_TESTS       "Build the headless test suite" ON)

# Number of stereo output buses reserved for instruments.
#
# These cannot be added or removed at runtime: an LV2 bundle's port list is baked
# into its .ttl when the host scans the plugin, and JUCE's LV2 wrapper has no way to
# renegotiate it afterwards. VST3 *could* change at runtime, but keeping the two
# formats identical matters more than the tidiness of unused buses.
#
# VST3 allows 64 buses, so this can safely go up. Rebuild with e.g.
#   -DGLORP_MAX_INSTRUMENT_BUSES=8
# for a leaner alternative if a host struggles.
set(GLORP_MAX_INSTRUMENT_BUSES 16 CACHE STRING "Stereo output buses reserved for instruments")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

#==================================================================================
# JUCE
#==================================================================================

add_subdirectory(libs/JUCE)

#==================================================================================
# Plugin targets
#==================================================================================

set(GLORP_PLUGIN_FORMATS "")
if(GLORP_BUILD_VST3)       list(APPEND GLORP_PLUGIN_FORMATS VST3) endif()
if(GLORP_BUILD_LV2)        list(APPEND GLORP_PLUGIN_FORMATS LV2)  endif()
if(GLORP_BUILD_STANDALONE) list(APPEND GLORP_PLUGIN_FORMATS Standalone) endif()

if(NOT GLORP_PLUGIN_FORMATS)
    message(FATAL_ERROR "At least one of GLORP_BUILD_VST3 / GLORP_BUILD_LV2 / GLORP_BUILD_STANDALONE must be ON")
endif()

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
)

string(REPLACE ";" "," GLORP_PLUGIN_FORMATS_STR "${GLORP_PLUGIN_FORMATS}")

juce_generate_juce_header(GlorpDrums)

#==================================================================================
# Shared sources
#==================================================================================

target_sources(GlorpDrums PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/core/XmlLite.cpp
    src/core/DrumKit.cpp
    src/core/KitScanner.cpp
    src/core/SampleCache.cpp
    src/core/PitchShifter.cpp
    src/core/Sampler.cpp
    src/core/MidiMap.cpp
    src/core/Settings.cpp
    src/ui/DrumKitView.cpp
    src/ui/CalloutBox.cpp
    src/ui/Knob.cpp
    src/ui/Scope.cpp
    src/ui/Editor.cpp
    src/ui/Theme.cpp
)

target_include_directories(GlorpDrums PRIVATE src src/core)

target_compile_definitions(GlorpDrums PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    GLORP_MAX_INSTRUMENT_BUSES=${GLORP_MAX_INSTRUMENT_BUSES}
)

target_link_libraries(GlorpDrums
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(GLORP_BUILD_STANDALONE)
    # ALSA MIDI so the standalone app can be played with a real drum pad.
    target_compile_definitions(GlorpDrums PRIVATE JUCE_ALSA=1 JUCE_JACK=0)
    target_link_libraries(GlorpDrums PRIVATE juce::juce_audio_devices)
endif()

# The logo and fonts are needed by the editor.
juce_add_binary_data(GlorpDrumsAssets
    COMPANY_NAME "GlorpDrums"
    BINARY_NAME  "GlorpDrumsAssets"
    HEADER_NAME  "GlorpDrumsAssets.h"
    NAMESPACE    GlorpAssets
)
target_sources(GlorpDrumsAssets
    resources/logo.svg
    resources/fonts/PatrickHand-Regular.ttf
    resources/fonts/LilitaOne-Regular.ttf
)
target_include_directories(GlorpDrumsAssets PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

#==================================================================================
# Standalone app wrapper
#==================================================================================

if(GLORP_BUILD_STANDALONE)
    juce_add_console_app(GlorpDrumsStandalone
        PRODUCT_NAME "GlorpDrums"
        COMPANY_NAME "GlorpDrums"
        BUNDLE_ID    "org.glorpdrums.GlorpDrums.Standalone"
    )

    target_sources(GlorpDrumsStandalone PRIVATE src/plugin/Standalone.cpp)

    target_link_libraries(GlorpDrumsStandalone
        PRIVATE
            GlorpDrums
            GlorpDrumsAssets
            juce::juce_audio_utils
    )

    target_compile_definitions(GlorpDrumsStandalone PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
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
        src/core/DrumKit.cpp
        src/core/KitScanner.cpp
        src/core/PitchShifter.cpp
    )
    target_include_directories(glorp_core_tests PRIVATE src/core)
    target_link_libraries(glorp_core_tests PRIVATE juce::juce_recommended_warning_flags)
    add_test(NAME core COMMAND glorp_core_tests)
endif()

#==================================================================================
# Install
#==================================================================================

include(cmake/install.cmake)
