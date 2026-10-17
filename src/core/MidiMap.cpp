#include "MidiMap.h"

#include <algorithm>

namespace glorp
{

MidiMap::MidiMap() = default;

//==============================================================================
bool MidiMap::assignNote (int note, int instrumentIndex, bool steal)
{
    if (note < 0 || note > 127 || instrumentIndex < 0)
        return false;

    if (const auto existing = noteToInstrument.find (note);
        existing != noteToInstrument.end() && existing->second != instrumentIndex)
    {
        if (! steal)
            return false;

        // Whoever had this note before loses it, rather than both instruments
        // silently claiming the same key.
        if (auto it = instrumentToNote.find (existing->second); it != instrumentToNote.end())
            instrumentToNote.erase (it);

        existing->second = instrumentIndex;
    }
    else
    {
        noteToInstrument[note] = instrumentIndex;
    }

    if (auto it = instrumentToNote.find (instrumentIndex); it != instrumentToNote.end() && it->second != note)
        instrumentToNote.erase (it);

    instrumentToNote[instrumentIndex] = note;

    if (onChanged)
        onChanged();

    return true;
}

void MidiMap::clearInstrument (int instrumentIndex)
{
    if (auto it = instrumentToNote.find (instrumentIndex); it != instrumentToNote.end())
    {
        noteToInstrument.erase (it->second);
        instrumentToNote.erase (it);

        if (onChanged)
            onChanged();
    }
}

int MidiMap::instrumentForNote (int note) const noexcept
{
    const auto it = noteToInstrument.find (note);
    return it != noteToInstrument.end() ? it->second : -1;
}

int MidiMap::noteForInstrument (int instrumentIndex) const noexcept
{
    const auto it = instrumentToNote.find (instrumentIndex);
    return it != instrumentToNote.end() ? it->second : -1;
}

void MidiMap::clearAssignments()
{
    noteToInstrument.clear();
    instrumentToNote.clear();

    if (onChanged)
        onChanged();
}

//==============================================================================
void MidiMap::clearBindings()
{
    bindings_.clear();
}

bool MidiMap::addBinding (const MidiBinding& binding)
{
    if (! binding.isValid())
        return false;

    removeBindingForControl (binding.controlId);
    removeBindingForMessage (binding.cc, binding.useNote, binding.channel);
    bindings_.push_back (binding);
    return true;
}

void MidiMap::removeBindingForControl (const std::string& controlId)
{
    bindings_.erase (std::remove_if (bindings_.begin(), bindings_.end(),
                                     [&] (const MidiBinding& b) { return b.controlId == controlId; }),
                     bindings_.end());
}

void MidiMap::removeBindingForMessage (int cc, bool useNote, int channel)
{
    bindings_.erase (std::remove_if (bindings_.begin(), bindings_.end(),
                                     [&] (const MidiBinding& b)
                                     {
                                         return b.cc == cc && b.useNote == useNote
                                             && (b.channel == 0 || channel == 0 || b.channel == channel);
                                     }),
                     bindings_.end());
}

juce::String MidiMap::controlForMessage (int cc, bool useNote, int channel) const
{
    for (const auto& b : bindings_)
    {
        if (b.cc == cc && b.useNote == useNote)
            if (b.channel == 0 || b.channel == channel)
                return juce::String (b.controlId);
    }

    return {};
}

//==============================================================================
void MidiMap::beginLearn (const std::string& controlId, bool useNote, int channel)
{
    cancelLearn();
    learningControlId_ = std::make_unique<std::string> (controlId);
    learningUsesNote = useNote;
    learningChannel = channel;
}

void MidiMap::cancelLearn()
{
    learningControlId_.reset();
    learningUsesNote = false;
    learningChannel = 0;
}

bool MidiMap::offerMessage (const juce::MidiMessage& message, juce::String* learnedControlId)
{
    if (learningControlId_ == nullptr)
        return false;

    if (message.isAllNotesOff() || message.isAllSoundOff() || message.isMetaEvent())
        return false;

    const bool isNote = message.isNoteOn() || message.isNoteOff();

    if (isNote != learningUsesNote)
        return false;

    if (learningChannel > 1 && message.getChannel() + 1 != learningChannel)
        return false;

    MidiBinding binding;
    binding.controlId = *learningControlId_;
    binding.cc = isNote ? message.getNoteNumber() : message.getControllerNumber();
    binding.channel = message.getChannel() + 1;
    binding.useNote = isNote;

    const auto captured = binding.controlId;

    cancelLearn();
    addBinding (binding);

    if (learnedControlId != nullptr)
        *learnedControlId = juce::String (captured);

    if (onChanged)
        onChanged();

    return true;
}

//==============================================================================
juce::File MidiMap::bundledPresetDirectory()
{
   #if defined(GLORP_PRESET_DIR)
    return juce::File (GLORP_PRESET_DIR);
   #else
    return {};
   #endif
}

juce::File MidiMap::userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("GlorpDrums")
               .getChildFile ("presets");
}

