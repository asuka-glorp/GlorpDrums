/*
    Metronome.cpp - Sample-accurate synthesized metronome & count-in engine for GlorpDrums.
*/

#include "Metronome.h"

#include <cmath>

namespace glorp
{

//==============================================================================
Metronome::Metronome()
{
    prepareToPlay (44100.0, 512);
}

void Metronome::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const double bpm = targetBpm.load (std::memory_order_relaxed);
    samplesPerBeat = static_cast<int64_t> (currentSampleRate * 60.0 / std::max (20.0, bpm));
    sampleCounter = 0;
    voice.active = false;
}

void Metronome::setBpm (double bpm) noexcept
{
    const double clamped = std::clamp (bpm, 30.0, 300.0);
    targetBpm.store (clamped, std::memory_order_relaxed);
    samplesPerBeat = static_cast<int64_t> (currentSampleRate * 60.0 / clamped);
}

void Metronome::setEnabled (bool enabled) noexcept
{
    metronomeEnabled.store (enabled, std::memory_order_relaxed);
    if (! enabled && ! countingIn.load (std::memory_order_relaxed))
        voice.active = false;
}

void Metronome::setTransportPlaying (bool isPlaying) noexcept
{
    transportPlaying.store (isPlaying, std::memory_order_relaxed);
    if (! isPlaying && ! countingIn.load (std::memory_order_relaxed))
    {
        voice.active = false;
        sampleCounter = 0;
    }
}

void Metronome::setCountInMode (CountInMode mode) noexcept
{
    countInMode.store (mode, std::memory_order_relaxed);
}

void Metronome::setClickSound (ClickSound sound) noexcept
{
    clickSound.store (sound, std::memory_order_relaxed);
}

void Metronome::setLevel (float level) noexcept
{
    clickLevel.store (std::clamp (level, 0.0f, 3.0f), std::memory_order_relaxed);
}

void Metronome::setTimeSignature (int numerator, int denominator) noexcept
{
    timeSigNumerator.store (std::max (1, numerator), std::memory_order_relaxed);
    timeSigDenominator.store (std::max (1, denominator), std::memory_order_relaxed);
}

void Metronome::setGridOffset (double offsetSeconds) noexcept
{
    gridOffsetSeconds.store (offsetSeconds, std::memory_order_relaxed);
}

void Metronome::startCountIn (double trackStartTimeSec)
{
    const auto mode = countInMode.load (std::memory_order_relaxed);
    const int numSig = timeSigNumerator.load (std::memory_order_relaxed);

    if (mode == CountInMode::off)
    {
        countingIn.store (false, std::memory_order_relaxed);
        isDelayingToTrackStart.store (false, std::memory_order_relaxed);
        currentBeat.store (1, std::memory_order_relaxed);
        if (onCountInComplete != nullptr)
            onCountInComplete();
        return;
    }

    const double bpm = targetBpm.load (std::memory_order_relaxed);
    const double secPerBeat = 60.0 / std::max (20.0, bpm);
    const double gridOffset = gridOffsetSeconds.load (std::memory_order_relaxed);

    // Calculate grid phase relative to trackStartTimeSec.
    // On the timeline, beats occur at t(k) = gridOffset + k * secPerBeat.
    // Find kLastBefore: the last beat at or strictly before trackStartTimeSec.
    const double timeRelative = trackStartTimeSec - gridOffset;
    const int64_t kLastBefore = static_cast<int64_t> (std::floor (timeRelative / secPerBeat));
    const double lastBeatTime = gridOffset + static_cast<double> (kLastBefore) * secPerBeat;
    const double delaySec = trackStartTimeSec - lastBeatTime; // 0.0 <= delaySec < secPerBeat

    const int minBars = (mode == CountInMode::twoBars) ? 2 : 1;
    const int minBeats = minBars * numSig;

    // To ensure the count-in starts cleanly on Beat 1 (downbeat) and delivers
    // at least the requested 1 or 2 bars before track playback starts:
    const int lastBeatNumber = static_cast<int> (((kLastBefore % numSig) + numSig) % numSig) + 1;
    const int beatsToCompleteBar = (lastBeatNumber == numSig) ? 0 : lastBeatNumber;
    const int totalBeats = minBeats + beatsToCompleteBar;

    countInRemaining.store (totalBeats, std::memory_order_relaxed);
    currentBeat.store (1, std::memory_order_relaxed); // Always starts on Downbeat 1!
    sampleCounter = samplesPerBeat; // Trigger first beat immediately on next render
    countingIn.store (true, std::memory_order_relaxed);
    isDelayingToTrackStart.store (false, std::memory_order_relaxed);
    preRollDelayCounter = 0;

    const int64_t delaySamples = std::clamp (static_cast<int64_t> (delaySec * currentSampleRate), (int64_t) 0, samplesPerBeat - 1);
    samplesUntilTrackStartAfterLastClick.store (delaySamples, std::memory_order_relaxed);
}

