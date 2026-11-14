/*
    AudioEngine.cpp - Audio rendering pipeline and engine management.
*/

#include "AudioEngine.h"
#include "DebugLog.h"
#include "MidiRouter.h"
#include "ParameterManager.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace glorp
{

namespace
{
constexpr double pi = 3.14159265358979323846;
}

//==============================================================================
AudioEngine::AudioEngine()
{
    busOutputs.fill(nullptr);
}

//==============================================================================
void AudioEngine::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    hasCachedParams = false;

    DebugLog::instance().logInfo("AUDIO",
                                 "prepareToPlay: sampleRate=" + juce::String(sampleRate, 1)
                                     + " Hz, blockSize=" + juce::String(samplesPerBlock));

    sampler.setSampleRateAndBlock(sampleRate, samplesPerBlock);
    backingTrackPlayer.prepareToPlay(sampleRate, samplesPerBlock);
    metronome.prepareToPlay(sampleRate, samplesPerBlock);
    audioRecorder.prepareToPlay(sampleRate, samplesPerBlock);
    streamOutputBridge.prepareToPlay(sampleRate, samplesPerBlock);
}

void AudioEngine::releaseResources()
{
    sampler.allNotesOff();
    metronome.stop();
    backingTrackPlayer.stop();
    audioRecorder.stopRecording();
    midiRecorder.stopRecording();
}

void AudioEngine::sampleRateChanged(double newRate)
{
    currentSampleRate = newRate;
    hasCachedParams = false;

    DebugLog::instance().logInfo("AUDIO",
                                 "sampleRateChanged: " + juce::String(newRate, 1)
                                     + " Hz (blockSize=" + juce::String(currentBlockSize) + ")");

    sampler.setSampleRateAndBlock(newRate, currentBlockSize);
    backingTrackPlayer.prepareToPlay(newRate, currentBlockSize);
    metronome.prepareToPlay(newRate, currentBlockSize);
    audioRecorder.prepareToPlay(newRate, currentBlockSize);
    streamOutputBridge.prepareToPlay(newRate, currentBlockSize);
}

void AudioEngine::blockSizeChanged(int)
{
    // The voice scratch buffers resize on demand, so nothing to do beyond telling
    // the shifter's block hint.
}

//==============================================================================
void AudioEngine::render(float *const *busOutputs,
                         int numBusOutputs,
                         int numSamples,
                         juce::MidiBuffer &midi,
                         ParameterManager &parameterManager,
                         MidiRouter &midiRouter,
                         std::atomic<float> &lastHiHatCC4,
                         std::atomic<float> &lastSnareCC16)
{
    // Update global parameters only when changed
    GlobalParams globals;
    globals.masterLevel =
        parameterManager.masterLevelParam != nullptr ? parameterManager.masterLevelParam->get() : 0.8f;
    globals.masterPan =
        (parameterManager.masterPanParam != nullptr ? parameterManager.masterPanParam->get() : 0.5f) * 2.0f - 1.0f;
    globals.attackMs = (parameterManager.attackParam != nullptr ? parameterManager.attackParam->get() : 0.0f) * 60.0f;
    globals.decayScale = (parameterManager.decayParam != nullptr ? parameterManager.decayParam->get() : 0.5f) * 4.0f;
    globals.humanize = parameterManager.humanizeParam != nullptr ? parameterManager.humanizeParam->get() : 0.0f;
    globals.velCurve = parameterManager.velCurveParam != nullptr ? parameterManager.velCurveParam->get() : 1.0f;

    if (!hasCachedParams || globals.masterLevel != cachedGlobals.masterLevel
        || globals.masterPan != cachedGlobals.masterPan || globals.attackMs != cachedGlobals.attackMs
        || globals.decayScale != cachedGlobals.decayScale || globals.humanize != cachedGlobals.humanize
        || globals.velCurve != cachedGlobals.velCurve)
    {
        sampler.setGlobalParams(globals);
        cachedGlobals = globals;
    }

    // Update per-instrument parameters only for instruments whose APVTS state changed
    const int instCount =
        std::min(sampler.numInstruments(), static_cast<int>(parameterManager.instrumentLevelParams.size()));
    const int cacheCap = static_cast<int>(config::MAX_SUPPORTED_INSTRUMENTS);

    for (int i = 0; i < instCount && i < cacheCap; ++i)
    {
        const size_t idx = static_cast<size_t>(i);
        const float newLevel = parameterManager.instrumentLevelParams[idx]->get();
        const float newPan = parameterManager.instrumentPanParams[idx]->get() * 2.0f - 1.0f;
        const float newPitch = (parameterManager.instrumentPitchParams[idx]->get() - 0.5f) * 24.0f;
        const float newDynamics = i < static_cast<int>(parameterManager.instrumentDynamicsParams.size())
                                      ? parameterManager.instrumentDynamicsParams[idx]->get()
                                      : cachedDynamics[idx];

        if (!hasCachedParams || newLevel != cachedLevel[idx] || newPan != cachedPan[idx] || newPitch != cachedPitch[idx]
            || newDynamics != cachedDynamics[idx])
        {
            auto params = sampler.getInstrumentParams(i);
            params.level = newLevel;
            params.pan = newPan;
            params.pitchSemitones = newPitch;
            if (i < static_cast<int>(parameterManager.instrumentDynamicsParams.size()))
                params.dynamics = newDynamics;
            sampler.setInstrumentParams(i, params);

            cachedLevel[idx] = newLevel;
            cachedPan[idx] = newPan;
            cachedPitch[idx] = newPitch;
            cachedDynamics[idx] = newDynamics;
        }

        if (i < static_cast<int>(parameterManager.instrumentBusParams.size()))
        {
            const int newBus = parameterManager.instrumentBusParams[idx]->getIndex();
            if (!hasCachedParams || newBus != cachedBus[idx])
            {
                sampler.setInstrumentBus(i, newBus);
                cachedBus[idx] = newBus;
            }
        }
    }

    hasCachedParams = true;

    // MIDI first, so hits scheduled inside this block are rendered by it.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto sampleOffset = metadata.samplePosition;

        if (midiRecorder.isRecording())
            midiRecorder.logMessage(message, sampler.currentSamplePosition() + sampleOffset);

        // Delegate MIDI routing to MidiRouter (handles learn, CC routing, E-drum CCs, chokes, note triggering)
        midiRouter.processMidiMessage(message, sampleOffset, sampler, parameterManager, lastHiHatCC4, lastSnareCC16);
    }

    // Render sampler to bus outputs
    sampler.render(busOutputs, numBusOutputs, numSamples);
}

} // namespace glorp