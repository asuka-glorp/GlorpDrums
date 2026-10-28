/*
    MidiRouter.cpp - MIDI note/CC routing, MIDI learn, and preset management.
*/

#include "MidiRouter.h"

#include "DrumKit.h"
#include "MidiMap.h"
#include "Sampler.h"

#include <juce_core/juce_core.h>

namespace glorp
{

//==============================================================================
MidiRouter::MidiRouter()
{
    midiMap.onChanged = [this]
    {
        if (onMidiMapChanged)
            onMidiMapChanged();
    };
}

//==============================================================================
bool MidiRouter::processMidiMessage (const juce::MidiMessage& message,
                                      int sampleOffset,
                                      Sampler& sampler,
                                      ParameterManager& parameterManager,
                                      std::atomic<float>& lastHiHatCC4,
                                      std::atomic<float>& lastSnareCC16)
{
    // Learn has first refusal on every message, so the first note played after
    // arming a callout's LEARN button is the one that gets bound.
    juce::String learnedControl;
    bool consumedByLearn = false;

    if (midiMap.isLearning() && midiMap.offerMessage (message, &learnedControl))
    {
        consumedByLearn = true;

        // Learning a note on an instrument's callout also makes that note
        // trigger the instrument, which is what the user actually meant.
        if (message.isNoteOn() && learnedControl.startsWith ("note"))
        {
            const auto index = learnedControl.substring (4).getIntValue();

            if (index >= 0 && index < sampler.numInstruments())
                midiMap.assignNote (message.getNoteNumber(), index);

            consumedByLearn = false;   // let it sound as well as being learned
        }
    }

    if (consumedByLearn)
        return true;

    if (! midiMap.isLearning())
    {
        const auto control = midiMap.controlForMessage (
            message.isNoteOn() ? message.getNoteNumber() : message.getControllerNumber(),
            message.isNoteOn(), message.getChannel() + 1);

        if (control.isNotEmpty())
        {
            // Notifying the host from here would allocate and post a message,
            // which the audio thread must never do. Queue it and let the
            // message thread apply it a few milliseconds later.
            if (message.isController())
                parameterManager.enqueueControlValue (control, static_cast<float> (message.getControllerValue()) / 127.0f);

            return true;
        }
    }

    // 1. E-drum Continuous Controllers (CC#4 Hi-Hat, CC#16 Snare position)
    if (message.isController())
    {
        const int cc = message.getControllerNumber();
        const int val = message.getControllerValue();

        if (cc == 4) // Hi-Hat pedal
        {
            const float pedal = static_cast<float> (val) / 127.0f;
            sampler.setHiHatPedal (pedal);
            lastHiHatCC4.store (pedal, std::memory_order_relaxed);
        }
        else if (cc == 16) // Snare position
        {
            const float pos = static_cast<float> (val) / 127.0f;
            sampler.setSnarePosition (pos);
            lastSnareCC16.store (pos, std::memory_order_relaxed);
        }
    }

    // 2. E-drum Cymbal Choke via Polyphonic Aftertouch (pressure per note)
    if (message.isAftertouch())
    {
        const int note = message.getNoteNumber();
        const int pressure = message.getAfterTouchValue();

        if (pressure > 0)
        {
            const int inst = midiMap.instrumentForNote (note);
            if (inst >= 0)
                sampler.chokeCymbal (inst, 15, sampler.currentSamplePosition() + sampleOffset);
        }
        return true;
    }

    // 3. E-drum Cymbal Choke via Channel Pressure (monophonic aftertouch)
    if (message.isChannelPressure())
    {
        const int pressure = message.getChannelPressureValue();
        if (pressure > 0)
            sampler.chokeAllCymbals (15, sampler.currentSamplePosition() + sampleOffset);
        return true;
    }

    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        sampler.allNotesOff();
        return true;
    }

    // 4. Note-Off: acoustic drums and cymbals ring out naturally until natural decay or explicit choke
    if (message.isNoteOff())
        return true;

    if (! message.isNoteOn() || message.getVelocity() <= 0)
        return true;

    const auto instrument = midiMap.instrumentForNote (message.getNoteNumber());

    if (instrument < 0)
        return true;

    sampler.noteOn (instrument, message.getNoteNumber(), message.getFloatVelocity(),
                    sampler.currentSamplePosition() + sampleOffset);

    return true;
}

//==============================================================================
void MidiRouter::drainMidiLearnQueue (ParameterManager& parameterManager)
{
    parameterManager.drainControlQueue();
}

//==============================================================================
juce::String MidiRouter::stripName (const juce::String& s)
{
    return s.toLowerCase().removeCharacters (" .-_/\\");
}