void Metronome::stop()
{
    countingIn.store (false, std::memory_order_relaxed);
    isDelayingToTrackStart.store (false, std::memory_order_relaxed);
    countInRemaining.store (0, std::memory_order_relaxed);
    transportPlaying.store (false, std::memory_order_relaxed);
    voice.active = false;
    sampleCounter = 0;
    preRollDelayCounter = 0;
    lastTrackBeatIndex = -999999;
}

void Metronome::triggerClick (bool isDownbeat, double /*sampleOffset*/)
{
    const auto sound = clickSound.load (std::memory_order_relaxed);
    voice.sound = sound;
    voice.active = true;
    voice.phase = 0.0f;
    voice.harmonicsPhase = 0.0f;
    voice.amplitude = isDownbeat ? 1.4f : 1.0f;

    float freq = isDownbeat ? 1600.0f : 1000.0f;
    float decay = 0.991f;

    switch (sound)
    {
        case ClickSound::woodblock:
            freq = isDownbeat ? 1500.0f : 1050.0f;
            decay = 0.992f;
            break;
        case ClickSound::studioClick:
            freq = isDownbeat ? 2400.0f : 1600.0f;
            decay = 0.985f;
            break;
        case ClickSound::sideStick:
            freq = isDownbeat ? 1300.0f : 900.0f;
            decay = 0.990f;
            break;
        case ClickSound::cowbell:
            freq = isDownbeat ? 840.0f : 560.0f;
            decay = 0.995f;
            break;
        case ClickSound::hihatPedal:
            freq = isDownbeat ? 4200.0f : 3100.0f;
            decay = 0.980f;
            break;
        case ClickSound::digitalBeep:
            freq = isDownbeat ? 2800.0f : 2000.0f;
            decay = 0.994f;
            break;
        case ClickSound::clave:
            freq = isDownbeat ? 2200.0f : 1750.0f;
            decay = 0.991f;
            break;
        case ClickSound::triangle:
            freq = isDownbeat ? 3200.0f : 2400.0f;
            decay = 0.996f;
            break;
    }

    voice.phaseDelta = static_cast<float> (2.0 * juce::MathConstants<double>::pi * freq / currentSampleRate);
    voice.harmonicsDelta = voice.phaseDelta * 1.54f; // Inharmonic partial for wood/metal click
    voice.decayRate = decay;
}

