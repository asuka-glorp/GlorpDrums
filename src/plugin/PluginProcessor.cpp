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
        const auto semitones = (value - 0.5f) * 24.0f; // ±12 semitones
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
    for (int i = 0; i < maxOutputBuses; ++i)
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
    sampler.setMultiOutMode (true);
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

    if (! sampler.isKitLoaded())
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

    // Per-instrument parameters exist for up to maxSupportedInstruments so that
    // any kit (including multi-zone and large kits like CrocellKit) can control
    // every piece independently.
    for (int i = 0; i < maxSupportedInstruments; ++i)
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
        for (int b = 0; b < maxOutputBuses; ++b)
            busChoices.add ("Out " + juce::String (b * 2 + 1) + "/" + juce::String (b * 2 + 2));

        auto busParam = std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID (prefix + "bus", kVersionTag), label + "Output Bus", busChoices, std::min (i, maxOutputBuses - 1));
        instrumentBusParams.push_back (busParam.get());
        addParameter (busParam.release());
    }
}

void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampler.setSampleRateAndBlock (sampleRate, samplesPerBlock);
    backingTrackPlayer.prepareToPlay (sampleRate, samplesPerBlock);
    metronome.prepareToPlay (sampleRate, samplesPerBlock);
    audioRecorder.prepareToPlay (sampleRate, samplesPerBlock);
}

void PluginProcessor::releaseResources()
{
    sampler.allNotesOff();
    metronome.stop();
    backingTrackPlayer.stop();
    audioRecorder.stopRecording();
    midiRecorder.stopRecording();
}

