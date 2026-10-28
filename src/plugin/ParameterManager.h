/*
    ParameterManager.h - Plugin parameter layout and MIDI learn queue.

    Extracted from PluginProcessor to separate parameter management concerns.
*/

#pragma once

#include "Config.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <string>

namespace glorp
{

class ParameterManager
{
public:
    ParameterManager();
    ~ParameterManager() = default;

    ParameterManager (const ParameterManager&) = delete;
    ParameterManager& operator= (const ParameterManager&) = delete;

    // -- Parameter layout -----------------------------------------------------

    void createParameterLayout (juce::AudioProcessor& processor);

    // -- Parameter access -----------------------------------------------------

    juce::AudioProcessorParameter* findParameter (const juce::String& paramID) const;
    juce::AudioProcessorParameter* getParameterForMidiControl (int cc, int channel) const;

    // -- Parameter pointers (for audio thread access) -------------------------

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

    // -- MIDI learn queue -----------------------------------------------------

    /// Called from audio thread to queue a control value for message thread.
    void enqueueControlValue (const juce::String& controlId, float normalised);

    /// Called from message thread to apply queued values to parameters.
    void drainControlQueue();

    /// Checks if a control ID is a master parameter.
    bool isMasterParameter (const juce::String& controlId) const;

    /// Applies a normalized value to a master parameter.
    void setMasterParameterValue (const juce::String& controlId, float normalised);

    /// Applies a normalized value to an instrument parameter.
    void setInstrumentParameterValue (int instrumentIndex, const juce::String& field, float normalised);

    /// Parses "instN.field" control ID into parts.
    bool parseControlId (const juce::String& controlId, int& instrumentIndex, juce::String& field) const;

    /// Generates a control ID for an instrument parameter.
    static juce::String controlIdForInstrument (const juce::String& suffix, int index);

private:
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

    bool dequeueControlValue (juce::String& controlId, float& normalised);
};

} // namespace glorp