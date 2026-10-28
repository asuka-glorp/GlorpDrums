/*
    MidiRouter.cpp - MIDI note/CC routing, MIDI learn integration, and E-drum handling.
*/

#include "MidiRouter.h"

#include <algorithm>

namespace glorp
{

//==============================================================================
void MidiRouter::setMidiMap (MidiMap* midiMap) noexcept
{
    midiMap_ = midiMap;
}

void MidiRouter::setSampler (Sampler* sampler) noexcept
{
    sampler_ = sampler;
}

void MidiRouter::setControlQueueCallback (std::function<void (const juce::String&, float)> callback)
{
    controlQueueCallback_ = std::move (callback);
}

//==============================================================================
bool MidiRouter::processMessage (const juce::MidiMessage& message, int sampleOffset)
{
    if (message.isNoteOn())
    {
        if (message.getVelocity() <= 0)
            return routeNoteOff (message); // Note-on with vel 0 = note-off

        return routeNote (message, sampleOffset);
    }

    if (message.isNoteOff())
        return routeNoteOff (message);

    if (message.isController())
        return routeControlChange (message);

    if (message.isAftertouch())
    {
        handlePolyphonicAftertouch (message, sampleOffset);
        return true;
    }

    if (message.isChannelPressure())
    {
        handleChannelPressure (message, sampleOffset);
        return true;
    }

    if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        if (sampler_)
            sampler_->allNotesOff();
        return true;
    }

    return false;
}

//==============================================================================
bool MidiRouter::routeNote (const juce::MidiMessage& message, int sampleOffset)
{
    if (! midiMap_)
        return false;

    // MIDI learn has first refusal on every message
    juce::String learnedControl;
    bool consumedByLearn = false;

    if (midiMap_->isLearning() && midiMap_->offerMessage (message, &learnedControl))
    {
        consumedByLearn = true;

        // Learning a note on an instrument's callout also makes that note
        // trigger the instrument, which is what the user actually meant.
        if (learnedControl.startsWith ("note"))
        {
            const auto index = learnedControl.substring (4).getIntValue();

            if (index >= 0 && sampler_ && index < sampler_->numInstruments())
                midiMap_->assignNote (message.getNoteNumber(), index);

            consumedByLearn = false; // let it sound as well as being learned
        }
    }

    if (consumedByLearn)
        return true;

    if (! midiMap_->isLearning())
    {
        const auto control = midiMap_->controlForMessage (
            message.getNoteNumber(), true, message.getChannel() + 1);

        if (control.isNotEmpty())
        {
            // Queue CC value for the message thread to apply to host parameter
            if (controlQueueCallback_)
                controlQueueCallback_ (control, static_cast<float> (message.getControllerValue()) / 127.0f);

            return true;
        }
    }

    const auto instrument = midiMap_->instrumentForNote (message.getNoteNumber());

    if (instrument < 0)
        return false;

    if (sampler_)
    {
        sampler_->noteOn (instrument, message.getNoteNumber(), message.getFloatVelocity(),
                          sampler_->currentSamplePosition() + sampleOffset);
    }

    return false; // Not consumed by MIDI learn; let it sound
}

bool MidiRouter::routeControlChange (const juce::MidiMessage& message)
{
    if (! midiMap_)
        return false;

    const int cc = message.getControllerNumber();
    const int val = message.getControllerValue();
    const float normalised = static_cast<float> (val) / 127.0f;

    // MIDI learn has first refusal
    if (midiMap_->isLearning())
    {
        juce::String learnedControl;
        if (midiMap_->offerMessage (message, &learnedControl))
            return true;
    }

    if (! midiMap_->isLearning())
    {
        const auto control = midiMap_->controlForMessage (cc, false, message.getChannel() + 1);

        if (control.isNotEmpty())
        {
            enqueueControlValue (control, normalised);
            return true;
        }
    }

    // E-drum continuous controllers
    if (cc == 4) // Hi-Hat pedal
    {
        handleHiHatPedal (val, 0);
        return true;
    }
    else if (cc == 16) // Snare position
    {
        handleSnarePosition (val);
        return true;
    }

    return false;
}

bool MidiRouter::routeNoteOff (const juce::MidiMessage& message)
{
    // Note-Off: acoustic drums and cymbals ring out naturally
    // until natural decay or explicit choke
    // We just ignore note-offs for now (voices decay naturally)
    juce::ignoreUnused (message);
    return false;
}

//==============================================================================
void MidiRouter::handleHiHatPedal (int val, int sampleOffset)
{
    const float pedal = static_cast<float> (val) / 127.0f;

    if (sampler_)
    {
        sampler_->setHiHatPedal (pedal);
    }
}

void MidiRouter::handleSnarePosition (int val)
{
    const float pos = static_cast<float> (val) / 127.0f;

    if (sampler_)
    {
        sampler_->setSnarePosition (pos);
    }
}

void MidiRouter::handlePolyphonicAftertouch (const juce::MidiMessage& message, int sampleOffset)
{
    const int note = message.getNoteNumber();
    const int pressure = message.getAfterTouchValue();

    if (pressure > 0 && sampler_)
    {
        const int inst = midiMap_ ? midiMap_->instrumentForNote (note) : -1;
        if (inst >= 0)
            sampler_->chokeCymbal (inst, 15, sampler_->currentSamplePosition() + sampleOffset);
    }
}

void MidiRouter::handleChannelPressure (const juce::MidiMessage& message, int sampleOffset)
{
    const int pressure = message.getChannelPressureValue();

    if (pressure > 0 && sampler_)
        sampler_->chokeAllCymbals (15, sampler_->currentSamplePosition() + sampleOffset);
}

//==============================================================================
void MidiRouter::enqueueControlValue (const juce::String& controlId, float normalised)
{
    if (controlQueueCallback_)
        controlQueueCallback_ (controlId, normalised);
}

//==============================================================================
void MidiRouter::applyMapPreset (const juce::String& presetName, const std::vector<juce::String>& instrumentNames)
{
    if (! midiMap_)
        return;

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
                // Error handling would go here
                return;
            }
        }

        midiMap_->applyPreset (resolved, instrumentNames);
        return;
    }
}

int MidiRouter::instrumentForNote (int note) const noexcept
{
    if (! midiMap_)
        return -1;
    return midiMap_->instrumentForNote (note);
}

int MidiRouter::noteForInstrument (int instrumentIndex) const noexcept
{
    if (! midiMap_)
        return -1;
    return midiMap_->noteForInstrument (instrumentIndex);
}

bool MidiRouter::isLearning() const noexcept
{
    return midiMap_ ? midiMap_->isLearning() : false;
}

void MidiRouter::beginLearn (const juce::String& controlId, bool useNote, int channel)
{
    if (midiMap_)
        midiMap_->beginLearn (controlId.toStdString(), useNote, channel);
}

void MidiRouter::cancelLearn()
{
    if (midiMap_)
        midiMap_->cancelLearn();
}

float MidiRouter::getHiHatPedal() const noexcept
{
    return sampler_ ? sampler_->getHiHatPedal() : 0.0f;
}

float MidiRouter::getSnarePosition() const noexcept
{
    return sampler_ ? sampler_->getSnarePosition() : 0.5f;
}

void MidiRouter::setHiHatMode (Sampler::HiHatMode mode) noexcept
{
    if (sampler_)
        sampler_->setHiHatMode (mode);
}

Sampler::HiHatMode MidiRouter::getHiHatMode() const noexcept
{
    return sampler_ ? sampler_->getHiHatMode() : Sampler::HiHatMode::continuousCC4;
}

} // namespace glorp