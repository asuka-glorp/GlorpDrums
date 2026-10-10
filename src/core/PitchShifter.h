/*
    PitchShifter.h - transient-preserving pitch shifting for drum samples.

    Why not a phase vocoder?
    ------------------------
    A phase vocoder smears transients, and a drum hit *is* a transient. Tuning a
    kick or snare with one sounds like it was played through a broken sampler: the
    attack arrives late, blurred, with pre-ringing. Rubber Band is a fine library,
    but its realtime mode also carries roughly 5000 samples of latency it will not
    tell you about, which a sample instrument cannot hide.

    What this does instead
    ----------------------
    WSOLA (waveform-similarity overlap-add), the same family of technique SoundTouch
    uses. The signal is cut into overlapping frames which are cross-correlated
    against the ideal position and slid to wherever the waveform lines up best.
    That preserves the waveform through the attack, so the transient survives.

    The chain is:

      1. The first few milliseconds are played completely dry. The attack is the
         most important part of a drum hit and the part overlap-add damages most,
         so it bypasses the algorithm entirely.
      2. WSOLA time-stretches by the pitch ratio.
      3. Cubic resampling at the same ratio restores the original duration,
         producing a net pitch shift with unchanged timing.
      4. A gentle high shelf, proportional to an upward shift, restores the snap
         that moving every partial upward otherwise costs.

    Latency is one WSOLA window, and latencySamples() reports it so the host can
    compensate.
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