void MidiRouter::setupNoteMappingFromKit (const DrumKit& kit, Sampler& sampler)
{
    // Seed the note map from the kit's own midimap, then fall back to General
    // MIDI so something sensible is playable immediately.
    for (const auto& [note, instrumentName] : kit.suggestedMap)
    {
        const auto targetStripped = stripName (instrumentName);
        for (int i = 0; i < sampler.numInstruments(); ++i)
        {
            if (stripName (sampler.instrumentName (i)) == targetStripped)
            {
                midiMap.assignNote (note, i);
                break;
            }
        }
    }

    if (midiMap.noteAssignments().empty())
    {
        for (const auto& preset : MidiMap::availablePresets())
        {
            if (preset.name != "General MIDI")
                continue;

            for (const auto& [note, instrumentName] : preset.noteToInstrumentName)
            {
                const auto targetStripped = stripName (instrumentName);
                for (int i = 0; i < sampler.numInstruments(); ++i)
                {
                    if (stripName (sampler.instrumentName (i)) == targetStripped)
                    {
                        midiMap.assignNote (note, i);
                        break;
                    }
                }
            }

            break;
        }
    }

    // Ensure every instrument in the kit has a playable MIDI note assigned
    for (int i = 0; i < sampler.numInstruments(); ++i)
    {
        if (midiMap.noteForInstrument (i) >= 0)
            continue;

        const auto name = sampler.instrumentName (i).toLowerCase();
        int defaultNote = getDefaultNoteForInstrument (name);

        if (defaultNote >= 0 && midiMap.instrumentForNote (defaultNote) < 0)
        {
            midiMap.assignNote (defaultNote, i);
        }
        else
        {
            for (int note = 35; note <= 81; ++note)
            {
                if (midiMap.instrumentForNote (note) < 0)
                {
                    midiMap.assignNote (note, i);
                    break;
                }
            }
        }
    }
}

int MidiRouter::getDefaultNoteForInstrument (const juce::String& name) const
{
    if (name.contains ("kick") || name.contains ("bdrum") || name.contains ("kdrum") || name.contains ("bd"))
        return name.contains ("2") || name.contains ("l") ? 35 : 36;
    else if (name.contains ("snare") || name.contains ("sd"))
    {
        if (name.contains ("rimshot")) return 40;
        else if (name.contains ("rimclick") || name.contains ("sidestick") || name.contains ("cross")) return 37;
        else if (name.contains ("rim")) return 37;
        else if (name.contains ("edge")) return 39;
        else if (name.contains ("muted") || name.contains ("muff")) return 33;
        else if (name.contains ("fat")) return 29;
        else return 38;
    }
    else if (name.contains ("hat") || name.contains ("hh"))
    {
        if (name.contains ("splash") || name.contains ("spl")) return 63;
        else if (name.contains ("pedal") || name.contains ("foot")) return 44;
        else if (name.contains ("open1") || name.contains ("op1")) return 23;
        else if (name.contains ("open2") || name.contains ("op2")) return 31;
        else if (name.contains ("open3") || name.contains ("op3")) return 34;
        else if (name.contains ("open4") || name.contains ("op4")) return 30;
        else if (name.contains ("semi")) return 80;
        else if (name.contains ("open")) return name.contains ("edge") ? 26 : 46;
        else if (name.contains ("closed") || name.contains ("close")) return name.contains ("edge") ? 22 : 42;
        else return 42;
    }
    else if (name.contains ("tom2") || name.contains ("tom 2") || name.contains ("racktom2") || name.contains ("midtom") || name.contains ("tom13"))
        return name.contains ("rim") ? 60 : 47;
    else if (name.contains ("tom3") || name.contains ("tom 3") || name.contains ("lowtom") || name.contains ("tom14"))
        return name.contains ("rim") ? 62 : 45;
    else if (name.contains ("ftom2") || name.contains ("floor2") || name.contains ("tom18"))
        return name.contains ("rim") ? 65 : 41;
    else if (name.contains ("ftom") || name.contains ("floor") || name.contains ("tom16"))
        return name.contains ("rim") ? 58 : 43;
    else if (name.contains ("tom1") || name.contains ("tom 1") || name.contains ("racktom1") || name.contains ("hightom") || name.contains ("tom10") || name.contains ("tom12") || name.contains ("tom"))
        return name.contains ("rim") ? 50 : 48;
    else if (name.contains ("ride"))
        return name.contains ("bell") ? 53 : (name.contains ("edge") ? 59 : 51);
    else if (name.contains ("splash"))
        return 55;
    else if (name.contains ("china"))
        return 52;
    else if (name.contains ("crash2") || name.contains ("crash 2"))
        return name.contains ("edge") ? 52 : 57;
    else if (name.contains ("crash3") || name.contains ("crash 3"))
        return name.contains ("edge") ? 54 : 56;
    else if (name.contains ("crash"))
        return name.contains ("edge") ? 55 : 49;
    else if (name.contains ("cowbell"))
        return name.contains ("muff") ? 32 : (name.contains ("hi") ? 33 : 31);

    return -1;
}

void MidiRouter::applyMapPreset (const juce::String& presetName, Sampler& sampler)
{
    std::vector<juce::String> names;

    for (int i = 0; i < sampler.numInstruments(); ++i)
        names.push_back (sampler.instrumentName (i));

    for (const auto& preset : MidiMap::availablePresets())
    {
        if (preset.name != presetName)
            continue;

        MapPreset resolved = preset;

        if (! preset.fileName.isEmpty())
        {
            juce::String error;

            if (! MidiMap::loadPresetFile (juce::File (preset.fileName), resolved, error))
            {
                // Error handling would be done by caller
                return;
            }
        }

        midiMap.applyPreset (resolved, names);
        return;
    }
}

std::vector<MapPreset> MidiRouter::getAvailablePresets()
{
    return MidiMap::availablePresets();
}

void MidiRouter::beginMidiLearn (const juce::String& controlId)
{
    midiMap.beginLearn (controlId.toStdString());
}

void MidiRouter::cancelMidiLearn()
{
    midiMap.cancelLearn();
}

} // namespace glorp