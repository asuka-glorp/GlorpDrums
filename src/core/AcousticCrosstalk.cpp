/*
    AcousticCrosstalk.cpp - Implementation of AcousticCrosstalk engine.
*/

#include "AcousticCrosstalk.h"
#include <algorithm>
#include <cmath>

namespace glorp
{

AcousticCrosstalk::AcousticCrosstalk()
{
    const size_t numPieces = static_cast<size_t> (BleedPiece::count);
    for (size_t p = 0; p < numPieces; ++p)
    {
        pieceDelayL[p].assign (ringSize, 0.0f);
        pieceDelayR[p].assign (ringSize, 0.0f);
    }

    setupBandpassFilter (3200.0f, 3.2f);
}

void AcousticCrosstalk::prepare (double sampleRate, int /*maxBlockSize*/)
{
    currentSampleRate = (sampleRate > 1000.0) ? sampleRate : 44100.0;

    const size_t numPieces = static_cast<size_t> (BleedPiece::count);
    for (size_t p = 0; p < numPieces; ++p)
    {
        pieceDelayL[p].assign (ringSize, 0.0f);
        pieceDelayR[p].assign (ringSize, 0.0f);
        lpfStateL[p] = 0.0f;
        lpfStateR[p] = 0.0f;
    }

    writePos = 0;

    // Decay rate: ~180 ms T60 for sympathetic snare wire buzz
    const float decayTimeSeconds = 0.18f;
    snareWireDecay = std::exp (-4.6f / (decayTimeSeconds * static_cast<float> (currentSampleRate)));

    // 1-pole LPF cutoff ~2800 Hz for off-axis acoustic bleed
    const float lpfFreq = 2800.0f;
    const float omega = 2.0f * 3.14159265358979323846f * (lpfFreq / static_cast<float> (currentSampleRate));
    lpfCoeff = std::clamp (omega / (omega + 1.0f), 0.05f, 0.95f);

    setupBandpassFilter (3200.0f, 3.2f);
    reset();
}

void AcousticCrosstalk::reset()
{
    const size_t numPieces = static_cast<size_t> (BleedPiece::count);
    for (size_t p = 0; p < numPieces; ++p)
    {
        std::fill (pieceDelayL[p].begin(), pieceDelayL[p].end(), 0.0f);
        std::fill (pieceDelayR[p].begin(), pieceDelayR[p].end(), 0.0f);
        lpfStateL[p] = 0.0f;
        lpfStateR[p] = 0.0f;
    }

    writePos = 0;
    snareWireEnv = 0.0f;
    biquadX1 = 0.0f;
    biquadX2 = 0.0f;
    biquadY1 = 0.0f;
    biquadY2 = 0.0f;
}

void AcousticCrosstalk::setupBandpassFilter (float freq, float q)
{
    const float sr = static_cast<float> (currentSampleRate);
    const float w0 = 2.0f * 3.14159265358979323846f * std::clamp (freq, 100.0f, sr * 0.45f) / sr;
    const float alpha = std::sin (w0) / (2.0f * std::max (0.1f, q));

    const float a0 = 1.0f + alpha;
    b0 = alpha / a0;
    b1 = 0.0f;
    b2 = -alpha / a0;
    a1 = (-2.0f * std::cos (w0)) / a0;
    a2 = (1.0f - alpha) / a0;
}

void AcousticCrosstalk::triggerSympatheticHit (BleedPiece piece, float velocity) noexcept
{
    const float vel = std::clamp (velocity, 0.0f, 1.5f);

    switch (piece)
    {
        case BleedPiece::kick:
            // Kick strikes produce strong sympathetic buzz on the snare bottom head
            snareWireEnv = std::max (snareWireEnv, vel * 0.70f);
            break;

        case BleedPiece::floorTom:
            // Floor tom low frequencies excite the snare wires
            snareWireEnv = std::max (snareWireEnv, vel * 0.55f);
            break;

        case BleedPiece::tom2:
            // Mid/rack tom 2 produces moderate buzz
            snareWireEnv = std::max (snareWireEnv, vel * 0.35f);
            break;

        case BleedPiece::tom1:
            // High tom produces slight buzz
            snareWireEnv = std::max (snareWireEnv, vel * 0.20f);
            break;

        case BleedPiece::snare:
        case BleedPiece::hihat:
        case BleedPiece::ride:
        case BleedPiece::crash1:
        case BleedPiece::crash2:
        case BleedPiece::count:
        default:
            break;
    }
}

void AcousticCrosstalk::feedDirectSignal (BleedPiece piece, const float* audioL, const float* audioR, int numSamples)
{
    if (audioL == nullptr || numSamples <= 0)
        return;

    const size_t p = static_cast<size_t> (piece);
    if (p >= static_cast<size_t> (BleedPiece::count))
        return;

    auto& bufL = pieceDelayL[p];
    auto& bufR = pieceDelayR[p];
    const bool hasR = (audioR != nullptr);

    for (int i = 0; i < numSamples; ++i)
    {
        const size_t idx = (writePos + static_cast<size_t> (i)) & ringMask;
        bufL[idx] += audioL[i];
        bufR[idx] += hasR ? audioR[i] : audioL[i];
    }
}

void AcousticCrosstalk::render (float* const* micBuffersL,
                                float* const* micBuffersR,
                                int numMics,
                                int numSamples,
                                const BleedMatrix& bleedMatrix,
                                int snareMicIndex,
                                int snareBotMicIndex)
{
    if (numSamples <= 0 || numMics <= 0 || micBuffersL == nullptr)
        return;

    const auto mode = bleedMatrix.getMode();
    if (mode == BleedMatrix::Mode::disabled)
    {
        // Advance write position and decay envelope even when disabled
        writePos = (writePos + static_cast<size_t> (numSamples)) & ringMask;
        snareWireEnv *= std::pow (snareWireDecay, static_cast<float> (numSamples));
        return;
    }

    const float masterBleed = bleedMatrix.getMasterBleed();
    const float sympLevel = bleedMatrix.getSympatheticResonance() * masterBleed;

    // 1. Render Sympathetic Snare Wire Rattle
    if (sympLevel > 0.001f && snareWireEnv > 1.0e-5f)
    {
        // Choose destination mic: snare bottom mic preferred, then snare top, then first mic
        int destMic = snareBotMicIndex >= 0 ? snareBotMicIndex : snareMicIndex;
        if (destMic < 0 || destMic >= numMics)
            destMic = 0;

        float* outL = micBuffersL[destMic];
        float* outR = (micBuffersR != nullptr && micBuffersR[destMic] != nullptr)
                          ? micBuffersR[destMic]
                          : outL;

        for (int i = 0; i < numSamples; ++i)
        {
            if (snareWireEnv < 1.0e-5f)
                break;

            // Pseudo-random white noise modulated by envelope
            const float rawNoise = nextNoise();
            const float excited = rawNoise * snareWireEnv;

            // Pass through bandpass biquad (wires resonance around 3.2 kHz)
            const float bp = b0 * excited + b1 * biquadX1 + b2 * biquadX2 - a1 * biquadY1 - a2 * biquadY2;
            biquadX2 = biquadX1;
            biquadX1 = excited;
            biquadY2 = biquadY1;
            biquadY1 = bp;

            // Non-linear wire rattle clipping (buzzing against snare head)
            const float buzz = std::tanh (bp * 2.2f) * sympLevel * 0.18f;

            if (outL != nullptr) outL[i] += buzz;
            if (outR != nullptr && outR != outL) outR[i] += buzz;

            snareWireEnv *= snareWireDecay;
        }
    }

    // 2. Clear current block's delay buffer write slot for the next cycle
    for (size_t p = 0; p < static_cast<size_t> (BleedPiece::count); ++p)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const size_t idx = (writePos + static_cast<size_t> (i)) & ringMask;
            pieceDelayL[p][idx] = 0.0f;
            pieceDelayR[p][idx] = 0.0f;
        }
    }

    writePos = (writePos + static_cast<size_t> (numSamples)) & ringMask;
}

} // namespace glorp
