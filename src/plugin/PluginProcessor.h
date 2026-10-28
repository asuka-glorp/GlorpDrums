/*
    PluginProcessor.h - the JUCE bridge.

    Buses
    -----
    One MIDI input, and GLORP_MAX_INSTRUMENT_BUSES stereo outputs with no audio
    input. The output buses cannot change while the plugin is loaded: an LV2
    bundle's port list is baked into its .ttl when the host scans the plugin, and
    JUCE's LV2 wrapper has no way to renegotiate it afterwards. VST3 could, but
    having the two formats behave differently would be worse than a few idle
    buses. Unused buses stay silent and are named for whatever kit is loaded.
*/

#pragma once

#include "KitScanner.h"
#include "MidiMap.h"
#include "Settings.h"
#include "SetlistPlayer.h"
#include "SongDatabase.h"
#include "TempoDetector.h"
#include "ParameterManager.h"
#include "MidiRouter.h"
#include "AudioEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include "Config.h"

#include <atomic>
#include <memory>
#include <string>

namespace glorp
{

class PluginProcessor : public juce::AudioProcessor
{
public:
    static constexpr int maxSupportedInstruments = config::MAX_SUPPORTED_INSTRUMENTS;
    static constexpr int maxOutputBuses = config::MAX_OUTPUT_BUSES;

    PluginProcessor();
    ~PluginProcessor() override;

    // -- juce::AudioProcessor ----------------------------------------------
    const juce::String getName() const override { return JucePlugin_Name; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    bool canAddBus (bool /*isInput*/) const override { return false; }
    bool canRemoveBus (bool /*isInput*/) const override { return false; }

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // -- hosts -------------------------------------------------------------
    void sampleRateChanged (double);
    void blockSizeChanged (int);

    juce::AudioProcessorParameter* getParameterForMidiControl (int cc, int channel);

    juce::AudioProcessorParameter* findParameter (const juce::String& paramID) const;

    // -- GlorpDrums --------------------------------------------------------
    Sampler& getSampler() noexcept { return audioEngine.getSampler(); }
    MidiMap& getMidiMap() noexcept { return midiRouter.getMidiMap(); }
    Settings& getSettings() noexcept { return settings; }
    BackingTrackPlayer& getBackingTrackPlayer() noexcept { return audioEngine.getBackingTrackPlayer(); }
    Metronome& getMetronome() noexcept { return audioEngine.getMetronome(); }
    SongDatabase& getSongDatabase() noexcept { return songDatabase; }
    SetlistPlayer& getSetlistPlayer() noexcept { return setlistPlayer; }
    AudioRecorder& getAudioRecorder() noexcept { return audioEngine.getAudioRecorder(); }
    MidiRecorder& getMidiRecorder() noexcept { return audioEngine.getMidiRecorder(); }
    TempoDetector::Worker& getTempoWorker() noexcept { return tempoWorker; }

    AudioEngine& getAudioEngine() noexcept { return audioEngine; }
    const AudioEngine& getAudioEngine() const noexcept { return audioEngine; }

    /// The stable identifier a knob uses when it appears in a MIDI map.
    static juce::String controlIdForInstrument (const juce::String& suffix, int index);

    void loadKit (const juce::String& drumkitPath);
    void refreshKitList();

    const std::vector<KitEntry>& getKitList() const noexcept { return kitList; }
    juce::String getCurrentKitPath() const { return currentKitPath; }
    const DrumKit& getCurrentKit() const noexcept { return currentKit; }

    /// Applies a MIDI-map preset to the loaded kit.
    void applyMapPreset (const juce::String& presetName);

    /// E-drum continuous controls
    float getHiHatCC4() const noexcept { return lastHiHatCC4.load (std::memory_order_relaxed); }
    float getSnarePositionCC16() const noexcept { return lastSnareCC16.load (std::memory_order_relaxed); }

    /// Notifies the editor that kit-dependent state changed.
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

    /// Latest kit load error, for the UI to show.
    juce::String getLastKitError() const { return lastKitError; }

    /// Sets a named parameter and notifies the editor, used by MIDI learn.
    void setControlValue (const juce::String& controlId, float normalised);

    /// Called from the editor's timer so MIDI-learned CCs reach the host
    /// parameters on the message thread rather than the audio thread.
    void drainControlQueue();

private:
    static BusesProperties createBusesProperties();
    void notifyParametersChanged();

    ParameterManager parameterManager;
    MidiRouter midiRouter;
    AudioEngine audioEngine;

    Settings settings;
    SongDatabase songDatabase;
    SetlistPlayer setlistPlayer { songDatabase, audioEngine.getBackingTrackPlayer(), audioEngine.getMetronome() };
    TempoDetector::Worker tempoWorker;

    std::vector<KitEntry> kitList;
    juce::String currentKitPath;
    juce::String lastKitError;
    DrumKit currentKit;

    std::atomic<float> lastHiHatCC4 { 0.0f };
    std::atomic<float> lastSnareCC16 { 0.5f };

    juce::ListenerList<Listener> listeners;

    juce::UndoManager* undoManager = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace glorp