std::vector<MapPreset> MidiMap::availablePresets()
{
    std::vector<MapPreset> presets;

    struct BuiltIn
    {
        const char* name;
        std::vector<std::pair<int, const char*>> notes;
    };

    static const BuiltIn builtIns[] =
    {
        { "General MIDI", {
            { 35, "Kick" }, { 36, "Kick" }, { 37, "SideStick" }, { 38, "Snare" },
            { 40, "Rimshot" }, { 42, "HihatClosed" }, { 44, "HihatPedal" }, { 46, "HihatOpen" },
            { 48, "Tom1" }, { 45, "Tom2" }, { 43, "FloorTom" }, { 41, "FloorTom" },
            { 49, "Crash" }, { 51, "Ride" }, { 53, "RideBell" } } },

        { "General MIDI (extended)", {
            { 35, "Kick" }, { 36, "Kick" }, { 37, "SideStick" }, { 38, "Snare" },
            { 39, "Clap" }, { 40, "Rimshot" }, { 41, "FloorTom" }, { 42, "HihatClosed" },
            { 43, "LowFloorTom" }, { 44, "HihatPedal" }, { 45, "Tom2" }, { 46, "HihatOpen" },
            { 47, "Tom3" }, { 48, "Tom1" }, { 49, "Crash" }, { 50, "HighTom" },
            { 51, "Ride" }, { 53, "RideBell" }, { 54, "Tambourine" }, { 55, "Splash" },
            { 56, "Cowbell" }, { 57, "Crash2" }, { 52, "China" } } },

        { "Roland V-Drums", {
            { 36, "Kick" }, { 35, "Kick" },
            { 38, "SnareHead" }, { 40, "Rimshot" }, { 37, "CrossStick" },
            { 42, "HihatClosed" }, { 22, "HihatClosed" }, { 26, "HihatSemiOpen" },
            { 46, "HihatOpen" }, { 44, "HihatPedal" }, { 80, "HihatSemiOpen" },
            { 48, "Tom1Head" }, { 50, "Tom1Rim" },
            { 45, "Tom2Head" }, { 47, "Tom2Rim" },
            { 43, "FTomHead" }, { 58, "FTomRim" },
            { 49, "Crash1Bow" }, { 55, "Crash1Edge" },
            { 57, "Crash2Bow" }, { 52, "Crash2Edge" },
            { 51, "RideBow" }, { 53, "RideBell" }, { 59, "RideEdge" },
            { 54, "Tambourine" }, { 56, "Cowbell" } } },

        { "Yamaha DTX", {
            { 36, "Kick" }, { 35, "Kick" },
            { 38, "SnareHead" }, { 40, "Rimshot" }, { 37, "CrossStick" },
            { 42, "HihatClosed" }, { 26, "HihatSemiOpen" }, { 46, "HihatOpen" }, { 44, "HihatPedal" },
            { 48, "Tom1Head" }, { 50, "Tom1Rim" },
            { 45, "Tom2Head" }, { 47, "Tom2Rim" },
            { 43, "FTomHead" }, { 41, "FTomRim" },
            { 49, "Crash1Bow" }, { 55, "Crash1Edge" },
            { 57, "Crash2Bow" }, { 52, "Crash2Edge" },
            { 51, "RideBow" }, { 53, "RideBell" }, { 59, "RideEdge" } } },

        { "Alesis (Strike / Nitro / Surge)", {
            { 36, "Kick" },
            { 38, "SnareHead" }, { 40, "Rimshot" }, { 37, "CrossStick" },
            { 42, "HihatClosed" }, { 23, "HihatSemiOpen" }, { 46, "HihatOpen" }, { 44, "HihatPedal" },
            { 48, "Tom1Head" }, { 60, "Tom1Rim" },
            { 45, "Tom2Head" }, { 62, "Tom2Rim" },
            { 43, "FTomHead" }, { 64, "FTomRim" },
            { 49, "Crash1Bow" }, { 55, "Crash1Edge" },
            { 57, "Crash2Bow" }, { 52, "Crash2Edge" },
            { 51, "RideBow" }, { 53, "RideBell" }, { 59, "RideEdge" } } },

        { "Millenium / 2box", {
            { 36, "Kick" },
            { 38, "SnareHead" }, { 40, "Rimshot" }, { 37, "CrossStick" },
            { 42, "HihatClosed" }, { 46, "HihatOpen" }, { 44, "HihatPedal" },
            { 48, "Tom1Head" }, { 50, "Tom1Rim" },
            { 45, "Tom2Head" }, { 47, "Tom2Rim" },
            { 43, "FTomHead" }, { 41, "FTomRim" },
            { 49, "Crash1Bow" }, { 55, "Crash1Edge" },
            { 57, "Crash2Bow" },
            { 51, "RideBow" }, { 53, "RideBell" } } },

        { "EFNote (3 / 5 / 7)", {
            { 36, "Kick" },
            { 38, "SnareHead" }, { 40, "Rimshot" }, { 37, "CrossStick" },
            { 42, "HihatClosed" }, { 46, "HihatOpen" }, { 44, "HihatPedal" },
            { 48, "Tom1Head" }, { 45, "Tom2Head" }, { 43, "FTomHead" },
            { 49, "Crash1Bow" }, { 55, "Crash1Edge" },
            { 57, "Crash2Bow" }, { 52, "Crash2Edge" },
            { 51, "RideBow" }, { 53, "RideBell" }, { 59, "RideEdge" } } },

        { "Ableton Push", {
            { 36, "Kick" }, { 37, "Clap" }, { 38, "Snare" }, { 42, "HihatClosed" },
            { 46, "HihatOpen" }, { 45, "Tom1" }, { 47, "Tom2" }, { 48, "Tom3" },
            { 43, "FloorTom" }, { 49, "Crash" }, { 51, "Ride" } } },
    };

    for (const auto& b : builtIns)
    {
        MapPreset preset;
        preset.name = b.name;
        preset.isBuiltIn = true;

        for (const auto& [note, instrument] : b.notes)
        {
            if (note <= 0 || instrument == nullptr)
                continue;

            preset.noteToInstrumentName[note] = instrument;
        }

        presets.push_back (std::move (preset));
    }

    // The user's own presets, which are just XML in the same directory.
    auto scanDirectory = [&] (const juce::File& dir)
    {
        if (! dir.isDirectory())
            return;

        for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*.xml"))
        {
            MapPreset preset;
            preset.fileName = entry.getFile().getFullPathName();
            preset.name = entry.getFile().getFileNameWithoutExtension();
            preset.isBuiltIn = false;

            if (auto doc = juce::parseXML (entry.getFile()))
                preset.name = doc->getStringAttribute ("name", preset.name);

            presets.push_back (std::move (preset));
        }
    };

    scanDirectory (userPresetDirectory());
    scanDirectory (bundledPresetDirectory());

    return presets;
}

