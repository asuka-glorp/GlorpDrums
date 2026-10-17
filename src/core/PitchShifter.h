/*
    PitchShifter.h - DEPRECATED: transient-preserving pitch shifting for drum samples.

    This class is no longer used in the render path. Varispeed resampling in
    Sampler::render() replaces it: zero latency, no FFT allocation, and the
    tape-style pitch behavior drummers expect (pitch up = shorter + brighter).

    Kept for reference and potential future use (e.g. formant-preserving mode).
    Do not use for new code.
*/

#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace glorp
{

class Fft;

class PitchShifter
{
public:
    PitchShifter();
    ~PitchShifter();

    PitchShifter (const PitchShifter&) = delete;
    PitchShifter& operator= (const PitchShifter&) = delete;

    /** Configures for a sample rate. Must be called before processing.
        `blockSize` is a hint about the host's maximum block size.
    */
    void prepare (double sampleRate, int blockSize = 512);

    /** Clears all internal state but keeps configuration. Call on every new note. */
    void reset();
    void startNote();

    /** Semitones of pitch shift, clamped to +/- maxSemitones. Zero bypasses
        completely: no processing and no latency.
    */
    void setSemitones (float semitones);
    float getSemitones() const noexcept { return semitones; }

    /** Largest shift that still sounds like a drum. Beyond this WSOLA degrades
        badly enough that the dry signal is the better answer.
    */
    static constexpr float maxSemitones = 12.0f;

    /** Length of the untouched attack at the start of a hit, in milliseconds. */
    void setDryAttackMs (float ms) noexcept { dryAttackMs = ms; }

    /** Radius of the waveform alignment search, in samples. Wider finds better
        matches but costs proportionally more.
    */
    void setSearchRadius (int samples) noexcept;

    /** Pushes `numInSamples` in and writes exactly `numOutSamples` out. Positions
        where the algorithm has nothing to say yet (its own latency, or the sample
        running out) come out as silence.
    */
    void processMono (const float* in, int numInSamples, float* out, int numOutSamples);

    /** Delay in samples this shifter introduces, for host compensation. */
    int latencySamples() const noexcept;

    /** True while the shifter is engaged, i.e. would contribute latency. */
    bool isActive() const noexcept { return std::abs (semitones) >= 1.0e-4f; }

    /// Tunables exposed for tests and for the tuning-quality regression checks.
    int getFrameSize() const noexcept { return frameSize; }
    int getHopSize() const noexcept { return hopSize; }
    int getDryAttackSamples() const noexcept { return dryAttackSamples; }
    float getRatio() const noexcept { return pitchRatio; }

private:
    bool runFrame();
    float readInput (int64_t position) const noexcept;
    float applyTilt (float x) noexcept;
    int  searchBestOffset (int64_t nominalStart, int search);

    // -- configuration ----------------------------------------------------
    double sampleRate = 44100.0;
    float  semitones = 0.0f;
    float  dryAttackMs = 8.0f;
    int    searchRadius = 256;
    int    maxBlockSize = 512;

    int frameSize = 2048;
    int hopSize = 512;
    int overlapSize = 1536;
    int latency = 2048;
    int dryAttackSamples = 0;

    float pitchRatio = 1.0f;
    float analysisHop = 512.0f;
    double resampleStep = 1.0;
    float tiltCoefficient = 0.0f;
    float tiltGain = 0.0f;

    // -- input ------------------------------------------------------------
    std::vector<float> inputRing;
    std::vector<float> dryRing;
    int64_t inputWrite = 0;
    int     dryWrite = 0;

    // -- WSOLA state ------------------------------------------------------
    double  readCursor = 0.0;
    int64_t previousFrameStart = 0;
    bool    havePrevious = false;

    // Overlap-added output. Overlapping regions hold the *average* of the two
    // contributing frames rather than their sum: WSOLA aligns the waveform, so
    // for similar frames the average reconstructs the original amplitude, and
    // unlike a window-energy normalisation it never divides by a near-zero
    // denominator in the one-frame-covered regions at the very start.
    std::vector<float> fifoValue;
    size_t fifoRead = 0;
    int64_t outputProduced = 0;

    // -- resampler and tilt ----------------------------------------------
    double resamplePosition = 0.0;
    float  tiltState = 0.0f;

    // -- scratch ----------------------------------------------------------
    std::vector<float> prefixEnergy;
    std::vector<float> scratchUre, scratchUim;
    std::vector<float> scratchVre, scratchVim;
    std::vector<float> scratchWre, scratchWim;
    Fft* fft = nullptr;
};

} // namespace glorp
