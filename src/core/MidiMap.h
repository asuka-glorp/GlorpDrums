/*
    MidiMap.h - note-to-instrument mapping, MIDI learn, and presets.

    One map covers three things at once:
      - which MIDI note triggers which instrument
      - which CC or note automates which control (knobs, per-instrument level)
      - the output routing defaults that travel with a preset

    A preset is a small XML file. The built-ins cover the layouts drum pads
    actually ship with (General MIDI, Push, the Roland and Yamaha kits), because
    getting that wrong by hand is the single most tedious part of setting up a
    drum plugin.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace glorp
{

/** A MIDI learn binding: a control (identified by a stable string id) driven by
    a CC, or by a note if `useNote` is set.
*/
struct MidiBinding
{
    std::string controlId;
    int cc = -1;        ///< CC number, or note number when `useNote`
    int channel = 0;    ///< 0 = omni
    bool useNote = false;

    bool isValid() const noexcept { return cc >= 0 && ! controlId.empty(); }
};

/// One preset: note assignments plus knob bindings.
///
/// Assignments are stored by instrument *name* rather than index, because the
/// index depends on which kit is loaded and a preset written for one kit has to
/// still do something sensible on the next one. Indices are accepted too, and
/// used when the name is not found.
struct MapPreset
{
    juce::String name;
    juce::String fileName;                  ///< empty when the preset is built in
    std::map<int, juce::String> noteToInstrumentName;
    std::map<int, int> noteToInstrument;
    std::vector<MidiBinding> bindings;
    bool isBuiltIn = false;
};

class MidiMap
{
public:
    MidiMap();

    // -- note assignments --------------------------------------------------
    /// Assigns a note to an instrument. Returns false if the note was already
    /// taken by a different instrument and `steal` is false.
    bool assignNote (int note, int instrumentIndex, bool steal = true);

    /// Removes any note assigned to this instrument.
    void clearInstrument (int instrumentIndex);

    /// Instrument triggered by a note, or -1.
    int instrumentForNote (int note) const noexcept;

    /// Note assigned to an instrument, or -1.
    int noteForInstrument (int instrumentIndex) const noexcept;

    const std::map<int, int>& noteAssignments() const noexcept { return noteToInstrument; }

    /// Fires when a note assignment changes, so the UI can redraw.
    std::function<void()> onChanged;

    // -- knob bindings -----------------------------------------------------
    void clearBindings();
    bool addBinding (const MidiBinding& binding);
    void removeBindingForControl (const std::string& controlId);
    void removeBindingForMessage (int cc, bool useNote, int channel);
    const std::vector<MidiBinding>& bindings() const noexcept { return this->bindings_; }

    /// Control driven by an incoming CC, or an empty string.
    juce::String controlForMessage (int cc, bool useNote, int channel) const;

    // -- learn -------------------------------------------------------------
    /// Arms learning. The next matching message is captured for `controlId`.
    void beginLearn (const std::string& controlId, bool useNote = false, int channel = 0);
    void cancelLearn();
    bool isLearning() const noexcept { return learningControlId_ != nullptr; }
    const std::string* learningControlId() const noexcept { return learningControlId_.get(); }

    /** Offers a message to the learner. Returns true if it was consumed.
        If `learnedControlId` is given it receives the id that was just bound.
    */
    bool offerMessage (const juce::MidiMessage& message, juce::String* learnedControlId = nullptr);

    // -- presets -----------------------------------------------------------
    void clearAssignments();

    /// Parses a preset file. Resolution of instrument names against a loaded kit
    /// happens later, in applyPreset().
    static bool loadPresetFile (const juce::File& file, MapPreset& out, juce::String& error);

    /// `instrumentNames` is indexed by instrument index, so the saved preset
    /// stays readable when the same map is applied to a different kit.
    void savePresetFile (const juce::File& file, const juce::String& name,
                         const std::vector<juce::String>& instrumentNames) const;

    /// Presets shipped with the plugin plus any the user has added.
    static std::vector<MapPreset> availablePresets();

    /// Directory the bundled presets live in, for the "save a copy" workflow.
    static juce::File bundledPresetDirectory();

    /// The user's own preset directory, created if absent.
    static juce::File userPresetDirectory();

    /// Applies a preset, matching instruments by name so a preset written for one
    /// kit still does something sensible on another.
    /// `kitInstrumentNames` is indexed by instrument index.
    void applyPreset (const MapPreset& preset, const std::vector<juce::String>& kitInstrumentNames);

    /// Serialises the map for the plugin state.
    juce::XmlElement* exportXml() const;

    /// Restores a map written by exportXml. Indices are kept as-is; the map is
    /// remapped against the loaded kit by the caller if the kit changed.
    void importXml (const juce::XmlElement& xml);

private:
    std::map<int, int> noteToInstrument;
    std::map<int, int> instrumentToNote;
    std::vector<MidiBinding> bindings_;

    std::unique_ptr<std::string> learningControlId_;
    bool   learningUsesNote = false;
    int    learningChannel = 0;
};

} // namespace glorp