bool MidiMap::loadPresetFile (const juce::File& file, MapPreset& out, juce::String& error)
{
    auto root = juce::parseXML (file);

    if (root == nullptr || root->getTagName() != "glorpMapPreset")
    {
        error = file.getFileName() + " is not a GlorpDrums MIDI map preset";
        return false;
    }

    out = MapPreset {};
    out.name = root->getStringAttribute ("name", file.getFileNameWithoutExtension());
    out.fileName = file.getFullPathName();
    out.isBuiltIn = false;

    if (auto* notes = root->getChildByName ("notes"))
    {
        for (auto* m : notes->getChildIterator())
        {
            const auto note = m->getIntAttribute ("note", -1);

            if (note < 0 || note > 127)
                continue;

            if (auto name = m->getStringAttribute ("name"); name.isNotEmpty())
                out.noteToInstrumentName[note] = name;

            const auto instrument = m->getIntAttribute ("instrument", -1);

            if (instrument >= 0)
                out.noteToInstrument[note] = instrument;
        }
    }

    if (auto* learn = root->getChildByName ("learn"))
    {
        for (auto* m : learn->getChildIterator())
        {
            MidiBinding binding;
            binding.controlId = m->getStringAttribute ("control").toStdString();
            binding.cc = m->getIntAttribute ("cc", -1);
            binding.channel = m->getIntAttribute ("channel", 0);
            binding.useNote = m->getBoolAttribute ("useNote", false);

            if (binding.isValid())
                out.bindings.push_back (std::move (binding));
        }
    }

    return true;
}

