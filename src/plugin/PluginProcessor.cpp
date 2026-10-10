#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KitScanner.h"

namespace glorp
{

namespace
{
    constexpr int kVersionTag = 1;

    juce::String pitchToText (float value, int)
    {
        const auto semitones = (value - 0.5f) * 2.0f * PitchShifter::maxSemitones;
        return juce::String (juce::roundToInt (semitones * 100.0f) / 100.0f) + " st";
    }

    juce::String panToText (float value, int)
    {
        const auto pan = (value - 0.5f) * 2.0f;

        if (std::abs (pan) < 0.01f)
            return "centre";

        return juce::String (juce::roundToInt (std::abs (pan) * 100.0f)) + (pan < 0 ? " L" : " R");
    }

    juce::String percentToText (float value, int)
    {
        return juce::String (juce::roundToInt (value * 100.0f)) + "%";
    }

    float knobToDecibels (float normalised)  { return 0.0f - (1.0f - normalised) * 60.0f; }
    float decibelsToKnob (float db)            { return 1.0f - db / -60.0f; }

    std::unique_ptr<juce::AudioBuffer<float>> makeSilentBuffer (int channels, int samples)
    {
        return std::make_unique<juce::AudioBuffer<float>> (std::max (1, channels),
                                                           std::max (1, samples));
    }
}

juce::AudioProcessor::BusesProperties PluginProcessor::createBusesProperties()
{
    BusesProperties bp;
    for (int i = 0; i < GLORP_MAX_INSTRUMENT_BUSES; ++i)
        bp = bp.withOutput ("Bus " + juce::String (i + 1), juce::AudioChannelSet::stereo(), i == 0);
    return bp;
}

//==============================================================================
PluginProcessor::PluginProcessor()
    : juce::AudioProcessor (createBusesProperties())
{
    createParameterLayout();

    midiMap.onChanged = [this]
    {
        listeners.call (&Listener::parametersChanged);
    };

    sampler.setSampleRateAndBlock (44100.0, 512);
    refreshKitList();
}

PluginProcessor::~PluginProcessor()
{
    settings.saveIfChanged();
}

//==============================================================================
void PluginProcessor::createParameterLayout()
{
    auto makeFloat = [this] (const juce::String& id, const juce::String& name,
                             juce::NormalisableRange<float> range, float defaultValue)
    {
        auto parameter = std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (id, kVersionTag), name, range, defaultValue);

        auto* raw = parameter.get();
        addParameter (parameter.release());
        return raw;
    };

    masterLevelParam = makeFloat ("masterLevel", "Master Level",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.8f);
    masterPanParam   = makeFloat ("masterPan", "Master Pan",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f);
    attackParam      = makeFloat ("attack", "Attack",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f);
    decayParam       = makeFloat ("decay", "Decay",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f);
    humanizeParam    = makeFloat ("humanize", "Humanise",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f);
    velCurveParam    = makeFloat ("velocityCurve", "Velocity Curve",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f);

