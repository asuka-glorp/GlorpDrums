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
#include "Sampler.h"
#include "Settings.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <string>

namespace glorp
{

class PluginProcessor : public juce::AudioProcessor
{
public:
    static constexpr int maxSupportedInstruments = 64;
    static constexpr int maxOutputBuses = 16;

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
    Sampler& getSampler() noexcept { return sampler; }
    MidiMap& getMidiMap() noexcept { return midiMap; }
    Settings& getSettings() noexcept { return settings; }

    /// The stable identifier a knob uses when it appears in a MIDI map.
    static juce::String controlIdForInstrument (const juce::String& suffix, int index);

    void loadKit (const juce::String& drumkitPath);
    void refreshKitList();

    const std::vector<KitEntry>& getKitList() const noexcept { return kitList; }
    juce::String getCurrentKitPath() const { return currentKitPath; }

    /// Applies a MIDI-map preset to the loaded kit.
    void applyMapPreset (const juce::String& presetName);

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
    /// A MIDI-learned control value waiting for the message thread.
    struct PendingControl
    {
        std::string id;
        float value = 0.0f;
    };

    static constexpr int controlRingSize = 128;
    PendingControl controlRing[controlRingSize];
    std::atomic<int> controlWrite { 0 };
    std::atomic<int> controlRead { 0 };

    void enqueueControlValue (const juce::String& controlId, float normalised);
    bool dequeueControlValue (juce::String& controlId, float& normalised);

    static BusesProperties createBusesProperties();
    void createParameterLayout();
    void notifyParametersChanged();

    /// Parses a control id like "inst.3.pitch" back into its parts.
    bool parseControlId (const juce::String& controlId, int& instrumentIndex,
                         juce::String& field) const;

    void applyControlValue (int instrumentIndex, const juce::String& field, float normalised);

    Sampler sampler;
    MidiMap midiMap;
    Settings settings;

    std::vector<KitEntry> kitList;
    juce::String currentKitPath;
    juce::String lastKitError;

    juce::ListenerList<Listener> listeners;

    // Parameter handles, kept so state and MIDI learn can address them.
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

    juce::UndoManager* undoManager = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace glorp
