/*
    MidiRouter.h - MIDI note/CC routing, MIDI learn, and preset management.

    Extracted from PluginProcessor to separate MIDI routing concerns.
*/

#pragma once

#include "Config.h"
#include "DrumKit.h"
#include "MidiMap.h"
#include "ParameterManager.h"
#include "Sampler.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <string>
#include <vector>

namespace glorp
{

class MidiRouter
{
public:
    MidiRouter();
    ~MidiRouter() = default;

    MidiRouter (const MidiRouter&) = delete;
    MidiRouter& operator= (const MidiRouter&) = delete;

    // -- Core MIDI processing -------------------------------------------------

    /// Processes a MIDI message for routing. Returns true if the message was
    /// consumed (e.g., by MIDI learn) and should not be passed to the sampler.
    bool processMidiMessage (const juce::MidiMessage& message,
                              int sampleOffset,
                              Sampler& sampler,
                              ParameterManager& parameterManager,
                              std::atomic<float>& lastHiHatCC4,
                              std::atomic<float>& lastSnareCC16);

    /// Called from message thread to drain MIDI learn queue to parameters.
    void drainMidiLearnQueue (ParameterManager& parameterManager);

    // -- Note mapping ---------------------------------------------------------

    MidiMap& getMidiMap() noexcept { return midiMap; }
    const MidiMap& getMidiMap() const noexcept { return midiMap; }

    /// Sets up note mapping from a loaded kit's suggested map.
    void setupNoteMappingFromKit (const DrumKit& kit, Sampler& sampler);

    /// Applies a MIDI map preset by name.
    void applyMapPreset (const juce::String& presetName, Sampler& sampler);

    /// Gets available MIDI map presets.
    static std::vector<MapPreset> getAvailablePresets();

    // -- MIDI learn -----------------------------------------------------------

    /// Arms MIDI learn for a specific control ID.
    void beginMidiLearn (const juce::String& controlId);

    /// Cancels MIDI learn mode.
    void cancelMidiLearn();

    /// Checks if MIDI learn is active.
    bool isMidiLearning() const noexcept { return midiMap.isLearning(); }

    /// Gets the control ID currently being learned.
    const std::string* getLearningControlId() const noexcept { return midiMap.learningControlId(); }

    // -- Callbacks ------------------------------------------------------------

    std::function<void()> onMidiMapChanged;

private:
    MidiMap midiMap;

    // Helper to strip names for comparison
    static juce::String stripName (const juce::String& s);

    // Default MIDI note assignments by instrument name
    int getDefaultNoteForInstrument (const juce::String& name) const;
};

} // namespace glorp