    // Per-instrument parameters exist for every possible bus, whether or not the
    // loaded kit fills them, so that automation and MIDI maps survive a kit change.
    for (int i = 0; i < GLORP_MAX_INSTRUMENT_BUSES; ++i)
    {
        const auto prefix = "inst" + juce::String (i) + ".";
        const auto label = "Instrument " + juce::String (i + 1) + " ";

        instrumentLevelParams.push_back (makeFloat (prefix + "level", label + "Level",
                                                    juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f));
        instrumentPanParams.push_back (makeFloat (prefix + "pan", label + "Pan",
                                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        instrumentPitchParams.push_back (makeFloat (prefix + "pitch", label + "Pitch",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
    }
}

void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampler.setSampleRateAndBlock (sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources()
{
    sampler.allNotesOff();
}

void PluginProcessor::sampleRateChanged (double newRate)
{
    sampler.setSampleRateAndBlock (newRate, 512);
}

void PluginProcessor::blockSizeChanged (int)
{
    // The voice scratch buffers resize on demand, so nothing to do beyond telling
    // the shifter's block hint.
}

double PluginProcessor::getTailLengthSeconds() const
{
    // Long enough for a ring-out to finish once the tuner has flushed.
    return 4.0;
}

//==============================================================================
void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    juce::ignoreUnused (midi);

    // The parameter values are the source of truth, so pull the current snapshot
    // before rendering rather than trying to keep two copies in step.
    GlobalParams globals;
    globals.masterLevel = masterLevelParam != nullptr ? masterLevelParam->get() : 0.8f;
    globals.masterPan = (masterPanParam != nullptr ? masterPanParam->get() : 0.5f) * 2.0f - 1.0f;
    globals.attackMs = (attackParam != nullptr ? attackParam->get() : 0.0f) * 60.0f;
    globals.decayScale = (decayParam != nullptr ? decayParam->get() : 0.5f) * 4.0f;
    globals.humanize = humanizeParam != nullptr ? humanizeParam->get() : 0.0f;
    globals.velCurve = velCurveParam != nullptr ? velCurveParam->get() : 1.0f;
    sampler.setGlobalParams (globals);

    for (int i = 0; i < sampler.numInstruments() && i < static_cast<int> (instrumentLevelParams.size()); ++i)
    {
        auto params = sampler.getInstrumentParams (i);

        params.level = instrumentLevelParams[static_cast<size_t> (i)]->get();
        params.pan = instrumentPanParams[static_cast<size_t> (i)]->get() * 2.0f - 1.0f;
        params.pitchSemitones = (instrumentPitchParams[static_cast<size_t> (i)]->get() - 0.5f)
                              * 2.0f * PitchShifter::maxSemitones;

        sampler.setInstrumentParams (i, params);
    }

    // MIDI first, so hits scheduled inside this block are rendered by it.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto sampleOffset = metadata.samplePosition;

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
            continue;

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
                    enqueueControlValue (control, static_cast<float> (message.getControllerValue()) / 127.0f);

                continue;
            }
        }

        if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            sampler.allNotesOff();
            continue;
        }

        if (! message.isNoteOn() || message.getVelocity() <= 0)
            continue;

        const auto instrument = midiMap.instrumentForNote (message.getNoteNumber());

        if (instrument < 0)
            continue;

        sampler.noteOn (instrument, message.getNoteNumber(), message.getFloatVelocity(),
                        sampler.currentSamplePosition() + sampleOffset);
    }

    // JUCE may hand us fewer channels than the host configured; the sampler only
    // writes to buses that exist, so this is safe.
    std::vector<float*> channelPointers;
    channelPointers.reserve (static_cast<size_t> (numChannels));

    for (int c = 0; c < numChannels; ++c)
        channelPointers.push_back (buffer.getWritePointer (c));

    sampler.render (channelPointers.data(), numChannels, numSamples);
}

//==============================================================================
juce::String PluginProcessor::controlIdForInstrument (const juce::String& suffix, int index)
{
    return "inst" + juce::String (index) + "." + suffix;
}

bool PluginProcessor::parseControlId (const juce::String& controlId, int& instrumentIndex,
                                      juce::String& field) const
{
    const auto dot = controlId.indexOfChar ('.');

    if (dot <= 0)
        return false;

    const auto prefix = controlId.substring (0, dot);

    if (! prefix.startsWith ("inst"))
        return false;

    instrumentIndex = prefix.substring (4).getIntValue();
    field = controlId.substring (dot + 1);

    return instrumentIndex >= 0 && instrumentIndex < GLORP_MAX_INSTRUMENT_BUSES && field.isNotEmpty();
}

