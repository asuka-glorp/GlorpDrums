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

    * Tuning runs *after* the channels are summed: one WSOLA shifter per stereo
      side per voice, rather than one per sample channel. Two shifters per voice
      instead of two per instrument channel is the difference between a 16-voice
      kit being affordable and not.

    * Nothing on the render path allocates or locks a mutex. The kit lock is only
      taken to stop a kit being swapped out from under the audio thread.
*/

#pragma once

#include "DrumKit.h"
#include "PitchShifter.h"
#include "SampleCache.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

namespace glorp
{

/// Per-instrument settings the UI exposes and the audio thread reads.
struct InstrumentParams
{
    float level = 1.0f;             ///< linear gain
    float pan = 0.0f;               ///< -1 hard left .. +1 hard right
    float pitchSemitones = 0.0f;
    float tuneSpread = 0.0f;        ///< cents of random detune per hit
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

    // -- note handling -----------------------------------------------------

    /** Triggers an instrument. `timeInSamples` is an absolute sample clock, the
        same one used by render(), so scheduling is sample accurate.
    */
    void noteOn (int instrument, float velocity, int64_t timeInSamples);

    /// As above, but remembers which note caused the hit so the GUI can show it.
    void noteOn (int instrument, int note, float velocity, int64_t timeInSamples);

    /// Fades out every voice of an instrument that matches a note. Kept simple:
    /// drum voices are one-shot, so a note off only matters for choking.
    void chokeInstrument (int instrument, int64_t fadeMs, int64_t timeInSamples);

    void allNotesOff();

    // -- rendering ---------------------------------------------------------

    /** Renders one block. `output` is `numOutputChannels` pointers to
        `numSamples` buffers; instrument i writes to channels 2i and 2i+1.
        Audio thread only.
    */
    void render (float* const* output, int numOutputChannels, int numSamples);

    /** Latency the tuner introduces for the current pitch settings, so the host
        can compensate. Buses whose instrument has no pitch shift are not delayed.
    */
    int latencySamples() const noexcept { return reportedLatency.load(); }

    int activeVoiceCount() const noexcept;

    /// Sample position at the end of the last rendered block, for the scope.
    int64_t currentSamplePosition() const noexcept { return sampleClock; }

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
    };

    /// A resolved round of an instrument: all of its channels, ready to sum.
    struct ResolvedHit
    {
        std::vector<ChannelRef> channels;
        juce::String name;
        float power = 1.0f;
        int64_t numFrames = 0;      ///< longest channel, i.e. when the round ends
    };

    struct InstrumentRuntime
    {
        std::vector<ResolvedHit> hits;
        juce::String group;
        juce::String name;
    };

    /// Audio-thread-visible copy of InstrumentParams.
    struct ChannelParams
    {
        float level = 1.0f;
        float pan = 0.0f;
        float pitch = 0.0f;
        float spread = 0.0f;
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

    /// A voice holds two WSOLA shifters, and PitchShifter is neither copyable nor
    /// movable, so voices are held by pointer. Allocated once at construction: the
    /// audio thread must never allocate.
    struct Voice
    {
        bool active = false;
        int instrument = 0;
        int hit = 0;
        int64_t startedAt = 0;
        int64_t endsAt = 0;          ///< absolute sample clock, 0 when not choking
        int64_t endsAtAbsolute = 0;  ///< when the sample data + flush runs out

        int64_t position = 0;        ///< read position within the sample, in frames
        int64_t startSkip = 0;       ///< output samples to discard so the note is sample accurate

        float velocity = 1.0f;
        float pitchSemitones = 0.0f;
        float attackGain = 0.0f;     ///< current value of the fade-in
        int   attackSamples = 0;

        PitchShifter shifter[2];
        bool shifterPrepared = false;

        // Reused scratch, sized to the largest block seen.
        std::vector<float> mixL, mixR, shiftedL, shiftedR, channelScratch;
    };

    /// Owns the voice pool. Kept out of Voice itself so the pool can be resized
    /// without ever moving a voice.
    struct VoicePool
    {
        std::vector<std::unique_ptr<Voice>> voices;
    };

    Voice* allocateVoice();
    int   chooseHit (int instrumentIndex);

    /// Which stereo output bus an instrument writes to. Instruments past the last
    /// bus share the final one rather than being dropped.
    int   busForInstrument (int instrument) const noexcept
    {
        return busCount > 0 ? std::min (instrument, busCount - 1) : 0;
    }

    void  resolveInstrumentChannels (const Instrument& instrument, InstrumentRuntime& out);
    void  recomputeLatency();
    void  recomputeSoloFlags();
    float nextRandom();
    void  pushHitEvent (const HitEvent& event) noexcept;

    static constexpr int maxVoices = 64;

    /// Cached latency of one tuned voice. Set once, then reused, because the
    /// WSOLA window size does not change with pitch.
    static int windowLatency;

    // -- audio thread state ------------------------------------------------
    VoicePool pool;
    std::vector<ChannelParams> channelParams;
    GlobalSnapshot globals;
    std::vector<int> roundRobinCursor;
    int64_t sampleClock = 0;
    int currentBlockSize = 512;
    std::atomic<int> reportedLatency { 0 };

    std::atomic<bool> kitLoaded { false };
    std::atomic<bool> anySolo { false };
    std::atomic<int> nextVoiceIndex { 0 };
    std::atomic<uint32_t> randomState { 0x12345678u };

    static constexpr int hitRingSize = 512;
    HitEvent hitRing[hitRingSize];
    std::atomic<int> hitWrite { 0 };
    std::atomic<int> hitRead { 0 };

    // -- kit state, guarded by kitLock --------------------------------------
    mutable juce::CriticalSection kitLock;
    std::vector<InstrumentRuntime> instruments_;
    std::vector<KitChannel> kitChannels_;
    int instrumentCount = 0;
    int busCount = 0;
    double currentSampleRate = 44100.0;
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