void MidiMap::savePresetFile (const juce::File& file, const juce::String& name,
                              const std::vector<juce::String>& instrumentNames) const
{
    juce::XmlElement root ("glorpMapPreset");
    root.setAttribute ("name", name);
    root.setAttribute ("version", 1);

    auto* notes = root.createNewChildElement ("notes");

    for (const auto& [note, instrumentIndex] : noteToInstrument)
    {
        auto* m = notes->createNewChildElement ("map");
        m->setAttribute ("note", note);
        m->setAttribute ("instrument", instrumentIndex);

        // Name as well as index, so the preset survives being applied to a
        // different kit with a different instrument order.
        if (instrumentIndex >= 0 && instrumentIndex < static_cast<int> (instrumentNames.size()))
            m->setAttribute ("name", instrumentNames[static_cast<size_t> (instrumentIndex)]);
    }

    auto* learn = root.createNewChildElement ("learn");

    for (const auto& b : bindings_)
    {
        auto* m = learn->createNewChildElement ("bind");
        m->setAttribute ("control", b.controlId);
        m->setAttribute ("cc", b.cc);
        m->setAttribute ("channel", b.channel);
        m->setAttribute ("useNote", b.useNote);
    }

    file.getParentDirectory().createDirectory();
    root.writeTo (file);
}

void MidiMap::applyPreset (const MapPreset& preset, const std::vector<juce::String>& kitInstrumentNames)
{
    clearAssignments();
    clearBindings();
    bindings_ = preset.bindings;

    // Smart instrument name matching so presets resolve correctly against any kit
    auto matchInstrument = [&] (const juce::String& target) -> int
    {
        const auto lower = target.toLowerCase();

        // 1. Exact match (case-insensitive)
        for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            if (kitInstrumentNames[i].equalsIgnoreCase (target))
                return static_cast<int> (i);

        // 2. Multi-zone snare pieces
        if (lower == "snarehead" || lower == "snare")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if (n == "snarehead" || n == "snare" || (n.contains ("snare") && ! n.contains ("rim") && ! n.contains ("click") && ! n.contains ("rest")))
                    return static_cast<int> (i);
            }
        }
        else if (lower == "rimshot" || lower == "snarerimshot")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
                if (kitInstrumentNames[i].toLowerCase().contains ("rimshot"))
                    return static_cast<int> (i);
        }
        else if (lower == "crossstick" || lower == "rimclick" || lower == "sidestick")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if (n.contains ("click") || n.contains ("cross") || n.contains ("stick"))
                    return static_cast<int> (i);
            }
        }

        // 3. Kick
        else if (lower == "kick" || lower == "kick2")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if (n.contains ("kick") || n.contains ("kdrum") || n.contains ("bdrum") || n.contains ("bd"))
                    return static_cast<int> (i);
            }
        }

        // 4. Hi-Hat
        else if (lower.contains ("closed") || lower.contains ("close"))
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("hat") || n.contains ("hh")) && (n.contains ("closed") || n.contains ("close")))
                    return static_cast<int> (i);
            }
        }
        else if (lower.contains ("semi"))
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("hat") || n.contains ("hh")) && n.contains ("semi"))
                    return static_cast<int> (i);
            }
        }
        else if (lower.contains ("open"))
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("hat") || n.contains ("hh")) && n.contains ("open") && ! n.contains ("semi"))
                    return static_cast<int> (i);
            }
        }
        else if (lower.contains ("pedal") || lower.contains ("foot"))
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("hat") || n.contains ("hh")) && (n.contains ("pedal") || n.contains ("foot")))
                    return static_cast<int> (i);
            }
        }

        // 5. Toms
        else if (lower == "tom1head" || lower == "tom1")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("tom1") || n.contains ("rack1") || n.contains ("hightom")) && ! n.contains ("rim"))
                    return static_cast<int> (i);
            }
        }
        else if (lower == "tom1rim")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("tom1") || n.contains ("rack1")) && n.contains ("rim"))
                    return static_cast<int> (i);
            }
        }
        else if (lower == "tom2head" || lower == "tom2")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("tom2") || n.contains ("rack2") || n.contains ("midtom")) && ! n.contains ("rim"))
                    return static_cast<int> (i);
            }
        }
        else if (lower == "tom2rim")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("tom2") || n.contains ("rack2")) && n.contains ("rim"))
                    return static_cast<int> (i);
            }
        }
        else if (lower.contains ("ftomhead") || lower.contains ("floortom") || lower == "ftom" || lower == "tom3")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("floor") || n.contains ("ftom") || n.contains ("lowtom") || n.contains ("tom3")) && ! n.contains ("rim"))
                    return static_cast<int> (i);
            }
        }
        else if (lower.contains ("ftomrim") || lower.contains ("floortomrim"))
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if ((n.contains ("floor") || n.contains ("ftom")) && n.contains ("rim"))
                    return static_cast<int> (i);
            }
        }

        // 6. Cymbals
        else if (lower == "ridebell")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if (n.contains ("ride") && n.contains ("bell"))
                    return static_cast<int> (i);
            }
        }
        else if (lower == "rideedge")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if (n.contains ("ride") && n.contains ("edge"))
                    return static_cast<int> (i);
            }
        }
        else if (lower == "ridebow" || lower == "ride")
        {
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if (n.contains ("ride") && ! n.contains ("bell") && ! n.contains ("edge") && ! n.contains ("choke"))
                    return static_cast<int> (i);
            }
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
                if (kitInstrumentNames[i].toLowerCase().contains ("ride"))
                    return static_cast<int> (i);
        }
        else if (lower.startsWith ("crash1") || lower == "crash")
        {
            const bool wantsEdge = lower.contains ("edge");
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if (n.contains ("crash1") || (n.contains ("crash") && ! n.contains ("2") && ! n.contains ("r")))
                {
                    if (wantsEdge && n.contains ("edge")) return static_cast<int> (i);
                    if (! wantsEdge && (n.contains ("bow") || (! n.contains ("edge") && ! n.contains ("choke")))) return static_cast<int> (i);
                }
            }
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
                if (kitInstrumentNames[i].toLowerCase().contains ("crash"))
                    return static_cast<int> (i);
        }
        else if (lower.startsWith ("crash2"))
        {
            const bool wantsEdge = lower.contains ("edge");
            for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            {
                const auto n = kitInstrumentNames[i].toLowerCase();
                if (n.contains ("crash2") || n.contains ("crashr"))
                {
                    if (wantsEdge && n.contains ("edge")) return static_cast<int> (i);
                    if (! wantsEdge && (n.contains ("bow") || (! n.contains ("edge") && ! n.contains ("choke")))) return static_cast<int> (i);
                }
            }
        }

        // Substring fallback
        for (size_t i = 0; i < kitInstrumentNames.size(); ++i)
            if (kitInstrumentNames[i].toLowerCase().contains (lower))
                return static_cast<int> (i);

        return -1;
    };

    for (const auto& [note, instrumentName] : preset.noteToInstrumentName)
    {
        const int instIdx = matchInstrument (instrumentName);
        if (instIdx >= 0)
            noteToInstrument[note] = instIdx;
    }

    for (const auto& [note, instrumentIndex] : preset.noteToInstrument)
    {
        if (noteToInstrument.count (note) > 0)
            continue;

        if (instrumentIndex >= 0 && instrumentIndex < static_cast<int> (kitInstrumentNames.size()))
            noteToInstrument[note] = instrumentIndex;
    }

    // Keep the reverse index consistent with whatever survived.
    instrumentToNote.clear();

    for (const auto& [note, instrument] : noteToInstrument)
        instrumentToNote[instrument] = note;

    if (onChanged)
        onChanged();
}