void PluginProcessor::applyControlValue (int instrumentIndex, const juce::String& field, float normalised)
{
    auto params = sampler.getInstrumentParams (instrumentIndex);

    if (field == "level")
    {
        params.level = normalised * 2.0f;

        if (instrumentIndex < static_cast<int> (instrumentLevelParams.size()))
            instrumentLevelParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (std::clamp (normalised, 0.0f, 1.0f));
    }
    else if (field == "pan")
    {
        params.pan = normalised * 2.0f - 1.0f;

        if (instrumentIndex < static_cast<int> (instrumentPanParams.size()))
            instrumentPanParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (std::clamp (normalised, 0.0f, 1.0f));
    }
    else if (field == "pitch")
    {
        params.pitchSemitones = (normalised - 0.5f) * 2.0f * PitchShifter::maxSemitones;

        if (instrumentIndex < static_cast<int> (instrumentPitchParams.size()))
            instrumentPitchParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (std::clamp (normalised, 0.0f, 1.0f));
    }
    else
    {
        return;
    }

    sampler.setInstrumentParams (instrumentIndex, params);
}

void PluginProcessor::setControlValue (const juce::String& controlId, float normalised)
{
    normalised = std::clamp (normalised, 0.0f, 1.0f);

    if (controlId == "masterLevel") { if (masterLevelParam) masterLevelParam->setValueNotifyingHost (normalised); return; }
    if (controlId == "masterPan")   { if (masterPanParam)   masterPanParam->setValueNotifyingHost (normalised);   return; }
    if (controlId == "attack")      { if (attackParam)      attackParam->setValueNotifyingHost (normalised);      return; }
    if (controlId == "decay")       { if (decayParam)       decayParam->setValueNotifyingHost (normalised);       return; }
    if (controlId == "humanize")    { if (humanizeParam)    humanizeParam->setValueNotifyingHost (normalised);    return; }
    if (controlId == "velocityCurve") { if (velCurveParam) velCurveParam->setValueNotifyingHost (normalised);   return; }

    int index = 0;
    juce::String field;

    if (parseControlId (controlId, index, field))
        applyControlValue (index, field, normalised);
}

juce::AudioProcessorParameter* PluginProcessor::getParameterForMidiControl (int cc, int channel)
{
    // Only note input for a drum plugin: CCs are routed through the learn map
    // instead, because the same CC may drive several instruments.
    juce::ignoreUnused (cc, channel);
    return nullptr;
}

juce::AudioProcessorParameter* PluginProcessor::findParameter (const juce::String& paramID) const
{
    for (auto* p : getParameters())
        if (auto* pWithId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (pWithId->paramID == paramID)
                return p;

    return nullptr;
}

//==============================================================================
void PluginProcessor::refreshKitList()
{
    kitList = KitScanner::scan (settings.allSearchPaths());
}

void PluginProcessor::loadKit (const juce::String& drumkitPath)
{
    lastKitError.clear();

    if (drumkitPath.isEmpty())
        return;

    auto kit = DrumKit::load (drumkitPath.toStdString());

    if (! kit.error.empty())
        lastKitError = juce::String (kit.error);
    else if (! kit.warnings.empty())
        lastKitError = juce::String (kit.warnings.front());

    // Keep the suggested map before the kit is moved into the sampler.
    const auto suggestedMap = kit.suggestedMap;

    juce::String error;

    if (! sampler.loadKit (std::move (kit), error))
    {
        lastKitError = error;
        sampler.unloadKit();
        return;
    }

    currentKitPath = drumkitPath;
    settings.pushRecentKit (drumkitPath);
    settings.saveIfChanged();

    // Seed the note map from the kit's own midimap, then fall back to General
    // MIDI so something sensible is playable immediately.
    for (const auto& [note, instrumentName] : suggestedMap)
        for (int i = 0; i < sampler.numInstruments(); ++i)
            if (sampler.instrumentName (i).toLowerCase() == juce::String (instrumentName).toLowerCase())
                midiMap.assignNote (note, i);

    if (midiMap.noteAssignments().empty())
    {
        for (const auto& preset : MidiMap::availablePresets())
        {
            if (preset.name != "General MIDI")
                continue;

            for (const auto& [note, instrumentName] : preset.noteToInstrumentName)
                for (int i = 0; i < sampler.numInstruments(); ++i)
                    if (sampler.instrumentName (i).toLowerCase() == instrumentName.toLowerCase())
                        midiMap.assignNote (note, i);

            break;
        }
    }

    // Reset per-instrument controls for the new kit.
    sampler.resetInstrumentParams();

    for (auto* p : instrumentLevelParams) p->setValueNotifyingHost (0.5f);
    for (auto* p : instrumentPanParams)   p->setValueNotifyingHost (0.5f);
    for (auto* p : instrumentPitchParams) p->setValueNotifyingHost (0.5f);

    listeners.call (&Listener::kitChanged);
}

void PluginProcessor::applyMapPreset (const juce::String& presetName)
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
                lastKitError = error;
                return;
            }
        }

        midiMap.applyPreset (resolved, names);
        settings.lastMapPreset = presetName;
        settings.saveIfChanged();
        return;
    }
}

