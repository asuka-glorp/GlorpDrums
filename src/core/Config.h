/*
    Config.h - Centralized configuration constants for GlorpDrums.

    This header provides a single source of truth for all magic numbers,
    buffer sizes, limits, and compile-time constants used throughout the
    codebase. Changing values here propagates everywhere without needing
    to hunt through multiple files.
*/

#pragma once

#include <cstdint>

namespace glorp
{
namespace config
{

//==============================================================================
// Plugin & Audio Engine Limits
//==============================================================================

/// Maximum number of stereo output buses reserved for instruments.
/// This is baked into the LV2 port list at build time and cannot change at runtime.
/// VST3 could theoretically change it, but keeping formats identical is safer.
/// 32 covers the largest kits in common use (CrocellKit_full has 27 instruments).
/// Instruments past this many share the final bus rather than being dropped.
constexpr int MAX_INSTRUMENT_BUSES = 32;

/// Maximum number of output buses exposed to the host (stereo pairs).
/// This is the bus count the plugin advertises; actual instrument count may be lower.
constexpr int MAX_OUTPUT_BUSES = 16;

/// Maximum number of instruments the plugin can handle simultaneously.
/// Used for parameter array sizing and voice allocation.
constexpr int MAX_SUPPORTED_INSTRUMENTS = 64;

//==============================================================================
// Voice Management
//==============================================================================

/// Maximum number of concurrent voices (polyphony).
/// Each voice holds one sample playback instance. 64 allows dense drum parts
/// with long cymbal decays and fast rolls without stealing.
constexpr int MAX_VOICES = 64;

//==============================================================================
// Lock-Free Ring Buffer Sizes
//==============================================================================

/// Size of the hit event ring buffer (audio thread -> GUI).
/// Must be a power of 2 for atomic index wrapping.
/// 512 entries handles bursty MIDI input at high tempos without overflow.
constexpr int HIT_RING_SIZE = 512;

/// Size of the MIDI-learned control value ring buffer (audio thread -> message thread).
/// Must be a power of 2. 128 is sufficient for typical CC learning bursts.
constexpr int CONTROL_RING_SIZE = 128;

//==============================================================================
// Audio Processing
//==============================================================================

/// Default block size assumption for preallocations (samples).
/// Actual block size comes from prepareToPlay().
constexpr int DEFAULT_BLOCK_SIZE = 512;

/// Maximum block size we preallocate scratch buffers for.
/// Hosts may request larger blocks; we resize dynamically if needed.
constexpr int MAX_BLOCK_SIZE = 4096;

/// Default sample rate assumption for preallocations (Hz).
/// Actual rate comes from prepareToPlay().
constexpr double DEFAULT_SAMPLE_RATE = 44100.0;

//==============================================================================
// Kit & Sample Limits
//==============================================================================

/// Maximum number of round-robin layers per zone.
constexpr int MAX_ROUND_ROBIN_LAYERS = 8;

/// Maximum number of velocity layers per zone.
constexpr int MAX_VELOCITY_LAYERS = 16;

/// Maximum number of mic channels per instrument (for bleed matrix).
constexpr int MAX_MIC_CHANNELS_PER_INSTRUMENT = 8;

/// Maximum number of kit channels (mic positions) in a drumkit.
constexpr int MAX_KIT_CHANNELS = 32;

//==============================================================================
// E-Drum / Hi-Hat Constants
//==============================================================================

/// Hi-hat CC#4 zone boundaries (0.0 = open, 1.0 = closed).
/// Closed:      0.00 - 0.20
/// Semi-Open:   0.20 - 0.45
/// Open Mid:    0.45 - 0.75
/// Full Open:   0.75 - 1.00
constexpr float HH_CLOSED_MAX      = 0.20f;
constexpr float HH_SEMI_OPEN_MAX   = 0.45f;
constexpr float HH_OPEN_MID_MAX    = 0.75f;
constexpr float HH_FULL_OPEN_MIN   = 0.75f;

/// Foot chick detection: pedal speed threshold (CC/sec) and time window (ms).
constexpr float HH_CHICK_SPEED_THRESHOLD = 0.40f;
constexpr int   HH_CHICK_TIME_WINDOW_MS  = 45;

/// Cymbal choke fade time (ms) - C^1 half-cosine window for click-free choking.
constexpr float CYMBAL_CHOKE_FADE_MS = 15.0f;

//==============================================================================
// UI & Rendering
//==============================================================================

/// Meter refresh rate (Hz) for peak/RMS level displays.
constexpr int METER_REFRESH_HZ = 30;

/// Maximum zoom level for waveform deck (1.0 = 1x, 32.0 = 32x).
constexpr float MAX_WAVEFORM_ZOOM = 32.0f;

/// Minimum zoom level for waveform deck.
constexpr float MIN_WAVEFORM_ZOOM = 1.0f;

/// Default editor dimensions (pixels).
constexpr int DEFAULT_EDITOR_WIDTH  = 1180;
constexpr int DEFAULT_EDITOR_HEIGHT = 860;

/// Maximum recent kits to remember in FILE menu.
constexpr int MAX_RECENT_KITS = 12;

//==============================================================================
// Tempo Detection
//==============================================================================

/// BPM search range for spectral flux autocorrelation.
constexpr float MIN_BPM = 60.0f;
constexpr float MAX_BPM = 200.0f;

/// Confidence threshold for tempo detection (0.0 - 1.0).
constexpr float TEMPO_CONFIDENCE_THRESHOLD = 0.3f;

//==============================================================================
// File & Path Limits
//==============================================================================

/// Maximum path length for internal buffers.
constexpr int MAX_PATH_LENGTH = 4096;

/// Default kit search depth (directory levels).
constexpr int DEFAULT_KIT_SEARCH_DEPTH = 6;

} // namespace config
} // namespace glorp