juce::XmlElement* MidiMap::exportXml() const
{
    auto* root = new juce::XmlElement ("MidiMap");

    auto* notes = root->createNewChildElement ("notes");

    for (const auto& [note, instrument] : noteToInstrument)
    {
        auto* m = notes->createNewChildElement ("map");
        m->setAttribute ("note", note);
        m->setAttribute ("instrument", instrument);
    }

    auto* learn = root->createNewChildElement ("learn");

    for (const auto& b : bindings_)
    {
        auto* m = learn->createNewChildElement ("bind");
        m->setAttribute ("control", b.controlId);
        m->setAttribute ("cc", b.cc);
        m->setAttribute ("channel", b.channel);
        m->setAttribute ("useNote", b.useNote);
    }

    return root;
}

void MidiMap::importXml (const juce::XmlElement& xml)
{
    clearAssignments();
    clearBindings();

    if (auto* notes = xml.getChildByName ("notes"))
    {
        for (auto* m : notes->getChildIterator())
        {
            const auto note = m->getIntAttribute ("note", -1);
            const auto instrument = m->getIntAttribute ("instrument", -1);

            if (note >= 0 && note < 128 && instrument >= 0)
                noteToInstrument[note] = instrument;
        }
    }

    if (auto* learn = xml.getChildByName ("learn"))
    {
        for (auto* m : learn->getChildIterator())
        {
            MidiBinding binding;
            binding.controlId = m->getStringAttribute ("control").toStdString();
            binding.cc = m->getIntAttribute ("cc", -1);
            binding.channel = m->getIntAttribute ("channel", 0);
            binding.useNote = m->getBoolAttribute ("useNote", false);

            if (binding.isValid())
                bindings_.push_back (std::move (binding));
        }
    }

    instrumentToNote.clear();

    for (const auto& [note, instrument] : noteToInstrument)
        instrumentToNote[instrument] = note;

    if (onChanged)
        onChanged();
}

} // namespace glorp
