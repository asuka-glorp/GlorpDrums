/*
    AudioEngine.h - Audio rendering pipeline and engine management.

    Extracted from PluginProcessor to separate audio processing concerns.
    Handles the core audio thread operations: rendering, MIDI processing,
    and coordination of all audio sub-components.
*/

#pragma once

#include "Config.h"
#include "Sampler.h"
#include "BackingTrackPlayer.h"
#include "Metronome.h"
#include "AudioRecorder.h"
#include "MidiRecorder.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace glorp
{

class ParameterManager;
class MidiRouter;

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine() = default;

    AudioEngine (const AudioEngine&) = delete;
    AudioEngine& operator= (const AudioEngine&) = delete;

    // -- Lifecycle ------------------------------------------------------------

    void prepareToPlay (double sampleRate, int samplesPerBlock);
    void releaseResources();

    void sampleRateChanged (double newRate);
    void blockSizeChanged (int);

    // -- Audio rendering ------------------------------------------------------

    // Note: processBlock() was removed as it's not used - PluginProcessor calls render() directly.
    // If needed for standalone, it can be re-added with ParameterManager/MidiRouter parameters.

    // -- Sub-component access -------------------------------------------------

    Sampler& getSampler() noexcept { return sampler; }
    const Sampler& getSampler() const noexcept { return sampler; }

    BackingTrackPlayer& getBackingTrackPlayer() noexcept { return backingTrackPlayer; }
    Metronome& getMetronome() noexcept { return metronome; }
    AudioRecorder& getAudioRecorder() noexcept { return audioRecorder; }
    MidiRecorder& getMidiRecorder() noexcept { return midiRecorder; }

    // -- Engine state ---------------------------------------------------------

    double getCurrentSampleRate() const noexcept { return currentSampleRate; }
    int getCurrentBlockSize() const noexcept { return currentBlockSize; }

    /// Returns the current latency in samples (from sampler).
    int getLatencySamples() const noexcept { return sampler.latencySamples(); }

    // -- Core rendering -------------------------------------------------------

    /// Processes audio with parameters, MIDI, and renders to bus outputs.
    /// Call from PluginProcessor::processBlock after setting up bus outputs.
    void render (float* const* busOutputs, int numBusOutputs, int numSamples, juce::MidiBuffer& midi,
                 ParameterManager& parameterManager, MidiRouter& midiRouter,
                 std::atomic<float>& lastHiHatCC4, std::atomic<float>& lastSnareCC16);

    // -- Configuration --------------------------------------------------------

    void setMultiOutMode (bool enabled) { sampler.setMultiOutMode (enabled); }
    bool isMultiOutMode() const noexcept { return sampler.isMultiOutMode(); }

private:
    // -- Sub-components -------------------------------------------------------
    Sampler sampler;
    BackingTrackPlayer backingTrackPlayer;
    Metronome metronome;
    AudioRecorder audioRecorder;
    MidiRecorder midiRecorder;

    // -- Engine state ---------------------------------------------------------
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    // -- Rendering helpers ----------------------------------------------------
    std::array<float*, config::MAX_OUTPUT_BUSES * 2> busOutputs {};
};

} // namespace glorp