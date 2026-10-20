/*
    Sampler.h - voice allocation and playback.

    Design notes
    ------------
    * Each instrument owns one stereo output bus, so a kit with twelve playable
      instruments presents twelve stereo outputs to the host and every piece of
      the kit can be treated as its own mixer channel.

    * An instrument's kit channels (overhead pair, room pair, trigger pads) are
      summed into that bus, with gains derived from each channel's position in the
      instrument. That is what DrumGizmo's channel layout means, translated into
      a stereo bus.

    * Tuning uses high-quality varispeed cubic Hermite resampling at sample read
      time: 0 latency, natural tape-style drum pitch scaling (pitching up shortens
      and brightens, pitching down lengthens and warms), and zero CPU overhead on bypass.

    * Nothing on the render path allocates or locks a mutex. The kit lock is only
      taken to stop a kit being swapped out from under the audio thread.
*/

#pragma once

#include "ChannelFX.h"
#include "DrumKit.h"
#include "SampleCache.h"
#include "BleedMatrix.h"
#include "AcousticCrosstalk.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace glorp
{

/// Hi-Hat response mode: continuous CC#4 pedal crossfade vs discrete mapped notes.
enum class HiHatMode : int
{
    continuousCC4 = 0,
    discreteNotes = 1
};

/// Velocity curve profile for e-drum pad calibration.
enum class VelocityCurve : int
{
    linear = 0,
    logarithmic = 1,
    exponential = 2,
    sCurve = 3
};

/// Per-instrument settings the UI exposes and the audio thread reads.
struct InstrumentParams
{
    float level = 1.0f;             ///< linear gain
    float pan = 0.0f;               ///< -1 hard left .. +1 hard right
    float pitchSemitones = 0.0f;
    float tuneSpread = 0.0f;        ///< cents of random detune per hit
    float dynamics = 0.5f;          ///< 0..1 per-zone velocity response & humanize
    VelocityCurve velocityCurve = VelocityCurve::linear; ///< E-drum velocity curve
    bool  autoRoundRobin = false;   ///< "AUTO": cycle the instrument's samples
    bool  mute = false;
    bool  solo = false;
    int   selectedSample = 0;       ///< used when autoRoundRobin is off
};

/// Kit-wide controls.
struct GlobalParams
{
    float masterLevel = 0.8f;
    float masterPan = 0.0f;
    float attackMs = 0.0f;          ///< fade-in applied to every hit
    float decayScale = 1.0f;        ///< 1 = natural length, <1 fades the tail out sooner
    float humanize = 0.0f;          ///< 0..1 timing and level scatter
    float velCurve = 1.0f;          ///< velocity response
};

/// A hit reported to the GUI so the kit can animate. Written from the audio
/// thread into a lock-free ring and drained on the message thread.
struct HitEvent
{
    int instrumentIndex = 0;
    int note = -1;                 ///< -1 when the hit did not come from a note
    float velocity = 0.0f;
    float pitchSemitones = 0.0f;
};

class Sampler
{
public:
    Sampler();
    ~Sampler();

    Sampler (const Sampler&) = delete;
    Sampler& operator= (const Sampler&) = delete;

    // -- setup -------------------------------------------------------------

    /// Tells the sampler the host's block geometry. Call before loading a kit.
    void setSampleRateAndBlock (double sampleRate, int blockSize);

    /** Loads a kit and resolves every sample handle. Blocking; never on the audio
        thread. Returns false and fills `error` if the kit is unusable.
    */
    bool loadKit (DrumKit kit, juce::String& error);

    void unloadKit();

    bool isKitLoaded() const noexcept { return kitLoaded.load(); }

    int numInstruments() const noexcept { return instrumentCount; }
    int usedBusCount() const noexcept { return busCount; }
    juce::String instrumentName (int index) const;
    juce::String kitTitleString() const { return kitTitle; }

    int numSamplesForInstrument (int index) const;
    juce::String sampleName (int instrument, int sample) const;

    // -- parameters --------------------------------------------------------

    void setInstrumentParams (int index, const InstrumentParams& params);
    InstrumentParams getInstrumentParams (int index) const;
    void setGlobalParams (const GlobalParams& params);
    GlobalParams getGlobalParams() const;

    // -- note & e-drum handling --------------------------------------------

    /** Triggers an instrument. `timeInSamples` is an absolute sample clock, the
        same one used by render(), so scheduling is sample accurate.
    */
    void noteOn (int instrument, float velocity, int64_t timeInSamples);

    /// As above, but remembers which note caused the hit so the GUI can show it.
    void noteOn (int instrument, int note, float velocity, int64_t timeInSamples);

    /// E-drum hi-hat mode
    void setHiHatMode (HiHatMode mode) noexcept { hiHatMode.store (mode, std::memory_order_relaxed); }
    HiHatMode getHiHatMode() const noexcept { return hiHatMode.load (std::memory_order_relaxed); }

    /// Continuous hi-hat pedal control (0.0 = fully open, 1.0 = fully closed / tight).
    void setHiHatPedal (float pedalValue) noexcept;
    float getHiHatPedal() const noexcept { return hiHatPedal.load (std::memory_order_relaxed); }
    void resetHiHatCC4() noexcept { cc4Active.store (false, std::memory_order_relaxed); }

    /// Snare positional sensing (0.0 = edge, 1.0 = center).
    void setSnarePosition (float pos) noexcept { snarePosition.store (pos, std::memory_order_relaxed); }
    float getSnarePosition() const noexcept { return snarePosition.load (std::memory_order_relaxed); }

    /// Fades out every voice of an instrument that matches a note. Kept simple:
    /// drum voices are one-shot, so a note off only matters for choking.
    void chokeInstrument (int instrument, int64_t fadeMs, int64_t timeInSamples);

    /// Chokes a cymbal instrument and all associated articulations (edge/bow/bell).
    void chokeCymbal (int instrumentIndex, int64_t fadeMs, int64_t timeInSamples);

    /// Chokes all currently active cymbal voices (for channel pressure / master choke).
    void chokeAllCymbals (int64_t fadeMs, int64_t timeInSamples);

    void allNotesOff();

    // -- Per-Mic Mixer (Phase 2) -------------------------------------------

    int numMicStrips() const noexcept;
    juce::String micStripName (int index) const;
    juce::String micStripTitle (int index) const;

    void setMicStripParams (int index, const MicStripParams& params);
    MicStripParams getMicStripParams (int index) const;

    void setMicOutputBus (int index, int bus);
    int  getMicOutputBus (int index) const;

    void setMasterStripParams (const MasterStripParams& params);
    MasterStripParams getMasterStripParams() const;

    void applyMixerPreset (const MixerPreset& preset);
    MixerPreset getCurrentMixerPreset (const juce::String& name) const;

    float getMicPeakL (int index) const noexcept;
    float getMicPeakR (int index) const noexcept;
    float getMicRmsL  (int index) const noexcept;
    float getMicRmsR  (int index) const noexcept;

    float getMasterPeakL() const noexcept { return masterPeakL.load (std::memory_order_relaxed); }
    float getMasterPeakR() const noexcept { return masterPeakR.load (std::memory_order_relaxed); }
    float getMasterRmsL()  const noexcept { return masterRmsL.load  (std::memory_order_relaxed); }
    float getMasterRmsR()  const noexcept { return masterRmsR.load  (std::memory_order_relaxed); }

    // -- Bleed Matrix & Crosstalk (Phase 4) --------------------------------
    BleedMatrix& getBleedMatrix() noexcept { return bleedMatrix; }
    const BleedMatrix& getBleedMatrix() const noexcept { return bleedMatrix; }
    AcousticCrosstalk& getAcousticCrosstalk() noexcept { return crosstalk; }

    // -- rendering ---------------------------------------------------------

    /** Renders one block. `output` is `numOutputChannels` pointers to
        `numSamples` buffers; instrument i writes to channels 2i and 2i+1.
        Audio thread only.
    */
    void render (float* const* output, int numOutputChannels, int numSamples);

    /** Latency the tuner introduces (0 for varispeed). */
    int latencySamples() const noexcept { return 0; }

    int activeVoiceCount() const noexcept;

    /// Sample position at the end of the last rendered block, for the scope.
    int64_t currentSamplePosition() const noexcept { return sampleClock; }

    /// Multi-out routing mode. When false (default), all instruments mix to stereo Bus 0.
    void setMultiOutMode (bool enabled) noexcept { multiOutMode.store (enabled); }
    bool isMultiOutMode() const noexcept { return multiOutMode.load(); }

    void setInstrumentBus (int instrument, int busIndex);
    int  getInstrumentBus (int instrument) const noexcept;

    // -- GUI event feed ----------------------------------------------------

    /// Pops the oldest hit, or returns false if there are none.
    bool popHit (HitEvent& event) noexcept;

    /// True if anything was queued for the GUI since the last call.
    bool hasPendingHits() const noexcept;

private:
    /// One channel of one sample round, already resolved and ready to read.
    struct ChannelRef
    {
        SampleCache::Handle handle;
        int channel = 0;
        float gainL = 0.0f;
        float gainR = 0.0f;
        int kitChannelIndex = 0;    ///< index into kitChannels_ for per-mic mixing
        bool isBleed = false;       ///< true if this is an off-axis bleed mic channel
        int pieceIdx = 0;           ///< source BleedPiece integer index
        float channelBleedFactor = 1.0f; ///< XML bleed attribute (1.0 = default)
    };

    /// A resolved round of an instrument: all of its channels, ready to sum.
    struct ResolvedHit
    {
        std::vector<ChannelRef> channels;
        juce::String name;
        float power = 1.0f;
        int64_t numFrames = 0;      ///< longest channel, i.e. when the round ends
    };

    struct ChokeTarget
    {
        int instrumentIndex = -1;
        float chokeTimeMs = 40.0f;
    };

    struct InstrumentRuntime
    {
        std::vector<ResolvedHit> hits;
        juce::String group;
        juce::String name;
        BleedPiece piece = BleedPiece::snare;
        std::vector<ChokeTarget> chokeTargets;
    };

    /// Audio-thread-visible copy of InstrumentParams.
    struct ChannelParams
    {
        float level = 1.0f;
        float pan = 0.0f;
        float pitch = 0.0f;
        float spread = 0.0f;
        float dynamics = 0.5f;
        VelocityCurve velocityCurve = VelocityCurve::linear;
        bool roundRobin = false;
        bool mute = false;        ///< as the user set it
        bool solo = false;
        bool muted = false;       ///< after solo resolution, read by the audio thread
        int selectedSample = 0;
    };

    struct GlobalSnapshot
    {
        float masterLevel = 0.8f;
        float masterPan = 0.0f;
        float attackMs = 0.0f;
        float decayScale = 1.0f;
        float humanize = 0.0f;
        float velCurve = 1.0f;
    };

    /// A voice is held by pointer so the pool can be resized without moving
    /// voices. Allocated once at construction: the audio thread must never
    /// allocate.
    struct Voice
    {
        bool active = false;
        int instrument = 0;
        int hit = 0;
        int64_t startedAt = 0;
        int64_t endsAt = 0;          ///< absolute sample clock, 0 when not choking
        int64_t endsAtAbsolute = 0;  ///< when the sample data + flush runs out
        int64_t chokeFadeSamples = 0;///< total fade length in samples for anti-click choke
        int64_t chokeStartSample = 0;///< sample clock when choke started

        double position = 0.0;       ///< fractional read position within the sample, in frames
        double pitchRatio = 1.0;     ///< varispeed ratio: 2^(semitones/12) * rateRatio
        int64_t startSkip = 0;       ///< output samples to discard so the note is sample accurate

        float velocity = 1.0f;
        float pitchSemitones = 0.0f;
        float attackGain = 0.0f;     ///< current value of the fade-in
        int   attackSamples = 0;

        // Reused scratch, sized to the largest block seen.
        std::vector<float> mixL, mixR, channelScratch;
    };

    /// Owns the voice pool. Kept out of Voice itself so the pool can be resized
    /// without ever moving a voice.
    struct VoicePool
    {
        std::vector<std::unique_ptr<Voice>> voices;
    };

    Voice* allocateVoice();
    int   chooseHit (int instrumentIndex, float velocity);
    void  triggerVoice (int instrument, int note, float velocity, int64_t timeInSamples);

    /// Which stereo output bus an instrument writes to.
    int   busForInstrument (int instrument) const noexcept
    {
        if (instrument >= 0 && instrument < static_cast<int> (instrumentBuses.size()))
        {
            const int b = instrumentBuses[static_cast<size_t> (instrument)];
            if (b >= 0)
                return busCount > 0 ? std::min (b, busCount - 1) : 0;
        }
        return busCount > 0 ? std::min (instrument, busCount - 1) : 0;
    }

    void  resolveInstrumentChannels (const DrumKit& kit, const Instrument& instrument, InstrumentRuntime& out);
    void  recomputeSoloFlags();
    float nextRandom();
    void  pushHitEvent (const HitEvent& event) noexcept;

    static constexpr int maxVoices = 64;

    // -- audio thread state ------------------------------------------------
    VoicePool pool;
    std::vector<ChannelParams> channelParams;
    GlobalSnapshot globals;
    std::vector<int> roundRobinCursor;
    std::vector<int> instrumentBuses;
    int64_t sampleClock = 0;
    int currentBlockSize = 512;

    std::atomic<bool> kitLoaded { false };
    std::atomic<bool> anySolo { false };
    std::atomic<bool> multiOutMode { true };
    std::atomic<int> nextVoiceIndex { 0 };
    std::atomic<uint32_t> randomState { 0x12345678u };

    // -- E-drum state -------------------------------------------------------
    std::atomic<HiHatMode> hiHatMode { HiHatMode::continuousCC4 };
    int64_t lastPedalDownClock = 0;
    float lastPedalDownSpeed = 0.0f;

    struct HiHatIndices
    {
        int closed = -1;
        int closedEdge = -1;
        int semiOpen = -1;
        int open = -1;
        int openEdge = -1;
        int open1 = -1;
        int open2 = -1;
        int open3 = -1;
        int open4 = -1;
        int pedal = -1;
        int splash = -1;
    } hiHat;

    std::atomic<float> hiHatPedal { 0.0f };    ///< 0 = open, 1 = closed
    std::atomic<bool>  cc4Active { false };    ///< true once an e-drum pedal sends CC#4
    std::atomic<float> snarePosition { 0.5f }; ///< 0 = edge, 1 = center

    static constexpr int hitRingSize = 512;
    HitEvent hitRing[hitRingSize];
    std::atomic<int> hitWrite { 0 };
    std::atomic<int> hitRead { 0 };

    // -- Per-mic mixer state & DSP ------------------------------------------
    struct MicChannelState
    {
        juce::String name;
        juce::String title;
        MicStripParams params;
        ChannelStripFX fx;
        std::atomic<float> peakL { 0.0f };
        std::atomic<float> peakR { 0.0f };
        std::atomic<float> rmsL  { 0.0f };
        std::atomic<float> rmsR  { 0.0f };
    };

    std::vector<std::unique_ptr<MicChannelState>> micChannels;
    MasterStripParams masterStripParams;
    MasterFX masterFX;
    std::atomic<float> masterPeakL { 0.0f };
    std::atomic<float> masterPeakR { 0.0f };
    std::atomic<float> masterRmsL  { 0.0f };
    std::atomic<float> masterRmsR  { 0.0f };

    // Preallocated audio-thread scratch buffers for mixing
    std::vector<std::vector<float>> micScratchL, micScratchR;
    std::vector<float> reverbAccumL, reverbAccumR;

    // -- Bleed Matrix & Acoustic Crosstalk (Phase 4) -----------------------
    BleedMatrix bleedMatrix;
    AcousticCrosstalk crosstalk;
    int snareMicIndex = -1;
    int snareBotMicIndex = -1;
    std::array<float*, 32> micScratchLPtrs {};
    std::array<float*, 32> micScratchRPtrs {};

    // -- kit state, guarded by kitLock --------------------------------------
    mutable juce::CriticalSection kitLock;
    std::vector<InstrumentRuntime> instruments_;
    std::vector<KitChannel> kitChannels_;
    int instrumentCount = 0;
    int busCount = 0;
    double currentSampleRate = 44100.0;
    double kitSampleRate = 48000.0;   ///< declared sample rate of the loaded kit
    juce::String kitTitle;

    SampleCache sampleCache;

public:
    /// Kit-level read-only information for the UI. Safe to call from the message
    /// thread; the kit only changes when the user picks another one.
    int numKitChannels() const;
    juce::String kitChannelName (int index) const;
    juce::String kitChannelTitle (int index) const;

    /// Resets every instrument to its default tuning and level.
    void resetInstrumentParams();
};

} // namespace glorp
