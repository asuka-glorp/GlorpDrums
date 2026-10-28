/*
    ParameterManager.cpp - Plugin parameter layout and MIDI learn queue.
*/

#include "ParameterManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace glorp
{

namespace
{
    constexpr int kVersionTag = 1;
}

//==============================================================================
ParameterManager::ParameterManager()
{
    instrumentLevelParams.reserve (config::MAX_SUPPORTED_INSTRUMENTS);
    instrumentPanParams.reserve (config::MAX_SUPPORTED_INSTRUMENTS);
    instrumentPitchParams.reserve (config::MAX_SUPPORTED_INSTRUMENTS);
    instrumentDynamicsParams.reserve (config::MAX_SUPPORTED_INSTRUMENTS);
    instrumentBusParams.reserve (config::MAX_SUPPORTED_INSTRUMENTS);
}

//==============================================================================
void ParameterManager::createParameterLayout (juce::AudioProcessor& processor)
{
    auto makeFloat = [&processor] (const juce::String& id, const juce::String& name,
                                    juce::NormalisableRange<float> range, float defaultValue)
    {
        auto parameter = std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (id, kVersionTag), name, range, defaultValue);

        auto* raw = parameter.get();
        processor.addParameter (parameter.release());
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

    // Per-instrument parameters exist for up to maxSupportedInstruments
    for (int i = 0; i < config::MAX_SUPPORTED_INSTRUMENTS; ++i)
    {
        const auto prefix = "inst" + juce::String (i) + ".";
        const auto label = "Instrument " + juce::String (i + 1) + " ";

        instrumentLevelParams.push_back (makeFloat (prefix + "level", label + "Level",
                                                    juce::NormalisableRange<float> (0.0f, 2.0f, 0.001f), 1.0f));
        instrumentPanParams.push_back (makeFloat (prefix + "pan", label + "Pan",
                                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        instrumentPitchParams.push_back (makeFloat (prefix + "pitch", label + "Pitch",
                                                    juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));
        instrumentDynamicsParams.push_back (makeFloat (prefix + "dynamics", label + "Dynamics",
                                                       juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

        juce::StringArray busChoices;
        for (int b = 0; b < config::MAX_OUTPUT_BUSES; ++b)
            busChoices.add ("Out " + juce::String (b * 2 + 1) + "/" + juce::String (b * 2 + 2));

        auto busParam = std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "bus", kVersionTag), label + "Output Bus", busChoices, std::min (i, config::MAX_OUTPUT_BUSES - 1));
        instrumentBusParams.push_back (busParam.get());
        processor.addParameter (busParam.release());
    }
}

//==============================================================================
juce::AudioProcessorParameter* ParameterManager::findParameter (const juce::String& paramID) const
{
    // This needs to be called with access to the AudioProcessor's parameters
    // The actual implementation is in PluginProcessor::findParameter which has access to getParameters()
    // We keep this as a stub for interface consistency
    return nullptr;
}

juce::AudioProcessorParameter* ParameterManager::getParameterForMidiControl (int cc, int channel) const
{
    juce::ignoreUnused (cc, channel);
    return nullptr;
}

//==============================================================================
juce::String ParameterManager::controlIdForInstrument (const juce::String& suffix, int index)
{
    return "inst" + juce::String (index) + "." + suffix;
}

bool ParameterManager::parseControlId (const juce::String& controlId, int& instrumentIndex, juce::String& field) const
{
    const auto dot = controlId.indexOfChar ('.');

    if (dot <= 0)
        return false;

    const auto prefix = controlId.substring (0, dot);

    if (! prefix.startsWith ("inst"))
        return false;

    instrumentIndex = prefix.substring (4).getIntValue();
    field = controlId.substring (dot + 1);

    return instrumentIndex >= 0 && instrumentIndex < config::MAX_SUPPORTED_INSTRUMENTS && field.isNotEmpty();
}

//==============================================================================
bool ParameterManager::isMasterParameter (const juce::String& controlId) const
{
    return controlId == "masterLevel" || controlId == "masterPan"
        || controlId == "attack" || controlId == "decay"
        || controlId == "humanize" || controlId == "velocityCurve";
}

void ParameterManager::setMasterParameterValue (const juce::String& controlId, float normalised)
{
    normalised = std::clamp (normalised, 0.0f, 1.0f);

    if (controlId == "masterLevel") { if (masterLevelParam) masterLevelParam->setValueNotifyingHost (normalised); return; }
    if (controlId == "masterPan")   { if (masterPanParam)   masterPanParam->setValueNotifyingHost (normalised);   return; }
    if (controlId == "attack")      { if (attackParam)      attackParam->setValueNotifyingHost (normalised);      return; }
    if (controlId == "decay")       { if (decayParam)       decayParam->setValueNotifyingHost (normalised);       return; }
    if (controlId == "humanize")    { if (humanizeParam)    humanizeParam->setValueNotifyingHost (normalised);    return; }
    if (controlId == "velocityCurve") { if (velCurveParam) velCurveParam->setValueNotifyingHost (normalised);   return; }
}

void ParameterManager::setInstrumentParameterValue (int instrumentIndex, const juce::String& field, float normalised)
{
    normalised = std::clamp (normalised, 0.0f, 1.0f);

    if (field == "level")
    {
        if (instrumentIndex < static_cast<int> (instrumentLevelParams.size()))
            instrumentLevelParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (normalised);
    }
    else if (field == "pan")
    {
        if (instrumentIndex < static_cast<int> (instrumentPanParams.size()))
            instrumentPanParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (normalised);
    }
    else if (field == "pitch")
    {
        if (instrumentIndex < static_cast<int> (instrumentPitchParams.size()))
            instrumentPitchParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (normalised);
    }
    else if (field == "dynamics")
    {
        if (instrumentIndex < static_cast<int> (instrumentDynamicsParams.size()))
            instrumentDynamicsParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (normalised);
    }
    else if (field == "bus")
    {
        if (instrumentIndex < static_cast<int> (instrumentBusParams.size()))
            instrumentBusParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (normalised);
    }
}

//==============================================================================
// MIDI learn queue (audio thread -> message thread)

void ParameterManager::enqueueControlValue (const juce::String& controlId, float normalised)
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

bool ParameterManager::dequeueControlValue (juce::String& controlId, float& normalised)
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

void ParameterManager::drainControlQueue()
{
    juce::String id;
    float value = 0.0f;

    while (dequeueControlValue (id, value))
    {
        if (isMasterParameter (id))
        {
            setMasterParameterValue (id, value);
        }
        else
        {
            int index = 0;
            juce::String field;
            if (parseControlId (id, index, field))
                setInstrumentParameterValue (index, field, value);
        }
    }
}

} // namespace glorp