void Metronome::render (float* const* channels, int numChannels, int numSamples,
                        double trackPlayheadSeconds, bool trackIsPlaying)
{
    const bool isCountActive = countingIn.load (std::memory_order_relaxed);
    const bool isMetroActive = metronomeEnabled.load (std::memory_order_relaxed)
                               && (transportPlaying.load (std::memory_order_relaxed) || trackIsPlaying);

    if (! isMetroActive && ! isCountActive && ! voice.active)
        return;

    const float level = clickLevel.load (std::memory_order_relaxed);
    const int numSig = timeSigNumerator.load (std::memory_order_relaxed);
    const double bpm = targetBpm.load (std::memory_order_relaxed);
    const double secPerBeat = 60.0 / std::max (20.0, bpm);
    const double gridOffset = gridOffsetSeconds.load (std::memory_order_relaxed);

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. If counting in, count-in timer drives the clicks
        if (isCountActive)
        {
            if (isDelayingToTrackStart.load (std::memory_order_relaxed))
            {
                // Last count-in click has finished; wait exact fractional delay before launching track
                ++preRollDelayCounter;
                if (preRollDelayCounter >= samplesUntilTrackStartAfterLastClick.load (std::memory_order_relaxed))
                {
                    countingIn.store (false, std::memory_order_relaxed);
                    isDelayingToTrackStart.store (false, std::memory_order_relaxed);
                    preRollDelayCounter = 0;
                    lastTrackBeatIndex = -999999;
                    if (onCountInComplete != nullptr)
                        onCountInComplete();
                }
            }
            else
            {
                ++sampleCounter;
                if (sampleCounter >= samplesPerBeat)
                {
                    sampleCounter = 0;

                    const int beat = currentBeat.load (std::memory_order_relaxed);
                    const bool isDownbeat = (beat == 1);
                    triggerClick (isDownbeat, i);

                    if (onBeat != nullptr)
                        onBeat (beat, isDownbeat);

                    int nextBeat = beat + 1;
                    if (nextBeat > numSig)
                        nextBeat = 1;
                    currentBeat.store (nextBeat, std::memory_order_relaxed);

                    const int remaining = countInRemaining.fetch_sub (1, std::memory_order_relaxed) - 1;
                    if (remaining <= 0)
                    {
                        const int64_t delaySamples = samplesUntilTrackStartAfterLastClick.load (std::memory_order_relaxed);
                        if (delaySamples > 0)
                        {
                            isDelayingToTrackStart.store (true, std::memory_order_relaxed);
                            preRollDelayCounter = 0;
                        }
                        else
                        {
                            countingIn.store (false, std::memory_order_relaxed);
                            lastTrackBeatIndex = -999999;
                            if (onCountInComplete != nullptr)
                                onCountInComplete();
                        }
                    }
                }
            }
        }
        // 2. If backing track is playing, phase-lock clicks to (trackTime - gridOffset)
        else if (isMetroActive && trackIsPlaying && trackPlayheadSeconds >= 0.0)
        {
            const double t = trackPlayheadSeconds + (static_cast<double> (i) / currentSampleRate) - gridOffset;
            const int64_t beatIdx = static_cast<int64_t> (std::floor (t / secPerBeat));

            if (lastTrackBeatIndex != -999999 && (beatIdx == lastTrackBeatIndex + 1 || std::abs (beatIdx - lastTrackBeatIndex) > 1))
            {
                const int beat = static_cast<int> (((beatIdx % numSig) + numSig) % numSig) + 1;
                const bool isDownbeat = (beat == 1);

                triggerClick (isDownbeat, i);
                currentBeat.store (beat, std::memory_order_relaxed);
                if (onBeat != nullptr)
                    onBeat (beat, isDownbeat);
            }
            lastTrackBeatIndex = beatIdx;
        }
        // 3. Standalone practice metronome when no track is playing
        else if (isMetroActive)
        {
            ++sampleCounter;
            if (sampleCounter >= samplesPerBeat)
            {
                sampleCounter = 0;

                const int beat = currentBeat.load (std::memory_order_relaxed);
                const bool isDownbeat = (beat == 1);
                triggerClick (isDownbeat, i);

                if (onBeat != nullptr)
                    onBeat (beat, isDownbeat);

                int nextBeat = beat + 1;
                if (nextBeat > numSig)
                    nextBeat = 1;
                currentBeat.store (nextBeat, std::memory_order_relaxed);
            }
        }

        // Render click synthesizer voice
        if (voice.active)
        {
            voice.phase += voice.phaseDelta;
            if (voice.phase > 2.0f * juce::MathConstants<float>::pi)
                voice.phase -= 2.0f * juce::MathConstants<float>::pi;

            voice.harmonicsPhase += voice.harmonicsDelta;
            if (voice.harmonicsPhase > 2.0f * juce::MathConstants<float>::pi)
                voice.harmonicsPhase -= 2.0f * juce::MathConstants<float>::pi;

            float sampleVal = std::sin (voice.phase);
            if (voice.sound == ClickSound::hihatPedal)
            {
                static uint32_t noiseSeed = 0x12345678;
                noiseSeed = noiseSeed * 1664525u + 1013904223u;
                const float noise = static_cast<float> (static_cast<int32_t> (noiseSeed)) / 2147483648.0f;
                sampleVal = sampleVal * 0.35f + noise * 0.65f;
            }
            else if (voice.sound == ClickSound::cowbell || voice.sound == ClickSound::woodblock || voice.sound == ClickSound::triangle)
            {
                sampleVal = sampleVal * 0.65f + std::sin (voice.harmonicsPhase) * 0.35f;
            }
            else if (voice.sound == ClickSound::clave)
            {
                sampleVal = sampleVal * 0.8f + std::sin (voice.harmonicsPhase * 1.8f) * 0.2f;
            }

            // Louder punchy gain boost with soft tanh saturation
            sampleVal *= voice.amplitude * (level * 2.2f);
            voice.amplitude *= voice.decayRate;

            if (sampleVal > 1.0f || sampleVal < -1.0f)
                sampleVal = std::tanh (sampleVal);

            if (voice.amplitude < 0.0005f)
                voice.active = false;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (channels[ch] != nullptr)
                    channels[ch][i] += sampleVal;
            }
        }
    }
}

} // namespace glorp
