/*
    Metronome.h - Sample-accurate synthesized metronome & count-in engine for GlorpDrums.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>

namespace glorp
{

class Metronome
{
public:
    enum class CountInMode
    {
        off = 0,
        oneBar = 1,  // 4 beats
        twoBars = 2  // 8 beats
    };

    enum class ClickSound
    {
        woodblock = 0,
        studioClick = 1,
        sideStick = 2,
        cowbell = 3,
        hihatPedal = 4,
        digitalBeep = 5,
        clave = 6,
        triangle = 7
    };

    Metronome();
    ~Metronome() = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock);

    /// Real-time sample-accurate render called inside processBlock(). Zero allocations.
    void render (float* const* channels, int numChannels, int numSamples,
                 double trackPlayheadSeconds = -1.0, bool trackIsPlaying = false);

    // -- Thread-safe controls -------------------------------------------------
    void setBpm (double bpm) noexcept;
    double getBpm() const noexcept { return targetBpm.load (std::memory_order_relaxed); }

    void setEnabled (bool enabled) noexcept;
    bool isEnabled() const noexcept { return metronomeEnabled.load (std::memory_order_relaxed); }

    void setTransportPlaying (bool isPlaying) noexcept;
    bool isTransportPlaying() const noexcept { return transportPlaying.load (std::memory_order_relaxed); }

    void setCountInMode (CountInMode mode) noexcept;
    CountInMode getCountInMode() const noexcept { return countInMode.load (std::memory_order_relaxed); }

    void setClickSound (ClickSound sound) noexcept;
    ClickSound getClickSound() const noexcept { return clickSound.load (std::memory_order_relaxed); }

    void setLevel (float level) noexcept;
    float getLevel() const noexcept { return clickLevel.load (std::memory_order_relaxed); }

    void setTimeSignature (int numerator, int denominator) noexcept;

    void setGridOffset (double offsetSeconds) noexcept;
    double getGridOffset() const noexcept { return gridOffsetSeconds.load (std::memory_order_relaxed); }

    /// Triggers count-in state machine taking trackStartTime and gridOffset into account.
    void startCountIn (double trackStartTimeSec = 0.0);
    void stop();

    bool isCountingIn() const noexcept { return countingIn.load (std::memory_order_relaxed); }
    int getCurrentBeat() const noexcept { return currentBeat.load (std::memory_order_relaxed); }
    int getBeatsRemainingInCountIn() const noexcept { return countInRemaining.load (std::memory_order_relaxed); }

    /// Callback fired on the exact sample when count-in finishes.
    std::function<void()> onCountInComplete;

    /// Callback fired when each beat clicks (for UI pulse animation).
    std::function<void (int beat, bool isDownbeat)> onBeat;

private:
    void triggerClick (bool isDownbeat, double sampleOffset);

    double currentSampleRate = 44100.0;
    std::atomic<double> targetBpm { 120.0 };
    std::atomic<bool> metronomeEnabled { false };
    std::atomic<bool> transportPlaying { false };
    std::atomic<CountInMode> countInMode { CountInMode::off };
    std::atomic<ClickSound> clickSound { ClickSound::woodblock };
    std::atomic<float> clickLevel { 0.7f };
    std::atomic<int> timeSigNumerator { 4 };
    std::atomic<int> timeSigDenominator { 4 };

    std::atomic<bool> countingIn { false };
    std::atomic<bool> isDelayingToTrackStart { false };
    std::atomic<int64_t> samplesUntilTrackStartAfterLastClick { 0 };
    std::atomic<int> countInRemaining { 0 };
    std::atomic<int> currentBeat { 1 };
    std::atomic<double> gridOffsetSeconds { 0.0 };

    // DSP Oscillator & track sync state
    int64_t sampleCounter = 0;
    int64_t preRollDelayCounter = 0;
    int64_t samplesPerBeat = 22050;
    int64_t lastTrackBeatIndex = -999999;

    struct Voice
    {
        bool active = false;
        float phase = 0.0f;
        float phaseDelta = 0.0f;
        float amplitude = 0.0f;
        float decayRate = 0.99f;
        float harmonicsPhase = 0.0f;
        float harmonicsDelta = 0.0f;
        ClickSound sound = ClickSound::woodblock;
    } voice;
};

} // namespace glorp
