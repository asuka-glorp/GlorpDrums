/*
    AcousticCrosstalk.h - Inter-mic physical acoustic delay, off-axis filtering,
                          and sympathetic snare wire rattle simulation.
*/

#pragma once

#include "BleedMatrix.h"
#include <array>
#include <cmath>
#include <vector>

namespace glorp
{

class AcousticCrosstalk
{
public:
    AcousticCrosstalk();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /// Trigger sympathetic snare wire buzz when Kick or Low Toms are struck.
    void triggerSympatheticHit (BleedPiece piece, float velocity) noexcept;

    /// Renders acoustic crosstalk and sympathetic resonance into per-mic scratch buffers.
    /// @param micBuffersL Pointer to array of left scratch buffers (one per mic)
    /// @param micBuffersR Pointer to array of right scratch buffers (one per mic)
    /// @param numMics Number of mic channels
    /// @param numSamples Block size
    /// @param bleedMatrix Reference to matrix settings (master bleed, sympathetic level, etc.)
    /// @param snareMicIndex Index of the primary snare mic (or -1 if not identified)
    /// @param snareBotMicIndex Index of the snare bottom mic (or -1 if not identified)
    void render (float* const* micBuffersL,
                 float* const* micBuffersR,
                 int numMics,
                 int numSamples,
                 const BleedMatrix& bleedMatrix,
                 int snareMicIndex,
                 int snareBotMicIndex);

    /// Feed direct voice hit audio to inter-mic delay ring buffer for synthetic crosstalk
    void feedDirectSignal (BleedPiece piece, const float* audioL, const float* audioR, int numSamples);

private:
    double currentSampleRate = 44100.0;

    // Ring buffer size for acoustic delays (2048 covers >42 ms at 48kHz)
    static constexpr size_t ringSize = 2048;
    static constexpr size_t ringMask = ringSize - 1;

    // Ring buffer per drum piece (left/right)
    std::array<std::vector<float>, static_cast<size_t> (BleedPiece::count)> pieceDelayL;
    std::array<std::vector<float>, static_cast<size_t> (BleedPiece::count)> pieceDelayR;
    size_t writePos = 0;

    // 1-pole low-pass filter states per piece
    std::array<float, static_cast<size_t> (BleedPiece::count)> lpfStateL {};
    std::array<float, static_cast<size_t> (BleedPiece::count)> lpfStateR {};
    float lpfCoeff = 0.35f;

    // Sympathetic Snare Wire Model
    float snareWireEnv = 0.0f;
    float snareWireDecay = 0.992f;
    float biquadX1 = 0.0f, biquadX2 = 0.0f;
    float biquadY1 = 0.0f, biquadY2 = 0.0f;
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;

    // Pseudo-random noise state for wire buzz
    uint32_t noiseState = 0x12345678;
    float nextNoise() noexcept
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return (static_cast<float> (noiseState & 0x00FFFFFF) / 8388607.0f) - 1.0f;
    }

    void setupBandpassFilter (float freq, float q);
};

} // namespace glorp
