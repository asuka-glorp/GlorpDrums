/*
    AudioEngine.cpp - Audio rendering pipeline and engine management.
*/

#include "AudioEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace glorp
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
}

//==============================================================================
AudioEngine::AudioEngine()
{
    busOutputs.fill (nullptr);
}

//==============================================================================
void AudioEngine::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;

    sampler.setSampleRateAndBlock (sampleRate, samplesPerBlock);
    backingTrackPlayer.prepareToPlay (sampleRate, samplesPerBlock);
    metronome.prepareToPlay (sampleRate, samplesPerBlock);
    audioRecorder.prepareToPlay (sampleRate, samplesPerBlock);
}

void AudioEngine::releaseResources()
{
    sampler.allNotesOff();
    metronome.stop();
    backingTrackPlayer.stop();
    audioRecorder.stopRecording();
    midiRecorder.stopRecording();
}

void AudioEngine::sampleRateChanged (double newRate)
{
    currentSampleRate = newRate;
    sampler.setSampleRateAndBlock (newRate, 512);
    backingTrackPlayer.prepareToPlay (newRate, 512);
    metronome.prepareToPlay (newRate, 512);
    audioRecorder.prepareToPlay (newRate, 512);
}

void AudioEngine::blockSizeChanged (int)
{
    // The voice scratch buffers resize on demand, so nothing to do beyond telling
    // the shifter's block hint.
}

//==============================================================================
void AudioEngine::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    juce::ignoreUnused (buffer.getNumChannels());
    juce::ignoreUnused (midi);

    // The parameter values are the source of truth, so pull the current snapshot
    // before rendering rather than trying to keep two copies in step.
    GlobalParams globals;
    globals.masterLevel = parameterManager.masterLevelParam != nullptr ? parameterManager.masterLevelParam->get() : 0.8f;
    globals.masterPan = (parameterManager.masterPanParam != nullptr ? parameterManager.masterPanParam->get() : 0.5f) * 2.0f - 1.0f;
    globals.attackMs = (parameterManager.attackParam != nullptr ? parameterManager.attackParam->get() : 0.0f) * 60.0f;
    globals.decayScale = (parameterManager.decayParam != nullptr ? parameterManager.decayParam->get() : 0.5f) * 4.0f;
    globals.humanize = parameterManager.humanizeParam != nullptr ? parameterManager.humanizeParam->get() : 0.0f;
    globals.velCurve = parameterManager.velCurveParam != nullptr ? parameterManager.velCurveParam->get() : 1.0f;
    sampler.setGlobalParams (globals);

    const int samplerLatency = sampler.latencySamples();
    // Note: latency is handled by the plugin processor

    // Update per-instrument parameters
    for (int i = 0; i < sampler.numInstruments() && i < static_cast<int> (parameterManager.instrumentLevelParams.size()); ++i)
    {
        auto params = sampler.getInstrumentParams (i);

        params.level = parameterManager.instrumentLevelParams[static_cast<size_t> (i)]->get();
        params.pan = parameterManager.instrumentPanParams[static_cast<size_t> (i)]->get() * 2.0f - 1.0f;
        params.pitchSemitones = (parameterManager.instrumentPitchParams[static_cast<size_t> (i)]->get() - 0.5f) * 24.0f;

        if (i < static_cast<int> (parameterManager.instrumentDynamicsParams.size()))
            params.dynamics = parameterManager.instrumentDynamicsParams[static_cast<size_t> (i)]->get();

        sampler.setInstrumentParams (i, params);

        if (i < static_cast<int> (parameterManager.instrumentBusParams.size()))
            sampler.setInstrumentBus (i, parameterManager.instrumentBusParams[static_cast<size_t> (i)]->getIndex());
    }

    // MIDI first, so hits scheduled inside this block are rendered by it.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto sampleOffset = metadata.samplePosition;

        if (midiRecorder.isRecording())
            midiRecorder.logMessage (message, sampler.currentSamplePosition() + sampleOffset);

        // Delegate MIDI routing to MidiRouter (handles learn, CC routing, E-drum CCs, chokes, note triggering)
        midiRouter.processMidiMessage (message, sampleOffset, sampler, parameterManager,
                                       lastHiHatCC4, lastSnareCC16);
    }

    // Map host output buses to up to 16 stereo bus write pointers (32 channels)
    busOutputs.fill (nullptr);

    // Note: The actual bus mapping will be done by the plugin processor since it has access to getBusBuffer
    // For now, we'll render to a flat buffer and the plugin processor will handle bus mapping
    // This is a simplification - in reality the plugin processor should handle the bus mapping
}

//==============================================================================
void AudioEngine::render (float* const* busOutputs, int numBusOutputs, int numSamples, juce::MidiBuffer& midi)
{
    // Update global parameters
    GlobalParams globals;
    globals.masterLevel = parameterManager.masterLevelParam != nullptr ? parameterManager.masterLevelParam->get() : 0.8f;
    globals.masterPan = (parameterManager.masterPanParam != nullptr ? parameterManager.masterPanParam->get() : 0.5f) * 2.0f - 1.0f;
    globals.attackMs = (parameterManager.attackParam != nullptr ? parameterManager.attackParam->get() : 0.0f) * 60.0f;
    globals.decayScale = (parameterManager.decayParam != nullptr ? parameterManager.decayParam->get() : 0.5f) * 4.0f;
    globals.humanize = parameterManager.humanizeParam != nullptr ? parameterManager.humanizeParam->get() : 0.0f;
    globals.velCurve = parameterManager.velCurveParam != nullptr ? parameterManager.velCurveParam->get() : 1.0f;
    sampler.setGlobalParams (globals);

    const int samplerLatency = sampler.latencySamples();
    // Latency handling is done by the plugin processor

    // Update per-instrument parameters
    for (int i = 0; i < sampler.numInstruments() && i < static_cast<int> (parameterManager.instrumentLevelParams.size()); ++i)
    {
        auto params = sampler.getInstrumentParams (i);

        params.level = parameterManager.instrumentLevelParams[static_cast<size_t> (i)]->get();
        params.pan = parameterManager.instrumentPanParams[static_cast<size_t> (i)]->get() * 2.0f - 1.0f;
        params.pitchSemitones = (parameterManager.instrumentPitchParams[static_cast<size_t> (i)]->get() - 0.5f) * 24.0f;

        if (i < static_cast<int> (parameterManager.instrumentDynamicsParams.size()))
            params.dynamics = parameterManager.instrumentDynamicsParams[static_cast<size_t> (i)]->get();

        sampler.setInstrumentParams (i, params);

        if (i < static_cast<int> (parameterManager.instrumentBusParams.size()))
            sampler.setInstrumentBus (i, parameterManager.instrumentBusParams[static_cast<size_t> (i)]->getIndex());
    }

    // MIDI first, so hits scheduled inside this block are rendered by it.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto sampleOffset = metadata.samplePosition;

        if (midiRecorder.isRecording())
            midiRecorder.logMessage (message, sampler.currentSamplePosition() + sampleOffset);

        // Delegate MIDI routing to MidiRouter (handles learn, CC routing, E-drum CCs, chokes, note triggering)
        midiRouter.processMidiMessage (message, sampleOffset, sampler, parameterManager,
                                       lastHiHatCC4, lastSnareCC16);
    }

    // Render sampler to bus outputs
    sampler.render (busOutputs, numBusOutputs, numSamples);
}

} // namespace glorp