void PluginProcessor::notifyParametersChanged()
{
    listeners.call (&Listener::parametersChanged);
}

//==============================================================================
void PluginProcessor::enqueueControlValue (const juce::String& controlId, float normalised)
{
    const auto write = controlWrite.load (std::memory_order_relaxed);
    const auto read = controlRead.load (std::memory_order_relaxed);

    if (write - read >= controlRingSize)
        return;   // the message thread has fallen far behind; drop rather than block

    PendingControl& slot = controlRing[static_cast<size_t> (write) % controlRingSize];
    slot.id = controlId.toStdString();
    slot.value = normalised;
    controlWrite.store (write + 1, std::memory_order_release);
}

bool PluginProcessor::dequeueControlValue (juce::String& controlId, float& normalised)
{
    const auto read = controlRead.load (std::memory_order_relaxed);
    const auto write = controlWrite.load (std::memory_order_acquire);

    if (read == write)
        return false;

    const PendingControl& slot = controlRing[static_cast<size_t> (read) % controlRingSize];
    controlId = juce::String (slot.id);
    normalised = slot.value;
    controlRead.store (read + 1, std::memory_order_release);
    return true;
}

/// Message thread side of MIDI learn: apply queued CC values to the real
/// parameters, which is what notifies the host and the UI.
void PluginProcessor::drainControlQueue()
{
    juce::String id;
    float value = 0.0f;

    while (dequeueControlValue (id, value))
        setControlValue (id, value);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    juce::XmlElement state ("GlorpDrumsState");
    state.setAttribute ("version", kVersionTag);
    state.setAttribute ("kit", currentKitPath);

    auto* parameters = state.createNewChildElement ("Parameters");

    for (const auto* parameter : getParameters())
    {
        if (auto* pWithId = dynamic_cast<const juce::AudioProcessorParameterWithID*> (parameter))
        {
            auto* p = parameters->createNewChildElement ("P");
            p->setAttribute ("id", pWithId->paramID);
            p->setAttribute ("v", parameter->getValue());
        }
    }

    if (auto* map = midiMap.exportXml())
    {
        map->setTagName ("MidiMap");
        state.addChildElement (map);
    }

    copyXmlToBinary (state, destination);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName ("GlorpDrumsState"))
        return;

    auto kitPath = xml->getStringAttribute ("kit");

    if (auto* parameters = xml->getChildByName ("Parameters"))
    {
        for (auto* p : parameters->getChildIterator())
        {
            const auto id = p->getStringAttribute ("id");

            if (auto* parameter = findParameter (id))
                parameter->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, static_cast<float> (p->getDoubleAttribute ("v", 0.0))));
        }
    }

    if (auto* map = xml->getChildByName ("MidiMap"))
        midiMap.importXml (*map);

    if (kitPath.isNotEmpty() && kitPath != currentKitPath)
    {
        loadKit (kitPath);
    }
    else
    {
        listeners.call (&Listener::kitChanged);
        listeners.call (&Listener::parametersChanged);
    }
}

//==============================================================================
juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

} // namespace glorp

//==============================================================================
// Global filter creator
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new glorp::PluginProcessor();
}