void PluginProcessor::sampleRateChanged (double newRate)
{
    sampler.setSampleRateAndBlock (newRate, 512);
    backingTrackPlayer.prepareToPlay (newRate, 512);
    metronome.prepareToPlay (newRate, 512);
    audioRecorder.prepareToPlay (newRate, 512);
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

    const int samplerLatency = sampler.latencySamples();
    if (getLatencySamples() != samplerLatency)
        setLatencySamples (samplerLatency);

    for (int i = 0; i < sampler.numInstruments() && i < static_cast<int> (instrumentLevelParams.size()); ++i)
    {
        auto params = sampler.getInstrumentParams (i);

        params.level = instrumentLevelParams[static_cast<size_t> (i)]->get();
        params.pan = instrumentPanParams[static_cast<size_t> (i)]->get() * 2.0f - 1.0f;
        params.pitchSemitones = (instrumentPitchParams[static_cast<size_t> (i)]->get() - 0.5f) * 24.0f;

        if (i < static_cast<int> (instrumentDynamicsParams.size()))
            params.dynamics = instrumentDynamicsParams[static_cast<size_t> (i)]->get();

        sampler.setInstrumentParams (i, params);

        if (i < static_cast<int> (instrumentBusParams.size()))
            sampler.setInstrumentBus (i, instrumentBusParams[static_cast<size_t> (i)]->getIndex());
    }

    // MIDI first, so hits scheduled inside this block are rendered by it.
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto sampleOffset = metadata.samplePosition;

        if (midiRecorder.isRecording())
            midiRecorder.logMessage (message, sampler.currentSamplePosition() + sampleOffset);

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
            continue;
        }

        // 3. E-drum Cymbal Choke via Channel Pressure (monophonic aftertouch)
        if (message.isChannelPressure())
        {
            const int pressure = message.getChannelPressureValue();
            if (pressure > 0)
                sampler.chokeAllCymbals (15, sampler.currentSamplePosition() + sampleOffset);
            continue;
        }

        if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            sampler.allNotesOff();
            continue;
        }

        // 4. Note-Off: acoustic drums and cymbals ring out naturally until natural decay or explicit choke
        if (message.isNoteOff())
            continue;

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

    // If recording drums only (default), stream rendered drum audio now
    if (audioRecorder.isRecording() && audioRecorder.getSource() == AudioRecorder::RecordingSource::drumsOnly)
        audioRecorder.pushAudio (channelPointers.data(), numChannels, numSamples);

    // Play backing track (mixes into buffer)
    backingTrackPlayer.render (channelPointers.data(), numChannels, numSamples);

    // If recording full mix, stream combined drums + backing track (WITHOUT metronome)
    if (audioRecorder.isRecording() && audioRecorder.getSource() == AudioRecorder::RecordingSource::fullMix)
        audioRecorder.pushAudio (channelPointers.data(), numChannels, numSamples);

    // Play metronome / count-in clicks for the player's monitor output ONLY (never recorded)
    metronome.render (channelPointers.data(), numChannels, numSamples,
                      backingTrackPlayer.getPlayheadSeconds(), backingTrackPlayer.isPlaying());
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

    return instrumentIndex >= 0 && instrumentIndex < maxSupportedInstruments && field.isNotEmpty();
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
        params.pitchSemitones = (normalised - 0.5f) * 24.0f;

        if (instrumentIndex < static_cast<int> (instrumentPitchParams.size()))
            instrumentPitchParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (std::clamp (normalised, 0.0f, 1.0f));
    }
    else if (field == "dynamics")
    {
        params.dynamics = std::clamp (normalised, 0.0f, 1.0f);

        if (instrumentIndex < static_cast<int> (instrumentDynamicsParams.size()))
            instrumentDynamicsParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (std::clamp (normalised, 0.0f, 1.0f));
    }
    else if (field == "bus")
    {
        const int busIdx = static_cast<int> (normalised * 15.0f + 0.5f);
        if (instrumentIndex < static_cast<int> (instrumentBusParams.size()))
            instrumentBusParams[static_cast<size_t> (instrumentIndex)]
                ->setValueNotifyingHost (busIdx / 15.0f);
        sampler.setInstrumentBus (instrumentIndex, busIdx);
        return;
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

    currentKit = DrumKit::load (drumkitPath.toStdString());

    if (! currentKit.error.empty())
        lastKitError = juce::String (currentKit.error);
    else if (! currentKit.warnings.empty())
        lastKitError = juce::String (currentKit.warnings.front());

    // Keep the suggested map before the kit is passed to the sampler.
    const auto suggestedMap = currentKit.suggestedMap;

    juce::String error;

    if (! sampler.loadKit (currentKit, error))
    {
        lastKitError = error;
        sampler.unloadKit();
        currentKit = {};
        return;
    }

    currentKitPath = drumkitPath;
    settings.pushRecentKit (drumkitPath);
    settings.saveIfChanged();

    const auto stripName = [] (const juce::String& s)
    {
        return s.toLowerCase().removeCharacters (" .-_/\\");
    };

    // Seed the note map from the kit's own midimap, then fall back to General
    // MIDI so something sensible is playable immediately.
    for (const auto& [note, instrumentName] : suggestedMap)
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
        int defaultNote = -1;

        if (name.contains ("kick") || name.contains ("bdrum") || name.contains ("kdrum") || name.contains ("bd"))
            defaultNote = (name.contains ("2") || name.contains ("l")) ? 35 : 36;
        else if (name.contains ("snare") || name.contains ("sd"))
        {
            if (name.contains ("rimshot")) defaultNote = 40;
            else if (name.contains ("rimclick") || name.contains ("sidestick") || name.contains ("cross")) defaultNote = 37;
            else if (name.contains ("rim")) defaultNote = 37;
            else if (name.contains ("edge")) defaultNote = 39;
            else if (name.contains ("muted") || name.contains ("muff")) defaultNote = 33;
            else if (name.contains ("fat")) defaultNote = 29;
            else defaultNote = 38;
        }
        else if (name.contains ("hat") || name.contains ("hh"))
        {
            if (name.contains ("splash") || name.contains ("spl")) defaultNote = 63;
            else if (name.contains ("pedal") || name.contains ("foot")) defaultNote = 44;
            else if (name.contains ("open1") || name.contains ("op1")) defaultNote = 23;
            else if (name.contains ("open2") || name.contains ("op2")) defaultNote = 31;
            else if (name.contains ("open3") || name.contains ("op3")) defaultNote = 34;
            else if (name.contains ("open4") || name.contains ("op4")) defaultNote = 30;
            else if (name.contains ("semi")) defaultNote = 80;
            else if (name.contains ("open")) defaultNote = name.contains ("edge") ? 26 : 46;
            else if (name.contains ("closed") || name.contains ("close")) defaultNote = name.contains ("edge") ? 22 : 42;
            else defaultNote = 42;
        }
        else if (name.contains ("tom2") || name.contains ("tom 2") || name.contains ("racktom2") || name.contains ("midtom") || name.contains ("tom13"))
            defaultNote = name.contains ("rim") ? 60 : 47;
        else if (name.contains ("tom3") || name.contains ("tom 3") || name.contains ("lowtom") || name.contains ("tom14"))
            defaultNote = name.contains ("rim") ? 62 : 45;
        else if (name.contains ("ftom2") || name.contains ("floor2") || name.contains ("tom18"))
            defaultNote = name.contains ("rim") ? 65 : 41;
        else if (name.contains ("ftom") || name.contains ("floor") || name.contains ("tom16"))
            defaultNote = name.contains ("rim") ? 58 : 43;
        else if (name.contains ("tom1") || name.contains ("tom 1") || name.contains ("racktom1") || name.contains ("hightom") || name.contains ("tom10") || name.contains ("tom12") || name.contains ("tom"))
            defaultNote = name.contains ("rim") ? 50 : 48;
        else if (name.contains ("ride"))
            defaultNote = name.contains ("bell") ? 53 : (name.contains ("edge") ? 59 : 51);
        else if (name.contains ("splash"))
            defaultNote = 55;
        else if (name.contains ("china"))
            defaultNote = 52;
        else if (name.contains ("crash2") || name.contains ("crash 2"))
            defaultNote = name.contains ("edge") ? 52 : 57;
        else if (name.contains ("crash3") || name.contains ("crash 3"))
            defaultNote = name.contains ("edge") ? 54 : 56;
        else if (name.contains ("crash"))
            defaultNote = name.contains ("edge") ? 55 : 49;
        else if (name.contains ("cowbell"))
            defaultNote = name.contains ("muff") ? 32 : (name.contains ("hi") ? 33 : 31);

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

    // Reset per-instrument controls for the new kit.
    sampler.resetInstrumentParams();

    for (int i = 0; i < maxSupportedInstruments; ++i)
    {
        float levelNorm = 0.5f; // default 1.0f (0.5 * 2.0f)
        float panNorm = 0.5f;   // default 0.0f (0.5 * 2.0f - 1.0f)
        int busIdx = std::min (i, 15);

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

        if (i < static_cast<int> (instrumentLevelParams.size()))
            instrumentLevelParams[static_cast<size_t> (i)]->setValueNotifyingHost (levelNorm);
        if (i < static_cast<int> (instrumentPanParams.size()))
            instrumentPanParams[static_cast<size_t> (i)]->setValueNotifyingHost (panNorm);
        if (i < static_cast<int> (instrumentPitchParams.size()))
            instrumentPitchParams[static_cast<size_t> (i)]->setValueNotifyingHost (0.5f);
        if (i < static_cast<int> (instrumentDynamicsParams.size()))
            instrumentDynamicsParams[static_cast<size_t> (i)]->setValueNotifyingHost (0.5f);
        if (i < static_cast<int> (instrumentBusParams.size()))
            instrumentBusParams[static_cast<size_t> (i)]->setValueNotifyingHost (busIdx / 15.0f);
    }

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
    state.setAttribute ("hiHatMode", static_cast<int> (sampler.getHiHatMode()));

    const auto mixerPreset = sampler.getCurrentMixerPreset ("CurrentMix");
    if (auto mixerXml = juce::XmlDocument::parse (mixerPreset.toXmlString()))
    {
        mixerXml->setTagName ("MixerState");
        state.addChildElement (mixerXml.release());
    }

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

    if (xml->hasAttribute ("hiHatMode"))
    {
        const int modeInt = xml->getIntAttribute ("hiHatMode", 0);
        sampler.setHiHatMode (modeInt == 1 ? HiHatMode::discreteNotes : HiHatMode::continuousCC4);
    }

    if (auto* mixerXml = xml->getChildByName ("MixerState"))
    {
        MixerPreset p;
        if (MixerPreset::fromXmlString (mixerXml->toString(), p))
            sampler.applyMixerPreset (p);
    }

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
