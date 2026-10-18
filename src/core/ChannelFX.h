/*
    ChannelFX.h - Built-in DSP effects for per-mic drum mixer channel strips.

    Includes:
      - Polarity / Phase Invert
      - 3-band Parametric EQ (Low Shelf 80Hz, Peaking Mid 200-6000Hz, High Shelf 10kHz)
      - VCA Compressor with auto-makeup / makeup gain
      - Tape / Tube Saturation (harmonic warm waveshaping)
      - Transient Shaper (attack & sustain envelope modulation)
      - Algorithmic Drum Reverb send & return
      - Master Peak Limiter & dynamic LED meters
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace glorp
{

/// Parameters for one microphone channel strip.
struct MicStripParams
{
    float level = 1.0f;            ///< linear gain (0.0 = -inf, 1.0 = 0dB, 2.0 = +6dB)
    float pan = 0.0f;              ///< -1.0 (L) .. +1.0 (R)
    bool  mute = false;
    bool  solo = false;
    bool  phaseInvert = false;     ///< flip polarity (-1x)
    int   outputBus = 0;           ///< 0 = Master 1/2, 1 = Out 3/4, 2 = Out 5/6 ...
    bool  sendToMaster = false;    ///< when routed to aux bus, also send to master

    // FX rack settings
    bool  fxEnabled = true;

    // 3-Band Parametric EQ
    bool  eqEnabled = true;
    float eqLowGainDb = 0.0f;      ///< -12 .. +12 dB (Low Shelf at 80 Hz)
    float eqMidGainDb = 0.0f;      ///< -12 .. +12 dB (Peaking Mid)
    float eqMidFreq = 1000.0f;     ///< 200 .. 6000 Hz
    float eqMidQ = 1.0f;           ///< 0.5 .. 3.0
    float eqHighGainDb = 0.0f;     ///< -12 .. +12 dB (High Shelf at 10 kHz)

    // VCA Compressor
    bool  compEnabled = false;
    float compThresholdDb = -14.0f;///< -30 .. 0 dB
    float compRatio = 3.0f;        ///< 1.0 .. 8.0
    float compAttackMs = 15.0f;    ///< 1.0 .. 50.0 ms
    float compReleaseMs = 100.0f;  ///< 20.0 .. 300.0 ms
    float compMakeupDb = 0.0f;     ///< 0.0 .. 12.0 dB

    // Tape / Tube Saturation
    float saturationDrive = 0.0f;  ///< 0.0 .. 1.0 (warm harmonic drive)

    // Transient Shaper
    float transientAttack = 0.0f;  ///< -1.0 .. +1.0 (-100% to +100% attack punch)
    float transientSustain = 0.0f; ///< -1.0 .. +1.0 (-100% to +100% sustain body)

    // Reverb send
    float reverbSend = 0.0f;       ///< 0.0 .. 1.0
};

/// Parameters for the Master mixer channel.
struct MasterStripParams
{
    float level = 1.0f;            ///< linear master gain (0 dB unity default)
    float pan = 0.0f;              ///< -1.0 .. +1.0
    bool  limiterEnabled = true;   ///< fast transparent soft limiter
    float reverbReturn = 0.25f;    ///< wet reverb return level (0.0 .. 1.0)
    float reverbSize = 0.45f;      ///< room size (0.0 .. 1.0)
    float reverbDamp = 0.40f;      ///< high frequency damping (0.0 .. 1.0)
    float reverbWidth = 0.85f;     ///< stereo width (0.0 .. 1.0)
};

//==============================================================================
/** Built-in DSP processor for one channel strip.
    Zero audio-thread allocations.
*/
class ChannelStripFX
{
public:
    ChannelStripFX();
    ~ChannelStripFX();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Processes stereo audio in-place through EQ, Compression, Saturation,
        and Transient Shaper. Taps Reverb Send into the accumulator buffer.
    */
    void process (float* left, float* right, int numSamples,
                  const MicStripParams& params,
                  float* reverbAccumL, float* reverbAccumR);

private:
    void updateFilters (const MicStripParams& params);

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // 3-Band Parametric EQ Filters (stereo pairs)
    using IIRFilter = juce::dsp::IIR::Filter<float>;
    IIRFilter lowShelfL, lowShelfR;
    IIRFilter midPeakL,  midPeakR;
    IIRFilter highShelfL, highShelfR;

    float lastLowGain = 0.0f;
    float lastMidGain = 0.0f;
    float lastMidFreq = 1000.0f;
    float lastMidQ = 1.0f;
    float lastHighGain = 0.0f;

    // VCA Compressor
    juce::dsp::Compressor<float> compressor;
    float lastCompThresh = -14.0f;
    float lastCompRatio = 3.0f;
    float lastCompAttack = 15.0f;
    float lastCompRelease = 100.0f;

    // Transient Shaper Envelope Followers
    float fastEnvL = 0.0f, fastEnvR = 0.0f;
    float slowEnvL = 0.0f, slowEnvR = 0.0f;
    float fastAttackCoeff = 0.0f, fastReleaseCoeff = 0.0f;
    float slowAttackCoeff = 0.0f, slowReleaseCoeff = 0.0f;
};

//==============================================================================
/** Master DSP processor: Algorithmic drum reverb return and soft peak limiter.
*/
class MasterFX
{
public:
    MasterFX();
    ~MasterFX();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** Processes accumulated reverb, adds it to the master mix, and applies
        master level, pan, and soft limiter.
    */
    void process (float* masterL, float* masterR, int numSamples,
                  const float* reverbSendL, const float* reverbSendR,
                  const MasterStripParams& params);

private:
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    juce::dsp::Reverb reverb;
    juce::dsp::Reverb::Parameters lastReverbParams;

    // Scratch buffers for reverb wet output
    std::vector<float> reverbWetL, reverbWetR;
};

//==============================================================================
/** Named Mixer Preset (.glorpmix).
*/
struct MixerPreset
{
    juce::String name;
    MasterStripParams master;
    std::vector<MicStripParams> micStrips;
    std::vector<juce::String> micNames;

    static MixerPreset createDefaultClean (int numMics, const std::vector<juce::String>& names);
    static MixerPreset createMassiveRock  (int numMics, const std::vector<juce::String>& names);
    static MixerPreset createVintageWarmth(int numMics, const std::vector<juce::String>& names);
    static MixerPreset createPunchyStudio (int numMics, const std::vector<juce::String>& names);
    static MixerPreset createDryTight     (int numMics, const std::vector<juce::String>& names);

    juce::String toXmlString() const;
    static bool fromXmlString (const juce::String& xmlText, MixerPreset& outPreset);
};

} // namespace glorp
