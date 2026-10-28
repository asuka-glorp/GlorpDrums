/*
    AudioEngine.h - Audio processing engine combining sampler, backing track,
    metronome, and recorders.

    Extracted from PluginProcessor to separate audio processing concerns.
*/

#pragma once

#include "Sampler.h"
#include "BackingTrackPlayer.h"
#include "Metronome.h"
#include "AudioRecorder.h"
#include "MidiRecorder.h"
#include "MidiRouter.h"
#include "Settings.h"
#include "Config.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace glorp
{

/// Result type for operations that can fail.
template <typename T, typename E>
class Result
{
public:
    Result (T value) : value_ (std::move (value)), hasValue_ (true) {}
    Result (E error) : error_ (std::move (error)), hasValue_ (false) {}

    bool isOk() const noexcept { return hasValue_; }
    bool isError() const noexcept { return ! hasValue_; }

    T& value() { return value_; }
    const T& value() const { return value_; }
    E& error() { return error_; }
    const E& error() const { return error_; }

    T valueOr (T defaultValue) const { return hasValue_ ? value_ : defaultValue; }

private:
    T value_;
    E error_;
    bool hasValue_ = false;
};

using ResultString = Result<void, juce::String>;

/// AudioEngine manages all audio processing components:
/// - Sampler (drum synthesis)
/// - BackingTrackPlayer (audio playback)
/// - Metronome (click track)
/// - AudioRecorder (WAV recording)
/// - MidiRecorder (MIDI recording)
/// - MidiRouter (MIDI message routing)
///
/// The audio thread calls processBlock() with the MIDI buffer,
/// which routes MIDI through the MidiRouter and renders all sources.
class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine() = default;

    AudioEngine (const AudioEngine&) = delete;
    AudioEngine& operator= (const AudioEngine&) = delete;

    // -- Setup -----------------------------------------------------------------

    /// Prepares all components for audio playback.
    void prepareToPlay (double sampleRate, int samplesPerBlock);

    /// Releases all resources (stops playback, clears voices, etc.).
    void releaseResources();

    /// Handles sample rate changes.
    void sampleRateChanged (double newRate);

    /// Handles block size changes.
    void blockSizeChanged (int newBlockSize);

    // -- Main audio processing -------------------------------------------------

    /// Processes one audio block. Called from the audio thread.
    /// `buffer` is the output audio buffer (channels = output bus count).
    /// `midi` is the incoming MIDI buffer from the host.
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    /// Returns the latency in samples introduced by the sampler.
    int getLatencySamples() const noexcept;

    // -- Component accessors ---------------------------------------------------

    Sampler& getSampler() noexcept { return sampler; }
    const Sampler& getSampler() const noexcept { return sampler; }

    BackingTrackPlayer& getBackingTrackPlayer() noexcept { return backingTrackPlayer; }
    Metronome& getMetronome() noexcept { return metronome; }
    AudioRecorder& getAudioRecorder() noexcept { return audioRecorder; }
    MidiRecorder& getMidiRecorder() noexcept { return midiRecorder; }
    MidiRouter& getMidiRouter() noexcept { return midiRouter; }
    Settings& getSettings() noexcept { return settings; }
    const Settings& getSettings() const noexcept { return settings; }

    // -- Kit management --------------------------------------------------------

    /// Loads a drum kit.
    ResultString loadKit (const juce::String& drumkitPath);

    /// Unloads the current kit.
    void unloadKit();

    /// Returns true if a kit is loaded.
    bool isKitLoaded() const noexcept { return sampler.isKitLoaded(); }

    /// Returns the current kit path.
    juce::String getCurrentKitPath() const noexcept { return currentKitPath; }

    // -- State management ------------------------------------------------------

    /// Serializes the entire engine state to XML.
    void getStateInformation (juce::MemoryBlock& destination);

    /// Restores the entire engine state from XML.
    void setStateInformation (const void* data, int sizeInBytes);

    // -- MIDI learn queue ------------------------------------------------------

    /// Called from the message thread to drain the MIDI learn control queue.
    void drainControlQueue();

    /// Checks if a control ID is a master parameter.
    bool isMasterParameter (const juce::String& controlId) const;

    /// Sets a master parameter value.
    void setMasterParameterValue (const juce::String& controlId, float normalised);

    /// Sets an instrument parameter value.
    void setInstrumentParameterValue (int instrumentIndex, const juce::String& field, float normalised);

    /// Parses "instN.field" control ID into parts.
    bool parseControlId (const juce::String& controlId, int& instrumentIndex, juce::String& field) const;

    /// Generates a control ID for an instrument parameter.
    static juce::String controlIdForInstrument (const juce::String& suffix, int index);

    // -- MIDI map presets ------------------------------------------------------

    /// Applies a MIDI map preset.
    void applyMapPreset (const juce::String& presetName);

    // -- Listener notifications ------------------------------------------------

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void kitChanged() = 0;
        virtual void parametersChanged() = 0;
    };

    using juce::AudioProcessor::addListener;
    using juce::AudioProcessor::removeListener;
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // -- Error reporting -------------------------------------------------------

    /// Returns the last kit load error.
    juce::String getLastKitError() const { return lastKitError; }

    // -- E-drum state accessors ------------------------------------------------

    float getHiHatCC4() const noexcept { return midiRouter.getHiHatPedal(); }
    float getSnarePositionCC16() const noexcept { return midiRouter.getSnarePosition(); }

    // -- Tempo worker ----------------------------------------------------------

    class TempoWorker;
    TempoWorker& getTempoWorker() noexcept { return tempoWorker; }

    // -- Undo manager ----------------------------------------------------------

    juce::UndoManager* getUndoManager() const noexcept { return undoManager; }
    void setUndoManager (juce::UndoManager* um) noexcept { undoManager = um; }

