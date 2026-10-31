/*
    PluginProcessor.cpp - JUCE plugin bridge using ParameterManager.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KitScanner.h"

namespace glorp
{

//==============================================================================
juce::AudioProcessor::BusesProperties PluginProcessor::createBusesProperties()
{
    BusesProperties bp;
    for (int i = 0; i < maxOutputBuses; ++i)
    {
        const auto busName = (i == 0) ? juce::String ("Out 1/2 (Main)")
                                      : ("Out " + juce::String (i * 2 + 1) + "/" + juce::String (i * 2 + 2));
        bp = bp.withOutput (busName, juce::AudioChannelSet::stereo(), true);
    }
    return bp;
}

//==============================================================================
PluginProcessor::PluginProcessor()
    : juce::AudioProcessor (createBusesProperties())
{
    parameterManager.createParameterLayout (*this);

    midiRouter.onMidiMapChanged = [this]
    {
        listeners.call (&Listener::parametersChanged);
    };

    audioEngine.getSampler().setSampleRateAndBlock (44100.0, 512);
    audioEngine.getSampler().setMultiOutMode (true);
    refreshKitList();

    if (settings.lastKitPath.isNotEmpty() && juce::File (settings.lastKitPath).existsAsFile())
    {
        loadKit (settings.lastKitPath);
    }
    else if (! kitList.empty())
    {
        for (const auto& entry : kitList)
        {
            if (entry.valid)
            {
                loadKit (juce::String (entry.drumkitPath));
                break;
            }
        }
    }

    if (! audioEngine.getSampler().isKitLoaded())
    {
        const auto homeDemo = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                  .getChildFile (".config/GlorpDrums/kits/GlorpDemoKit/drumkit.xml");
        if (homeDemo.existsAsFile())
        {
            loadKit (homeDemo.getFullPathName());
        }
        else
        {
            const auto localDemo = juce::File::getCurrentWorkingDirectory()
                                       .getChildFile ("test/kits/GlorpDemoKit/drumkit.xml");
            if (localDemo.existsAsFile())
                loadKit (localDemo.getFullPathName());
        }
    }
}

PluginProcessor::~PluginProcessor()
{
    settings.saveIfChanged();
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    audioEngine.prepareToPlay (sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources()
{
    audioEngine.releaseResources();
}

void PluginProcessor::sampleRateChanged (double newRate)
{
    audioEngine.sampleRateChanged (newRate);
}

void PluginProcessor::blockSizeChanged (int)
{
    audioEngine.blockSizeChanged (0); // blockSize parameter not used in current implementation
}

double PluginProcessor::getTailLengthSeconds() const
{
    // Long enough for a ring-out to finish once the tuner has flushed.
    return 4.0;
}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // GlorpDrums has no audio inputs
    if (! layouts.getMainInputChannelSet().isDisabled())
        return false;

    // Main output bus (Bus 1) must be stereo
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Additional output buses (Buses 2..16) must be stereo or disabled
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& set = layouts.outputBuses[i];
        if (! set.isDisabled() && set != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

//==============================================================================
void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    // Map host output buses to up to 16 stereo bus write pointers (32 channels)
    std::array<float*, maxOutputBuses * 2> busOutputs {};
    busOutputs.fill (nullptr);

    const int busCount = getBusCount (false);
    for (int b = 0; b < maxOutputBuses && b < busCount; ++b)
    {
        auto busBuffer = getBusBuffer (buffer, false, b);
        if (busBuffer.getNumChannels() >= 2)
        {
            busOutputs[static_cast<size_t> (b * 2)]     = busBuffer.getWritePointer (0);
            busOutputs[static_cast<size_t> (b * 2 + 1)] = busBuffer.getWritePointer (1);
        }
    }

    // Fallback for flat buffers or unit tests where buses aren't configured in the host
    if (busOutputs[0] == nullptr && buffer.getNumChannels() >= 2)
    {
        const int available = std::min (buffer.getNumChannels(), maxOutputBuses * 2);
        for (int c = 0; c < available; ++c)
            busOutputs[static_cast<size_t> (c)] = buffer.getWritePointer (c);
    }

    // Delegate core audio processing to AudioEngine (parameters, MIDI, sampler rendering)
    audioEngine.render (busOutputs.data(), maxOutputBuses * 2, numSamples, midi,
                        parameterManager, midiRouter, lastHiHatCC4, lastSnareCC16);

    // Update latency if needed
    const int samplerLatency = audioEngine.getSampler().latencySamples();
    if (getLatencySamples() != samplerLatency)
        setLatencySamples (samplerLatency);

    // If recording drums only (default), stream rendered drum audio now (Main mix)
    if (audioEngine.getAudioRecorder().isRecording() && audioEngine.getAudioRecorder().getSource() == AudioRecorder::RecordingSource::drumsOnly)
        audioEngine.getAudioRecorder().pushAudio (busOutputs.data(), 2, numSamples);

    // Play backing track (mixes into Main Out 1/2)
    audioEngine.getBackingTrackPlayer().render (busOutputs.data(), 2, numSamples);

    // If recording full mix, stream combined drums + backing track (WITHOUT metronome)
    if (audioEngine.getAudioRecorder().isRecording() && audioEngine.getAudioRecorder().getSource() == AudioRecorder::RecordingSource::fullMix)
        audioEngine.getAudioRecorder().pushAudio (busOutputs.data(), 2, numSamples);

    // Play metronome / count-in clicks for the player's monitor output ONLY (Main Out 1/2)
    audioEngine.getMetronome().render (busOutputs.data(), 2, numSamples,
                      audioEngine.getBackingTrackPlayer().getPlayheadSeconds(), audioEngine.getBackingTrackPlayer().isPlaying());
}

//==============================================================================
juce::String PluginProcessor::controlIdForInstrument (const juce::String& suffix, int index)
{
    return ParameterManager::controlIdForInstrument (suffix, index);
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

    currentKit = DrumKit::load (drumkitPath.toStdString());

    if (! currentKit.error.empty())
        lastKitError = juce::String (currentKit.error);
    else if (! currentKit.warnings.empty())
        lastKitError = juce::String (currentKit.warnings.front());

    // Keep the suggested map before the kit is passed to the sampler.
    const auto suggestedMap = currentKit.suggestedMap;

    juce::String error;

    if (! audioEngine.getSampler().loadKit (currentKit, error))
    {
        lastKitError = error;
        audioEngine.getSampler().unloadKit();
        currentKit = {};
        return;
    }

    currentKitPath = drumkitPath;
    settings.pushRecentKit (drumkitPath);
    settings.saveIfChanged();

    // Delegate note mapping to MidiRouter (handles kit midimap, GM fallback, and default assignments)
    midiRouter.setupNoteMappingFromKit (currentKit, audioEngine.getSampler());

    // Reset per-instrument controls for the new kit.
    audioEngine.getSampler().resetInstrumentParams();

    for (int i = 0; i < maxSupportedInstruments; ++i)
    {
        float levelNorm = 0.5f; // default 1.0f (0.5 * 2.0f)
        float panNorm = 0.5f;   // default 0.0f (0.5 * 2.0f - 1.0f)
        int busIdx = (i < audioEngine.getSampler().numInstruments()) ? audioEngine.getSampler().getInstrumentBus (i) : std::min (i, 15);

        if (currentKit.isNativeGlorpKit && i < static_cast<int> (currentKit.instruments.size()))
        {
            const auto& inst = currentKit.instruments[static_cast<size_t> (i)];
            busIdx = std::clamp (inst.outputBus, 0, 15);

            auto itL = currentKit.factoryInstrumentLevels.find (inst.logicalName);
            if (itL == currentKit.factoryInstrumentLevels.end())
                itL = currentKit.factoryInstrumentLevels.find (inst.name);
            if (itL != currentKit.factoryInstrumentLevels.end())
                levelNorm = std::clamp (itL->second / 2.0f, 0.0f, 1.0f);

            auto itP = currentKit.factoryInstrumentPans.find (inst.logicalName);
            if (itP == currentKit.factoryInstrumentPans.end())
                itP = currentKit.factoryInstrumentPans.find (inst.name);
            if (itP != currentKit.factoryInstrumentPans.end())
                panNorm = std::clamp ((itP->second + 1.0f) * 0.5f, 0.0f, 1.0f);
        }

        if (i < static_cast<int> (parameterManager.instrumentLevelParams.size()))
            parameterManager.instrumentLevelParams[static_cast<size_t> (i)]->setValueNotifyingHost (levelNorm);
        if (i < static_cast<int> (parameterManager.instrumentPanParams.size()))
            parameterManager.instrumentPanParams[static_cast<size_t> (i)]->setValueNotifyingHost (panNorm);
        if (i < static_cast<int> (parameterManager.instrumentPitchParams.size()))
            parameterManager.instrumentPitchParams[static_cast<size_t> (i)]->setValueNotifyingHost (0.5f);
        if (i < static_cast<int> (parameterManager.instrumentDynamicsParams.size()))
            parameterManager.instrumentDynamicsParams[static_cast<size_t> (i)]->setValueNotifyingHost (0.5f);
        if (i < static_cast<int> (parameterManager.instrumentBusParams.size()))
            parameterManager.instrumentBusParams[static_cast<size_t> (i)]->setValueNotifyingHost (busIdx / 15.0f);
    }

    listeners.call (&Listener::kitChanged);
}

void PluginProcessor::applyMapPreset (const juce::String& presetName)
{
    midiRouter.applyMapPreset (presetName, audioEngine.getSampler());

    settings.lastMapPreset = presetName;
    settings.saveIfChanged();
}

void PluginProcessor::notifyParametersChanged()
{
    listeners.call (&Listener::parametersChanged);
}

//==============================================================================
void PluginProcessor::setControlValue (const juce::String& controlId, float normalised)
{
    parameterManager.setMasterParameterValue (controlId, normalised);

    int index = 0;
    juce::String field;

    if (parameterManager.parseControlId (controlId, index, field))
        parameterManager.setInstrumentParameterValue (index, field, normalised);
}

void PluginProcessor::drainControlQueue()
{
    parameterManager.drainControlQueue();
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    juce::XmlElement state ("GlorpDrumsState");
    state.setAttribute ("version", 1);
    state.setAttribute ("kit", currentKitPath);
    state.setAttribute ("hiHatMode", static_cast<int> (audioEngine.getSampler().getHiHatMode()));

    const auto mixerPreset = audioEngine.getSampler().getCurrentMixerPreset ("CurrentMix");
    if (auto mixerXml = juce::XmlDocument::parse (mixerPreset.toXmlString()))
    {
        mixerXml->setTagName ("MixerState");
        state.addChildElement (mixerXml.release());
    }

    audioEngine.getSampler().getBleedMatrix().writeXml (state);

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

    if (auto* map = getMidiMap().exportXml())
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

    if (xml->hasAttribute ("hiHatMode"))
    {
        const int modeInt = xml->getIntAttribute ("hiHatMode", 0);
        audioEngine.getSampler().setHiHatMode (modeInt == 1 ? HiHatMode::discreteNotes : HiHatMode::continuousCC4);
    }

    if (auto* mixerXml = xml->getChildByName ("MixerState"))
    {
        MixerPreset p;
        if (MixerPreset::fromXmlString (mixerXml->toString(), p))
            audioEngine.getSampler().applyMixerPreset (p);
    }

    audioEngine.getSampler().getBleedMatrix().readXml (*xml);

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
        getMidiMap().importXml (*map);

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