private:
    // -- Parameter accessors (for processBlock) --------------------------------

    juce::AudioParameterFloat* masterLevelParam = nullptr;
    juce::AudioParameterFloat* masterPanParam = nullptr;
    juce::AudioParameterFloat* attackParam = nullptr;
    juce::AudioParameterFloat* decayParam = nullptr;
    juce::AudioParameterFloat* humanizeParam = nullptr;
    juce::AudioParameterFloat* velCurveParam = nullptr;

    std::vector<juce::AudioParameterFloat*> instrumentLevelParams;
    std::vector<juce::AudioParameterFloat*> instrumentPanParams;
    std::vector<juce::AudioParameterFloat*> instrumentPitchParams;
    std::vector<juce::AudioParameterFloat*> instrumentDynamicsParams;
    std::vector<juce::AudioParameterChoice*> instrumentBusParams;

    // -- MIDI learn queue (audio thread -> message thread) --------------------

    struct PendingControl
    {
        std::string id;
        float value = 0.0f;
    };

    static constexpr int controlRingSize = config::CONTROL_RING_SIZE;
    PendingControl controlRing[controlRingSize];
    std::atomic<int> controlWrite { 0 };
    std::atomic<int> controlRead { 0 };

    void enqueueControlValue (const juce::String& controlId, float normalised);
    bool dequeueControlValue (juce::String& controlId, float& normalised);

    // -- Components ------------------------------------------------------------

    Sampler sampler;
    BackingTrackPlayer backingTrackPlayer;
    Metronome metronome;
    AudioRecorder audioRecorder;
    MidiRecorder midiRecorder;
    MidiRouter midiRouter;
    Settings settings;

    std::vector<KitEntry> kitList;
    juce::String currentKitPath;
    juce::String lastKitError;
    DrumKit currentKit;

    juce::ListenerList<Listener> listeners;
    juce::UndoManager* undoManager = nullptr;

    class TempoWorker tempoWorker;
};

} // namespace glorp