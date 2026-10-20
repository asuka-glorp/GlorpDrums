#include "Sampler.h"

#include <algorithm>
#include <cmath>

namespace glorp
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /// Equal-power pan gains for a -1..+1 position.
    void panGains (float pan, float& left, float& right) noexcept
    {
        const auto p = std::clamp (static_cast<double> ((std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f) * pi,
                                   0.0, 0.5 * pi);
        left  = static_cast<float> (std::cos (p));
        right = static_cast<float> (std::sin (p));
    }

    /// Builds a std::atomic-friendly next-sample-clock value. Guarded by callers
    /// being on the audio thread only.
    inline float velocityWithCurve (float velocity, float curve) noexcept
    {
        const auto v = std::clamp (velocity, 0.0f, 1.0f);
        return std::pow (v, std::max (0.05f, curve));
    }
}

//==============================================================================
//==============================================================================
// Varispeed tuning introduces zero latency — no window, no lookahead.

namespace
{
    /// Releases the kit lock however the enclosing scope is left.
    struct KitUnlock
    {
        juce::CriticalSection& lock;
        explicit KitUnlock (juce::CriticalSection& l) : lock (l) {}
        ~KitUnlock() { lock.exit(); }
    };
}

Sampler::Sampler()
{
    for (int i = 0; i < maxVoices; ++i)
        pool.voices.push_back (std::make_unique<Voice>());

    setSampleRateAndBlock (44100.0, 512);
}

Sampler::~Sampler() = default;

void Sampler::setSampleRateAndBlock (double rate, int blockSize)
{
    const juce::ScopedLock lock (kitLock);

    currentSampleRate = rate > 0.0 ? rate : 44100.0;
    currentBlockSize = std::max (16, blockSize);

    // Preallocate voice scratch buffers to max block size to avoid audio-thread allocation
    for (auto& v : pool.voices)
    {
        v->mixL.resize (static_cast<size_t> (currentBlockSize));
        v->mixR.resize (static_cast<size_t> (currentBlockSize));
    }

    micScratchL.resize (32);
    micScratchR.resize (32);
    for (size_t i = 0; i < 32; ++i)
    {
        micScratchL[i].resize (static_cast<size_t> (currentBlockSize));
        micScratchR[i].resize (static_cast<size_t> (currentBlockSize));
    }
    reverbAccumL.resize (static_cast<size_t> (currentBlockSize));
    reverbAccumR.resize (static_cast<size_t> (currentBlockSize));

    masterFX.prepare (currentSampleRate, currentBlockSize);
    crosstalk.prepare (currentSampleRate, currentBlockSize);
    for (auto& ch : micChannels)
        ch->fx.prepare (currentSampleRate, currentBlockSize);
}

//==============================================================================
bool Sampler::loadKit (DrumKit kit, juce::String& error)
{
    const juce::ScopedLock lock (kitLock);

    kitLoaded.store (false);
    allNotesOff();

    if (kit.instruments.empty())
    {
        error = kit.error.empty() ? juce::String ("the kit has no playable instruments")
                                 : juce::String (kit.error);
        return false;
    }

    if (static_cast<int> (kit.instruments.size()) > GLORP_MAX_INSTRUMENT_BUSES)
    {
        // Not a hard failure. Big kits are normal - CrocellKit_full alone has 27
        // instruments - and refusing to play it would be worse than losing a bit of
        // per-instrument separation on the extras. Everything past the last bus
        // shares that bus, and the user is told.
        const auto overflow = static_cast<int> (kit.instruments.size()) - GLORP_MAX_INSTRUMENT_BUSES;
        kit.warnings.push_back ("This kit has " + std::to_string (kit.instruments.size())
                              + " instruments but there are only " + std::to_string (GLORP_MAX_INSTRUMENT_BUSES)
                              + " output buses, so the last " + std::to_string (overflow)
                              + " instruments share a bus. Rebuild with a higher "
                                "-DGLORP_MAX_INSTRUMENT_BUSES for full separation.");
    }

    std::vector<InstrumentRuntime> resolved;
    resolved.reserve (kit.instruments.size());

    int missingFiles = 0;

    for (const auto& instrument : kit.instruments)
    {
        InstrumentRuntime runtime;
        resolveInstrumentChannels (kit, instrument, runtime);

        if (runtime.hits.empty())
            ++missingFiles;

        resolved.push_back (std::move (runtime));
    }

    for (size_t i = 0; i < kit.instruments.size() && i < resolved.size(); ++i)
    {
        for (const auto& chokeSpec : kit.instruments[i].chokes)
        {
            const auto targetStr = juce::String (chokeSpec.instrument);
            for (size_t targetIdx = 0; targetIdx < resolved.size(); ++targetIdx)
            {
                if (resolved[targetIdx].name.equalsIgnoreCase (targetStr))
                {
                    resolved[i].chokeTargets.push_back ({
                        static_cast<int> (targetIdx),
                        chokeSpec.hasChokeTime ? chokeSpec.chokeTimeMs : 40.0f
                    });
                }
            }
        }
    }

    if (resolved.empty())
    {
        error = "None of this kit's sample files could be read. Check the kit directory is still in place.";
        return false;
    }

    instruments_ = std::move (resolved);
    kitChannels_ = kit.channels;
    instrumentCount = static_cast<int> (instruments_.size());
    kitSampleRate = kit.declaredSampleRate > 0 ? static_cast<double> (kit.declaredSampleRate) : currentSampleRate;

    // The number of buses in use is bounded by what was built; anything beyond
    // that shares the last bus rather than being dropped.
    busCount = std::min (instrumentCount, GLORP_MAX_INSTRUMENT_BUSES);
    kitTitle = kit.title;

    channelParams.assign (static_cast<size_t> (instrumentCount), ChannelParams {});
    roundRobinCursor.assign (static_cast<size_t> (instrumentCount), 0);
    instrumentBuses.assign (static_cast<size_t> (instrumentCount), 0);

    // Identify hi-hat articulation indices for continuous pedal control
    hiHat = {};
    cc4Active.store (false);
    for (int i = 0; i < instrumentCount; ++i)
    {
        const auto n = instruments_[static_cast<size_t> (i)].name.toLowerCase();
        if (n.contains ("hat") || n.contains ("hh") || instruments_[static_cast<size_t> (i)].group.equalsIgnoreCase ("hihat"))
        {
            if (n.contains ("splash") || n.contains ("spl"))
                hiHat.splash = i;
            else if (n.contains ("pedal") || n.contains ("foot"))
                hiHat.pedal = i;
            else if (n.contains ("open1") || n.contains ("op1") || n.contains ("op 1"))
                hiHat.open1 = i;
            else if (n.contains ("open2") || n.contains ("op2") || n.contains ("op 2"))
                hiHat.open2 = i;
            else if (n.contains ("open3") || n.contains ("op3") || n.contains ("op 3"))
                hiHat.open3 = i;
            else if (n.contains ("open4") || n.contains ("op4") || n.contains ("op 4"))
                hiHat.open4 = i;
            else if (n.contains ("semi"))
                hiHat.semiOpen = i;
            else if (n.contains ("closed") || n.contains ("close") || n.contains ("clo"))
            {
                if (n.contains ("edge") || n.contains ("edg"))
                    hiHat.closedEdge = i;
                else
                    hiHat.closed = i;
            }
            else if (n.contains ("open") || n.contains ("op"))
            {
                if (n.contains ("edge") || n.contains ("edg"))
                    hiHat.openEdge = i;
                else
                    hiHat.open = i;
            }
        }
    }

    for (int i = 0; i < instrumentCount; ++i)
    {
        if (kit.isNativeGlorpKit)
        {
            instrumentBuses[static_cast<size_t> (i)] = std::clamp (kit.instruments[static_cast<size_t> (i)].outputBus, 0, 15);

            const auto& inst = kit.instruments[static_cast<size_t> (i)];
            auto itL = kit.factoryInstrumentLevels.find (inst.logicalName);
            if (itL == kit.factoryInstrumentLevels.end())
                itL = kit.factoryInstrumentLevels.find (inst.name);
            if (itL != kit.factoryInstrumentLevels.end())
                channelParams[static_cast<size_t> (i)].level = itL->second;

            auto itP = kit.factoryInstrumentPans.find (inst.logicalName);
            if (itP == kit.factoryInstrumentPans.end())
                itP = kit.factoryInstrumentPans.find (inst.name);
            if (itP != kit.factoryInstrumentPans.end())
                channelParams[static_cast<size_t> (i)].pan = itP->second;
        }
        else
        {
            const auto n = instruments_[static_cast<size_t> (i)].name.toLowerCase();
            if (n.contains ("kick") || n.contains ("kdrum"))
                instrumentBuses[static_cast<size_t> (i)] = 0; // Bus 1: Ch 1-2
            else if (n.contains ("snare") || n.contains ("rimclick") || n.contains ("rimshot"))
                instrumentBuses[static_cast<size_t> (i)] = 1; // Bus 2: Ch 3-4
            else if (n.contains ("hihat") || n.contains ("hh"))
                instrumentBuses[static_cast<size_t> (i)] = 2; // Bus 3: Ch 5-6
            else if (n.contains ("tom1") || n.contains ("hightom"))
                instrumentBuses[static_cast<size_t> (i)] = 3; // Bus 4: Ch 7-8
            else if (n.contains ("tom2") || n.contains ("midtom"))
                instrumentBuses[static_cast<size_t> (i)] = 4; // Bus 5: Ch 9-10
            else if (n.contains ("ftom") || n.contains ("floor"))
                instrumentBuses[static_cast<size_t> (i)] = 5; // Bus 6: Ch 11-12
            else if (n.contains ("ride"))
                instrumentBuses[static_cast<size_t> (i)] = 6; // Bus 7: Ch 13-14
            else if (n.contains ("crash") || n.contains ("cymbal") || n.contains ("splash") || n.contains ("china"))
                instrumentBuses[static_cast<size_t> (i)] = 7; // Bus 8: Ch 15-16
            else
                instrumentBuses[static_cast<size_t> (i)] = std::min (i, 15);
        }
    }

    // Initialize per-mic channel strip states & DSP
    micChannels.clear();
    for (size_t k = 0; k < kitChannels_.size(); ++k)
    {
        auto ch = std::make_unique<MicChannelState>();
        ch->name = kitChannels_[k].name;
        ch->title = kitChannels_[k].title.empty() ? kitChannels_[k].name : kitChannels_[k].title;
        ch->params = MicStripParams {};
        ch->fx.prepare (currentSampleRate, currentBlockSize);

        auto itM = kit.factoryMicLevels.find (ch->name.toStdString());
        if (itM != kit.factoryMicLevels.end())
            ch->params.level = itM->second;

        micChannels.push_back (std::move (ch));
    }

    // Detect snare top and bottom mics for sympathetic snare wire resonance
    snareMicIndex = -1;
    snareBotMicIndex = -1;
    for (size_t k = 0; k < kitChannels_.size(); ++k)
    {
        const auto n = juce::String (kitChannels_[k].name).toLowerCase();
        if (n.contains ("snare") || n.contains ("sd_") || n.contains ("sn_"))
        {
            if (n.contains ("bot") || n.contains ("bottom") || n.contains ("under"))
                snareBotMicIndex = static_cast<int> (k);
            else if (snareMicIndex < 0 || n.contains ("top") || n.contains ("up"))
                snareMicIndex = static_cast<int> (k);
        }
    }
    if (snareMicIndex < 0 && snareBotMicIndex >= 0)
        snareMicIndex = snareBotMicIndex;
    if (snareBotMicIndex < 0 && snareMicIndex >= 0)
        snareBotMicIndex = snareMicIndex;

    // A kit with more instruments than buses is a hard error, not a silent
    // truncation, so surface it rather than hiding some of the kit.
    if (missingFiles > 0)
    {
        error = juce::String (missingFiles) + " of " + juce::String (kit.instruments.size())
              + " instruments had unreadable sample files and were skipped.";
    }

    recomputeSoloFlags();
    sampleCache.trim();

    kitLoaded.store (true);
    return true;
}

void Sampler::unloadKit()
{
    const juce::ScopedLock lock (kitLock);

    kitLoaded.store (false);
    allNotesOff();

    crosstalk.reset();
    snareMicIndex = -1;
    snareBotMicIndex = -1;

    instruments_.clear();
    kitChannels_.clear();
    micChannels.clear();
    channelParams.clear();
    roundRobinCursor.clear();
    instrumentBuses.clear();
    instrumentCount = 0;
    busCount = 0;
    kitTitle.clear();

    anySolo.store (false);
    hiHat = {};
    cc4Active.store (false);
    sampleCache.trim();
}

void Sampler::recomputeSoloFlags()
{
    const auto soloing = anySolo.load();

    for (auto& p : channelParams)
        p.muted = p.mute || (soloing && ! p.solo);
}

void Sampler::resetInstrumentParams()
{
    for (auto& p : channelParams)
    {
        p = ChannelParams {};
        p.level = 1.0f;
        p.velocityCurve = VelocityCurve::linear;
    }

    anySolo.store (false);
    recomputeSoloFlags();
}

int Sampler::numKitChannels() const
{
    return static_cast<int> (kitChannels_.size());
}

juce::String Sampler::kitChannelName (int index) const
{
    if (index < 0 || index >= static_cast<int> (kitChannels_.size()))
        return {};

    return kitChannels_[static_cast<size_t> (index)].name;
}

juce::String Sampler::kitChannelTitle (int index) const
{
    if (index < 0 || index >= static_cast<int> (kitChannels_.size()))
        return {};

    const auto& c = kitChannels_[static_cast<size_t> (index)];

    if (! c.title.empty())
        return juce::String (c.title);

    return juce::String (c.name);
}

int Sampler::numMicStrips() const noexcept
{
    return static_cast<int> (micChannels.size());
}

juce::String Sampler::micStripName (int index) const
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        return micChannels[static_cast<size_t> (index)]->name;
    return {};
}

juce::String Sampler::micStripTitle (int index) const
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        return micChannels[static_cast<size_t> (index)]->title;
    return {};
}

void Sampler::setMicStripParams (int index, const MicStripParams& params)
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        micChannels[static_cast<size_t> (index)]->params = params;
}

MicStripParams Sampler::getMicStripParams (int index) const
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        return micChannels[static_cast<size_t> (index)]->params;
    return {};
}

void Sampler::setMicOutputBus (int index, int bus)
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        micChannels[static_cast<size_t> (index)]->params.outputBus = std::clamp (bus, 0, 15);
}

int Sampler::getMicOutputBus (int index) const
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        return micChannels[static_cast<size_t> (index)]->params.outputBus;
    return 0;
}

void Sampler::setMasterStripParams (const MasterStripParams& params)
{
    masterStripParams = params;
}

MasterStripParams Sampler::getMasterStripParams() const
{
    return masterStripParams;
}

void Sampler::applyMixerPreset (const MixerPreset& preset)
{
    masterStripParams = preset.master;
    for (size_t i = 0; i < preset.micStrips.size() && i < micChannels.size(); ++i)
        micChannels[i]->params = preset.micStrips[i];
}

MixerPreset Sampler::getCurrentMixerPreset (const juce::String& name) const
{
    MixerPreset p;
    p.name = name;
    p.master = masterStripParams;
    for (const auto& ch : micChannels)
    {
        p.micStrips.push_back (ch->params);
        p.micNames.push_back (ch->title);
    }
    return p;
}

float Sampler::getMicPeakL (int index) const noexcept
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        return micChannels[static_cast<size_t> (index)]->peakL.load (std::memory_order_relaxed);
    return 0.0f;
}

float Sampler::getMicPeakR (int index) const noexcept
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        return micChannels[static_cast<size_t> (index)]->peakR.load (std::memory_order_relaxed);
    return 0.0f;
}

float Sampler::getMicRmsL (int index) const noexcept
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        return micChannels[static_cast<size_t> (index)]->rmsL.load (std::memory_order_relaxed);
    return 0.0f;
}

float Sampler::getMicRmsR (int index) const noexcept
{
    if (index >= 0 && index < static_cast<int> (micChannels.size()))
        return micChannels[static_cast<size_t> (index)]->rmsR.load (std::memory_order_relaxed);
    return 0.0f;
}

//==============================================================================
juce::String Sampler::instrumentName (int index) const
{
    if (index < 0 || index >= instrumentCount)
        return {};

    return instruments_[static_cast<size_t> (index)].name;
}

int Sampler::numSamplesForInstrument (int index) const
{
    if (index < 0 || index >= instrumentCount)
        return 0;

    return static_cast<int> (instruments_[static_cast<size_t> (index)].hits.size());
}

juce::String Sampler::sampleName (int instrument, int sample) const
{
    if (instrument < 0 || instrument >= instrumentCount)
        return {};

    const auto& hits = instruments_[static_cast<size_t> (instrument)].hits;

    if (sample < 0 || sample >= static_cast<int> (hits.size()))
        return {};

    return hits[static_cast<size_t> (sample)].name;
}

//==============================================================================
void Sampler::setInstrumentParams (int index, const InstrumentParams& params)
{
    if (index < 0 || index >= static_cast<int> (channelParams.size()))
        return;

    auto& p = channelParams[static_cast<size_t> (index)];

    const float newPitch = std::clamp (params.pitchSemitones, -12.0f, 12.0f);
    const bool pitchChanged = (p.pitch != newPitch);
    const bool soloChanged = (p.solo != params.solo || p.mute != params.mute);

    p.level = std::max (0.0f, params.level);
    p.pan = std::clamp (params.pan, -1.0f, 1.0f);
    p.pitch = newPitch;
    p.spread = std::clamp (params.tuneSpread, 0.0f, 100.0f);
    p.dynamics = std::clamp (params.dynamics, 0.0f, 1.0f);
    p.velocityCurve = params.velocityCurve;
    p.roundRobin = params.autoRoundRobin;
    p.selectedSample = params.selectedSample;
    p.mute = params.mute;
    p.solo = params.solo;

    if (soloChanged)
    {
        bool currentAnySolo = false;
        for (const auto& cp : channelParams)
            if (cp.solo) { currentAnySolo = true; break; }
        anySolo.store (currentAnySolo, std::memory_order_relaxed);
        recomputeSoloFlags();
    }
}

InstrumentParams Sampler::getInstrumentParams (int index) const
{
    InstrumentParams out;

    if (index < 0 || index >= static_cast<int> (channelParams.size()))
        return out;

    const auto& p = channelParams[static_cast<size_t> (index)];

    out.level = p.level;
    out.pan = p.pan;
    out.pitchSemitones = p.pitch;
    out.tuneSpread = p.spread;
    out.dynamics = p.dynamics;
    out.velocityCurve = p.velocityCurve;
    out.autoRoundRobin = p.roundRobin;
    out.mute = p.mute;
    out.solo = p.solo;
    out.selectedSample = p.selectedSample;

    return out;
}

void Sampler::setInstrumentBus (int instrument, int busIndex)
{
    if (instrument >= 0 && instrument < static_cast<int> (instrumentBuses.size()))
        instrumentBuses[static_cast<size_t> (instrument)] = std::clamp (busIndex, 0, 15);
}

int Sampler::getInstrumentBus (int instrument) const noexcept
{
    if (instrument >= 0 && instrument < static_cast<int> (instrumentBuses.size()))
        return instrumentBuses[static_cast<size_t> (instrument)];
    return 0;
}

void Sampler::setGlobalParams (const GlobalParams& p)
{
    globals.masterLevel = std::max (0.0f, p.masterLevel);
    globals.masterPan = std::clamp (p.masterPan, -1.0f, 1.0f);
    globals.attackMs = std::clamp (p.attackMs, 0.0f, 500.0f);
    globals.decayScale = std::clamp (p.decayScale, 0.05f, 4.0f);
    globals.humanize = std::clamp (p.humanize, 0.0f, 1.0f);
    globals.velCurve = std::clamp (p.velCurve, 0.2f, 4.0f);
}

GlobalParams Sampler::getGlobalParams() const
{
    return { globals.masterLevel, globals.masterPan, globals.attackMs,
             globals.decayScale, globals.humanize, globals.velCurve };
}

//==============================================================================
void Sampler::resolveInstrumentChannels (const DrumKit& kit, const Instrument& instrument, InstrumentRuntime& out)
{
    out.name = instrument.displayName;
    out.group = instrument.group;
    out.piece = instrument.bleedPiece;

    for (size_t s = 0; s < instrument.samples.size(); ++s)
    {
        const auto& sources = instrument.resolvedChannels[s];

        if (sources.empty())
            continue;

        ResolvedHit hit;
        hit.name = instrument.samples[s].name;
        hit.power = instrument.samples[s].power;

        const auto channelCount = sources.size();
        // If the kit defines discrete mic channels, each source channel is routed
        // to its respective mic mixer track at unity gain (1.0f), matching DAW / REAPER behavior.
        // Only if kit.channels is empty (all audio files collapse into a single stereo pair)
        // do we normalize by sqrt(channelCount) to prevent digital clipping when summing.
        const bool hasDiscreteMics = ! kit.channels.empty();
        const float normalisation = hasDiscreteMics
            ? 1.0f
            : (1.0f / std::sqrt (std::max (1.0f, static_cast<float> (channelCount))));

        for (size_t c = 0; c < channelCount; ++c)
        {
            const auto& source = sources[c];
            const auto& ref = instrument.samples[s].audioFiles[static_cast<size_t> (source.audioFileIndex)];

            ChannelRef channelRef;
            channelRef.handle = sampleCache.open (instrument.samplePath (ref));
            channelRef.channel = ref.fileChannel;
            channelRef.kitChannelIndex = 0;
            channelRef.isBleed = ! source.main;
            channelRef.pieceIdx = static_cast<int> (instrument.bleedPiece);
            channelRef.channelBleedFactor = source.bleed;

            for (size_t k = 0; k < kit.channels.size(); ++k)
            {
                if (kit.channels[k].name == source.kitChannel || kit.channels[k].title == source.kitChannel)
                {
                    channelRef.kitChannelIndex = static_cast<int> (k);
                    break;
                }
            }

            if (! channelRef.handle.valid)
                continue;

            // When discrete mic channels exist, mic level is applied by the channel strip fader
            // (ch->params.level initialized from kit.factoryMicLevels).
            // For fallback single-track kits, apply factoryMicLevels directly here.
            float micLevel = 1.0f;
            if (! hasDiscreteMics)
            {
                auto itM = kit.factoryMicLevels.find (ref.channel);
                if (itM == kit.factoryMicLevels.end())
                    itM = kit.factoryMicLevels.find (source.kitChannel);
                if (itM != kit.factoryMicLevels.end())
                    micLevel = itM->second;
            }

            if (channelCount == 1)
            {
                channelRef.gainL = normalisation * micLevel;
                channelRef.gainR = normalisation * micLevel;
            }
            else
            {
                const auto chName = juce::String (ref.channel).toLowerCase();
                if (chName.contains ("left") || chName.endsWith ("l") || chName.contains ("_l") || chName.contains (".l"))
                {
                    channelRef.gainL = normalisation * micLevel;
                    channelRef.gainR = 0.0f;
                }
                else if (chName.contains ("right") || chName.endsWith ("r") || chName.contains ("_r") || chName.contains (".r"))
                {
                    channelRef.gainL = 0.0f;
                    channelRef.gainR = normalisation * micLevel;
                }
                else
                {
                    channelRef.gainL = normalisation * micLevel;
                    channelRef.gainR = normalisation * micLevel;
                }
            }

            hit.channels.push_back (std::move (channelRef));
            hit.numFrames = std::max (hit.numFrames, channelRef.handle.numFrames);
        }

        if (! hit.channels.empty() && hit.numFrames > 0)
            out.hits.push_back (std::move (hit));
    }
}

//==============================================================================
float Sampler::nextRandom()
{
    // xorshift: cheap, and good enough for humanising.
    auto x = randomState.load();
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    randomState.store (x);
    return static_cast<float> (x & 0xFFFFFF) / static_cast<float> (0xFFFFFF) * 2.0f - 1.0f;
}

Sampler::Voice* Sampler::allocateVoice()
{
    for (int attempt = 0; attempt < maxVoices; ++attempt)
    {
        const auto i = static_cast<size_t> (nextVoiceIndex.fetch_add (1, std::memory_order_relaxed) % maxVoices);

        if (! pool.voices[i]->active)
            return pool.voices[i].get();
    }

    // Everything is busy: steal the quietest, and oldest of those.
    Voice* best = nullptr;

    for (auto& voicePtr : pool.voices)
    {
        auto& v = *voicePtr;

        if (! best
            || v.velocity < best->velocity
            || (v.velocity == best->velocity && v.startedAt < best->startedAt))
            best = &v;
    }

    return best;
}

int Sampler::chooseHit (int instrumentIndex, float velocity)
{
    if (instrumentIndex < 0 || instrumentIndex >= instrumentCount)
        return 0;

    const auto& hits = instruments_[static_cast<size_t> (instrumentIndex)].hits;
    const auto numHits = static_cast<int> (hits.size());

    if (numHits <= 0)
        return 0;

    if (numHits == 1)
        return 0;

    const float vel = std::clamp (velocity, 0.05f, 1.0f);

    // If hit at full velocity (mouse click or velocity >= 0.96), choose from top-power bracket
    if (vel >= 0.96f)
    {
        float maxPower = 0.0f;
        for (const auto& h : hits)
            maxPower = std::max (maxPower, h.power);

        std::vector<int> topCandidates;
        for (int i = 0; i < numHits; ++i)
        {
            if (hits[static_cast<size_t> (i)].power >= maxPower * 0.88f)
                topCandidates.push_back (i);
        }

        if (! topCandidates.empty())
        {
            auto& cursor = roundRobinCursor[static_cast<size_t> (instrumentIndex)];
            const auto chosen = topCandidates[static_cast<size_t> (cursor % static_cast<int> (topCandidates.size()))];
            cursor = (cursor + 1) % 10000;
            return chosen;
        }
    }

    // Velocity-aware matching: pick from samples whose recorded power is close to velocity
    float minDiff = 1000.0f;
    int closestIdx = 0;

    for (int i = 0; i < numHits; ++i)
    {
        const float diff = std::abs (hits[static_cast<size_t> (i)].power - vel);
        if (diff < minDiff)
        {
            minDiff = diff;
            closestIdx = i;
        }
    }

    // Collect candidates within a proportional power window around closest
    std::vector<int> candidates;
    const float margin = std::max (0.12f, minDiff * 1.5f);
    for (int i = 0; i < numHits; ++i)
    {
        if (std::abs (hits[static_cast<size_t> (i)].power - vel) <= margin)
            candidates.push_back (i);
    }

    if (candidates.empty())
        candidates.push_back (closestIdx);

    auto& cursor = roundRobinCursor[static_cast<size_t> (instrumentIndex)];
    const auto chosen = candidates[static_cast<size_t> (cursor % static_cast<int> (candidates.size()))];
    cursor = (cursor + 1) % 10000;

    return chosen;
}

//==============================================================================
void Sampler::noteOn (int instrument, float velocity, int64_t timeInSamples)
{
    noteOn (instrument, -1, velocity, timeInSamples);
}

void Sampler::noteOn (int instrument, int note, float velocity, int64_t timeInSamples)
{
    if (! kitLoaded.load())
        return;

    if (instrument < 0 || instrument >= instrumentCount)
        return;

    // E-drum continuous hi-hat articulation selection & crossfading based on pedal CC#4.
    if (hiHatMode.load (std::memory_order_relaxed) == HiHatMode::continuousCC4
        && cc4Active.load (std::memory_order_relaxed))
    {
        const bool isVariableHiHatHit = (instrument == hiHat.open || instrument == hiHat.openEdge
                                         || (instrument == hiHat.closed && (note == 46 || note < 0)));

        if (isVariableHiHatHit)
        {
            const float pedal = std::clamp (hiHatPedal.load (std::memory_order_relaxed), 0.0f, 1.0f);
            const bool wantsEdge = (instrument == hiHat.openEdge || instrument == hiHat.closedEdge);

            struct Stage { int inst; float pos; };
            std::vector<Stage> stages;

            if (wantsEdge)
            {
                if (hiHat.openEdge >= 0)   stages.push_back ({ hiHat.openEdge, 0.0f });
                else if (hiHat.open >= 0)  stages.push_back ({ hiHat.open, 0.0f });

                if (hiHat.semiOpen >= 0)   stages.push_back ({ hiHat.semiOpen, 0.5f });
                else if (hiHat.open2 >= 0) stages.push_back ({ hiHat.open2, 0.5f });

                if (hiHat.closedEdge >= 0) stages.push_back ({ hiHat.closedEdge, 1.0f });
                else if (hiHat.closed >= 0) stages.push_back ({ hiHat.closed, 1.0f });
            }
            else
            {
                if (hiHat.open >= 0)       stages.push_back ({ hiHat.open, 0.0f });
                if (hiHat.open4 >= 0)      stages.push_back ({ hiHat.open4, 0.20f });
                if (hiHat.open3 >= 0)      stages.push_back ({ hiHat.open3, 0.40f });
                if (hiHat.open2 >= 0)      stages.push_back ({ hiHat.open2, 0.60f });
                if (hiHat.open1 >= 0)      stages.push_back ({ hiHat.open1, 0.80f });
                else if (hiHat.semiOpen >= 0) stages.push_back ({ hiHat.semiOpen, 0.75f });
                if (hiHat.closed >= 0)     stages.push_back ({ hiHat.closed, 1.0f });
            }

            if (stages.size() >= 2)
            {
                std::sort (stages.begin(), stages.end(), [](const auto& a, const auto& b) {
                    return a.pos < b.pos;
                });

                size_t idxA = 0;
                for (size_t s = 0; s + 1 < stages.size(); ++s)
                {
                    if (pedal >= stages[s].pos && pedal <= stages[s + 1].pos)
                    {
                        idxA = s;
                        break;
                    }
                    if (pedal > stages[s + 1].pos)
                        idxA = s;
                }
                const auto& stA = stages[idxA];
                const auto& stB = stages[std::min (idxA + 1, stages.size() - 1)];

                if (stA.inst == stB.inst)
                {
                    triggerVoice (stA.inst, note, velocity, timeInSamples);
                    return;
                }

                const float span = stB.pos - stA.pos;
                const float alpha = span > 0.001f ? std::clamp ((pedal - stA.pos) / span, 0.0f, 1.0f) : 0.0f;

                const float wA = std::cos (0.5f * static_cast<float> (pi) * alpha);
                const float wB = std::sin (0.5f * static_cast<float> (pi) * alpha);

                if (wB <= 0.08f)
                {
                    triggerVoice (stA.inst, note, velocity, timeInSamples);
                }
                else if (wA <= 0.08f)
                {
                    triggerVoice (stB.inst, note, velocity, timeInSamples);
                }
                else
                {
                    triggerVoice (stA.inst, note, velocity * wA, timeInSamples);
                    triggerVoice (stB.inst, note, velocity * wB, timeInSamples);
                }
                return;
            }
        }
    }

    triggerVoice (instrument, note, velocity, timeInSamples);
}

namespace
{
static bool isSameCymbal (const juce::String& nameA, const juce::String& nameB)
{
    const auto a = nameA.toLowerCase().removeCharacters (" _-");
    const auto b = nameB.toLowerCase().removeCharacters (" _-");

    // Check specific numbered/positioned cymbals (e.g. crash1 vs crash2, ride1 vs ride2)
    for (const char* id : { "crash1", "crash2", "crash3", "crash4", "crashl", "crashr",
                            "ride1", "ride2", "ride3", "china1", "china2", "splash1", "splash2" })
    {
        if (a.contains (id))
            return b.contains (id);
    }

    // Generic cymbal families when neither has a number/position
    if (a.contains ("ride"))
        return b.contains ("ride") && ! b.contains ("ride1") && ! b.contains ("ride2") && ! b.contains ("ride3");
    if (a.contains ("china"))
        return b.contains ("china") && ! b.contains ("china1") && ! b.contains ("china2");
    if (a.contains ("splash"))
        return b.contains ("splash") && ! b.contains ("splash1") && ! b.contains ("splash2");
    if (a.contains ("crash"))
        return b.contains ("crash") && ! b.contains ("crash1") && ! b.contains ("crash2")
            && ! b.contains ("crash3") && ! b.contains ("crash4")
            && ! b.contains ("crashl") && ! b.contains ("crashr");

    return false;
}
} // namespace

void Sampler::triggerVoice (int instrument, int note, float velocity, int64_t timeInSamples)
{
    if (instrument < 0 || instrument >= instrumentCount)
        return;

    const auto& runtime = instruments_[static_cast<size_t> (instrument)];
    if (runtime.hits.empty())
        return;

    // The only zones that stop playback when triggered are chokes (for the same cymbal)
    // and hi-hat pedal closing.
    const bool isChoke = runtime.name.containsIgnoreCase ("choke");
    const bool isHiHatPedal = (instrument == hiHat.pedal);

    if (isChoke)
    {
        chokeCymbal (instrument, 30, timeInSamples);

        for (const auto& target : runtime.chokeTargets)
        {
            if (target.instrumentIndex >= 0 && target.instrumentIndex < instrumentCount)
            {
                const auto& targetInst = instruments_[static_cast<size_t> (target.instrumentIndex)];
                if (isSameCymbal (runtime.name, targetInst.name))
                    chokeInstrument (target.instrumentIndex, static_cast<int64_t> (target.chokeTimeMs), timeInSamples);
            }
        }
    }
    else if (isHiHatPedal)
    {
        if (hiHat.open >= 0) chokeInstrument (hiHat.open, 20, timeInSamples);
        if (hiHat.openEdge >= 0) chokeInstrument (hiHat.openEdge, 20, timeInSamples);
        if (hiHat.open1 >= 0) chokeInstrument (hiHat.open1, 20, timeInSamples);
        if (hiHat.open2 >= 0) chokeInstrument (hiHat.open2, 20, timeInSamples);
        if (hiHat.open3 >= 0) chokeInstrument (hiHat.open3, 20, timeInSamples);
        if (hiHat.open4 >= 0) chokeInstrument (hiHat.open4, 20, timeInSamples);
        if (hiHat.semiOpen >= 0) chokeInstrument (hiHat.semiOpen, 20, timeInSamples);
    }

    auto* voice = allocateVoice();
    if (voice == nullptr)
        return;

    crosstalk.triggerSympatheticHit (runtime.piece, velocity);

    const auto& p = channelParams[static_cast<size_t> (instrument)];
    if (p.muted)
        return;

    // Apply e-drum velocity curve (linear, logarithmic, exponential, s-curve)
    float effectiveVel = std::clamp (velocity, 0.0f, 1.0f);
    switch (p.velocityCurve)
    {
        case VelocityCurve::logarithmic:
            effectiveVel = std::sqrt (effectiveVel);
            break;
        case VelocityCurve::exponential:
            effectiveVel = effectiveVel * effectiveVel;
            break;
        case VelocityCurve::sCurve:
            effectiveVel = effectiveVel * effectiveVel * (3.0f - 2.0f * effectiveVel);
            break;
        case VelocityCurve::linear:
        default:
            break;
    }

    if (std::abs (p.dynamics - 0.5f) > 0.01f)
    {
        const float dynExp = std::pow (2.0f, (p.dynamics - 0.5f) * 3.0f);
        effectiveVel = std::pow (std::clamp (effectiveVel, 0.01f, 1.0f), dynExp);
    }

    const auto hitIndex = chooseHit (instrument, effectiveVel);
    const auto& hit = runtime.hits[static_cast<size_t> (hitIndex)];

    float pitch = p.pitch;
    float level = velocityWithCurve (effectiveVel, globals.velCurve);

    if (globals.humanize > 0.0f)
    {
        level *= 1.0f + nextRandom() * 0.15f * globals.humanize;
        pitch += nextRandom() * (p.spread + 1.5f * globals.humanize);
    }
    else if (p.spread > 0.0f)
    {
        pitch += nextRandom() * p.spread;
    }

    pitch = std::clamp (pitch, -12.0f, 12.0f);

    voice->active = true;
    voice->instrument = instrument;
    voice->hit = hitIndex;
    voice->position = 0.0;

    const double rateRatio = (kitSampleRate > 0.0 && currentSampleRate > 0.0)
                                 ? kitSampleRate / currentSampleRate
                                 : 1.0;
    voice->pitchRatio = std::pow (2.0, pitch / 12.0) * rateRatio;

    voice->velocity = std::clamp (level, 0.0f, 2.0f);
    voice->pitchSemitones = pitch;
    voice->startedAt = timeInSamples;
    voice->endsAt = 0;
    voice->chokeFadeSamples = 0;
    voice->chokeStartSample = 0;
    voice->attackGain = globals.attackMs > 0.0f ? 0.0f : 1.0f;
    voice->attackSamples = static_cast<int> (globals.attackMs * 0.001 * currentSampleRate);

    voice->startSkip = std::max<int64_t> (0, timeInSamples - sampleClock);

    const auto naturalEnd = timeInSamples + static_cast<int64_t> (static_cast<double> (hit.numFrames) / voice->pitchRatio);
    voice->endsAtAbsolute = naturalEnd;

    if (globals.decayScale < 0.999f)
        voice->endsAt = timeInSamples + static_cast<int64_t> (static_cast<double> (hit.numFrames) * globals.decayScale / voice->pitchRatio);
    else
        voice->endsAt = 0;

    pushHitEvent ({ instrument, note, voice->velocity, pitch });
}

void Sampler::chokeInstrument (int instrument, int64_t fadeMs, int64_t timeInSamples)
{
    const auto fadeSamples = std::max<int64_t> (16, static_cast<int64_t> (static_cast<double> (fadeMs) * currentSampleRate * 0.001));

    for (auto& voicePtr : pool.voices)
    {
        auto& v = *voicePtr;

        if (v.active && v.instrument == instrument)
        {
            v.endsAt = timeInSamples + fadeSamples;
            v.chokeFadeSamples = fadeSamples;
            v.chokeStartSample = timeInSamples;
        }
    }
}

void Sampler::allNotesOff()
{
    for (auto& voicePtr : pool.voices)
    {
        auto& v = *voicePtr;

        v.active = false;
        v.instrument = 0;
        v.hit = 0;
        v.position = 0;
        v.velocity = 0.0f;
        v.pitchSemitones = 0.0f;
        v.startSkip = 0;
        v.endsAt = 0;
        v.chokeFadeSamples = 0;
        v.chokeStartSample = 0;
    }
}

void Sampler::setHiHatPedal (float pedalValue) noexcept
{
    const float clamped = std::clamp (pedalValue, 0.0f, 1.0f);
    cc4Active.store (true, std::memory_order_relaxed);
    const float prev = hiHatPedal.exchange (clamped, std::memory_order_relaxed);

    // Choke open hi-hats when pedal moves from open towards closed
    if (prev < 0.50f && clamped >= 0.55f)
    {
        if (hiHat.open >= 0) chokeInstrument (hiHat.open, 20, sampleClock);
        if (hiHat.openEdge >= 0) chokeInstrument (hiHat.openEdge, 20, sampleClock);
        if (hiHat.open1 >= 0) chokeInstrument (hiHat.open1, 20, sampleClock);
        if (hiHat.open2 >= 0) chokeInstrument (hiHat.open2, 20, sampleClock);
        if (hiHat.open3 >= 0) chokeInstrument (hiHat.open3, 20, sampleClock);
        if (hiHat.open4 >= 0) chokeInstrument (hiHat.open4, 20, sampleClock);
        if (hiHat.semiOpen >= 0) chokeInstrument (hiHat.semiOpen, 20, sampleClock);
    }

    // Firm downward foot stomp: trigger pedal chick
    if (clamped - prev > 0.22f && clamped >= 0.70f)
    {
        lastPedalDownClock = sampleClock;
        lastPedalDownSpeed = clamped - prev;

        const int chickInst = hiHat.pedal >= 0 ? hiHat.pedal : hiHat.closed;
        if (chickInst >= 0)
        {
            const float vel = std::clamp ((clamped - prev) * 2.2f, 0.25f, 1.0f);
            noteOn (chickInst, -1, vel, sampleClock);
        }
    }
    // Quick foot release immediately after a stomp: trigger Foot Splash!
    else if (prev - clamped > 0.22f && clamped < 0.40f)
    {
        const auto timeSinceDown = sampleClock - lastPedalDownClock;
        if (lastPedalDownClock > 0 && timeSinceDown < static_cast<int64_t> (currentSampleRate * 0.22) && lastPedalDownSpeed > 0.20f)
        {
            // Choke previous chick sound
            if (hiHat.pedal >= 0) chokeInstrument (hiHat.pedal, 15, sampleClock);
            if (hiHat.closed >= 0) chokeInstrument (hiHat.closed, 15, sampleClock);

            // Ring open splash articulation
            const int splashInst = hiHat.splash >= 0 ? hiHat.splash
                                                     : (hiHat.open >= 0 ? hiHat.open : hiHat.open1);
            if (splashInst >= 0)
            {
                const float vel = std::clamp (lastPedalDownSpeed * 2.0f, 0.30f, 1.0f);
                noteOn (splashInst, -1, vel, sampleClock);
            }
            lastPedalDownClock = 0; // reset
        }
    }
}



void Sampler::chokeCymbal (int instrumentIndex, int64_t fadeMs, int64_t timeInSamples)
{
    if (instrumentIndex < 0 || instrumentIndex >= instrumentCount)
        return;

    chokeInstrument (instrumentIndex, fadeMs, timeInSamples);

    const auto name = instruments_[static_cast<size_t> (instrumentIndex)].name;

    for (int i = 0; i < instrumentCount; ++i)
    {
        if (i == instrumentIndex)
            continue;

        const auto otherName = instruments_[static_cast<size_t> (i)].name;
        if (isSameCymbal (name, otherName))
            chokeInstrument (i, fadeMs, timeInSamples);
    }
}

void Sampler::chokeAllCymbals (int64_t fadeMs, int64_t timeInSamples)
{
    for (int i = 0; i < instrumentCount; ++i)
    {
        const auto n = instruments_[static_cast<size_t> (i)].name.toLowerCase();
        if (n.contains ("crash") || n.contains ("ride") || n.contains ("splash") || n.contains ("china") || n.contains ("cymbal"))
            chokeInstrument (i, fadeMs, timeInSamples);
    }
}

int Sampler::activeVoiceCount() const noexcept
{
    int count = 0;

    for (const auto& voicePtr : pool.voices)
        if (voicePtr->active)
            ++count;

    return count;
}

//==============================================================================
void Sampler::pushHitEvent (const HitEvent& event) noexcept
{
    const auto write = hitWrite.load (std::memory_order_relaxed);
    const auto read = hitRead.load (std::memory_order_relaxed);

    // If the GUI has fallen far behind, drop the event rather than overwrite
    // history the reader has not seen yet.
    if (write - read >= hitRingSize)
        return;

    hitRing[static_cast<size_t> (write) % hitRingSize] = event;
    hitWrite.store (write + 1, std::memory_order_release);
}

bool Sampler::popHit (HitEvent& event) noexcept
{
    const auto read = hitRead.load (std::memory_order_relaxed);
    const auto write = hitWrite.load (std::memory_order_acquire);

    if (read == write)
        return false;

    event = hitRing[static_cast<size_t> (read) % hitRingSize];
    hitRead.store (read + 1, std::memory_order_release);
    return true;
}

bool Sampler::hasPendingHits() const noexcept
{
    return hitRead.load (std::memory_order_relaxed) != hitWrite.load (std::memory_order_relaxed);
}

//==============================================================================
void Sampler::render (float* const* output, int numOutputChannels, int numSamples)
{
    if (numSamples <= 0 || numOutputChannels < 2)
        return;

    // A kit being swapped out must not be read mid-render.
    if (! kitLock.tryEnter())
    {
        for (int c = 0; c < numOutputChannels; ++c)
            if (output[c] != nullptr)
                juce::FloatVectorOperations::clear (output[c], numSamples);

        return;
    }

    KitUnlock unlock { kitLock };

    const auto totalBuses = numOutputChannels / 2;
    for (int c = 0; c < numOutputChannels; ++c)
        if (output[c] != nullptr)
            juce::FloatVectorOperations::clear (output[c], numSamples);

    if (! kitLoaded.load() || totalBuses <= 0)
    {
        sampleClock += numSamples;
        return;
    }

    const auto endOfBlock = sampleClock + numSamples;
    const int numMics = std::min (static_cast<int> (micChannels.size()), 32);

    if (numMics > 0)
    {
        // 1. Clear intermediate mic scratch buffers
        for (int m = 0; m < numMics; ++m)
        {
            std::fill (micScratchL[static_cast<size_t> (m)].begin(),
                       micScratchL[static_cast<size_t> (m)].begin() + numSamples, 0.0f);
            std::fill (micScratchR[static_cast<size_t> (m)].begin(),
                       micScratchR[static_cast<size_t> (m)].begin() + numSamples, 0.0f);
        }
        std::fill (reverbAccumL.begin(), reverbAccumL.begin() + numSamples, 0.0f);
        std::fill (reverbAccumR.begin(), reverbAccumR.begin() + numSamples, 0.0f);

        // 2. Voices accumulate directly into per-mic scratch buffers
        for (auto& voicePtr : pool.voices)
        {
            auto& v = *voicePtr;
            if (! v.active)
                continue;

            if (v.instrument < 0 || v.instrument >= instrumentCount)
            {
                v.active = false;
                continue;
            }

            const auto& runtime = instruments_[static_cast<size_t> (v.instrument)];
            if (v.hit < 0 || v.hit >= static_cast<int> (runtime.hits.size()))
            {
                v.active = false;
                continue;
            }

            if (v.endsAt > 0 && endOfBlock > v.endsAt)
            {
                v.active = false;
                continue;
            }

            const auto& hit = runtime.hits[static_cast<size_t> (v.hit)];
            if (v.position >= hit.numFrames)
            {
                v.active = false;
                continue;
            }

            const auto& p = channelParams[static_cast<size_t> (v.instrument)];
            if (p.muted)
                continue;

            float panL = 1.0f, panR = 1.0f;
            if (p.pan != 0.0f)
                panGains (p.pan, panL, panR);
            const float instGainL = v.velocity * p.level * panL;
            const float instGainR = v.velocity * p.level * panR;

            const double ratio = v.pitchRatio;
            const bool shifting = std::abs (ratio - 1.0) > 1.0e-5;

            const auto first = static_cast<size_t> (std::min<int64_t> (v.startSkip, numSamples));
            v.startSkip = std::max<int64_t> (0, v.startSkip - numSamples);
            const bool fading = v.attackSamples > 0 && v.attackGain < 1.0f;

            const int instBus = (v.instrument >= 0 && v.instrument < static_cast<int> (instrumentBuses.size()))
                                    ? instrumentBuses[static_cast<size_t> (v.instrument)] : 0;
            const bool hasAuxBus = multiOutMode.load() && (instBus > 0) && (instBus < totalBuses)
                                   && (output[instBus * 2] != nullptr) && (output[instBus * 2 + 1] != nullptr);
            float* const auxDestL = hasAuxBus ? output[instBus * 2] : nullptr;
            float* const auxDestR = hasAuxBus ? output[instBus * 2 + 1] : nullptr;

            for (int i = 0; i < numSamples; ++i)
            {
                if (v.position >= static_cast<double> (hit.numFrames))
                {
                    v.active = false;
                    break;
                }

                if (i < static_cast<int> (first))
                {
                    v.position += ratio;
                    continue;
                }

                float gain = 1.0f;
                if (fading)
                {
                    v.attackGain = std::min (1.0f, v.attackGain + 1.0f / static_cast<float> (v.attackSamples));
                    gain = v.attackGain;
                }

                // Smooth half-cosine choke rampdown
                if (v.endsAt > 0 && v.chokeFadeSamples > 0)
                {
                    const auto elapsed = (sampleClock + i) - v.chokeStartSample;
                    if (elapsed >= v.chokeFadeSamples)
                        gain = 0.0f;
                    else if (elapsed >= 0)
                    {
                        const float t = static_cast<float> (elapsed) / static_cast<float> (v.chokeFadeSamples);
                        gain *= 0.5f * (1.0f + std::cos (static_cast<float> (pi) * t));
                    }
                }
                else if (v.endsAt > 0)
                {
                    const auto remaining = v.endsAt - (sampleClock + i);
                    if (remaining <= 0)
                        gain = 0.0f;
                    else if (remaining < static_cast<int64_t> (currentSampleRate * 0.015))
                    {
                        const float t = static_cast<float> (remaining) / static_cast<float> (currentSampleRate * 0.015);
                        gain *= 0.5f * (1.0f - std::cos (static_cast<float> (pi) * t));
                    }
                }

                if (shifting)
                {
                    const int64_t pos = static_cast<int64_t> (v.position);
                    const float t = static_cast<float> (v.position - static_cast<double> (pos));

                    for (const auto& channelRef : hit.channels)
                    {
                        const int micIdx = std::clamp (channelRef.kitChannelIndex, 0, numMics - 1);
                        float bleedGain = 1.0f;
                        if (channelRef.isBleed)
                        {
                            bleedGain = bleedMatrix.getEffectiveGain (channelRef.pieceIdx, micIdx) * channelRef.channelBleedFactor;
                            if (bleedGain <= 0.0f)
                                continue;
                        }

                        const float y0 = channelRef.handle.readSample (pos - 1, channelRef.channel);
                        const float y1 = channelRef.handle.readSample (pos,     channelRef.channel);
                        const float y2 = channelRef.handle.readSample (pos + 1, channelRef.channel);
                        const float y3 = channelRef.handle.readSample (pos + 2, channelRef.channel);

                        const float c1 = 0.5f * (y2 - y0);
                        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                        const float sample = ((c3 * t + c2) * t + c1) * t + y1;

                        const float effGain = gain * bleedGain;

                        if (hasAuxBus)
                        {
                            float micLvlL = 1.0f, micLvlR = 1.0f;
                            if (micIdx < numMics)
                            {
                                const auto& mp = micChannels[static_cast<size_t> (micIdx)]->params;
                                float mPanL = 1.0f, mPanR = 1.0f;
                                if (mp.pan < 0.0f)      { mPanL = 1.0f; mPanR = 1.0f + mp.pan; }
                                else if (mp.pan > 0.0f) { mPanL = 1.0f - mp.pan; mPanR = 1.0f; }
                                const float phase = mp.phaseInvert ? -1.0f : 1.0f;
                                micLvlL = mp.level * mPanL * phase;
                                micLvlR = mp.level * mPanR * phase;
                            }
                            auxDestL[i] += sample * channelRef.gainL * instGainL * effGain * micLvlL;
                            auxDestR[i] += sample * channelRef.gainR * instGainR * effGain * micLvlR;
                        }
                        else
                        {
                            micScratchL[static_cast<size_t> (micIdx)][static_cast<size_t> (i)] +=
                                sample * channelRef.gainL * instGainL * effGain;
                            micScratchR[static_cast<size_t> (micIdx)][static_cast<size_t> (i)] +=
                                sample * channelRef.gainR * instGainR * effGain;
                        }
                    }
                }
                else
                {
                    const int64_t pos = static_cast<int64_t> (v.position);

                    for (const auto& channelRef : hit.channels)
                    {
                        const int micIdx = std::clamp (channelRef.kitChannelIndex, 0, numMics - 1);
                        float bleedGain = 1.0f;
                        if (channelRef.isBleed)
                        {
                            bleedGain = bleedMatrix.getEffectiveGain (channelRef.pieceIdx, micIdx) * channelRef.channelBleedFactor;
                            if (bleedGain <= 0.0f)
                                continue;
                        }

                        const float sample = channelRef.handle.readSample (pos, channelRef.channel);
                        const float effGain = gain * bleedGain;

                        if (hasAuxBus)
                        {
                            float micLvlL = 1.0f, micLvlR = 1.0f;
                            if (micIdx < numMics)
                            {
                                const auto& mp = micChannels[static_cast<size_t> (micIdx)]->params;
                                float mPanL = 1.0f, mPanR = 1.0f;
                                if (mp.pan < 0.0f)      { mPanL = 1.0f; mPanR = 1.0f + mp.pan; }
                                else if (mp.pan > 0.0f) { mPanL = 1.0f - mp.pan; mPanR = 1.0f; }
                                const float phase = mp.phaseInvert ? -1.0f : 1.0f;
                                micLvlL = mp.level * mPanL * phase;
                                micLvlR = mp.level * mPanR * phase;
                            }
                            auxDestL[i] += sample * channelRef.gainL * instGainL * effGain * micLvlL;
                            auxDestR[i] += sample * channelRef.gainR * instGainR * effGain * micLvlR;
                        }
                        else
                        {
                            micScratchL[static_cast<size_t> (micIdx)][static_cast<size_t> (i)] +=
                                sample * channelRef.gainL * instGainL * effGain;
                            micScratchR[static_cast<size_t> (micIdx)][static_cast<size_t> (i)] +=
                                sample * channelRef.gainR * instGainR * effGain;
                        }
                    }
                }

                v.position += ratio;
            }

            if (v.position >= static_cast<double> (hit.numFrames))
                v.active = false;
        }

        // 2b. Acoustic Crosstalk & Sympathetic Snare Wire Simulation (Phase 4)
        for (int m = 0; m < numMics; ++m)
        {
            micScratchLPtrs[static_cast<size_t> (m)] = micScratchL[static_cast<size_t> (m)].data();
            micScratchRPtrs[static_cast<size_t> (m)] = micScratchR[static_cast<size_t> (m)].data();
        }
        crosstalk.render (micScratchLPtrs.data(), micScratchRPtrs.data(), numMics, numSamples,
                          bleedMatrix, snareMicIndex, snareBotMicIndex);

        // 3. Process each mic channel strip (Phase, EQ, Comp, Saturation, Transient, Reverb send)
        bool anyMicSolo = false;
        for (int m = 0; m < numMics; ++m)
        {
            if (micChannels[static_cast<size_t> (m)]->params.solo)
            {
                anyMicSolo = true;
                break;
            }
        }

        for (int m = 0; m < numMics; ++m)
        {
            auto& state = *micChannels[static_cast<size_t> (m)];
            const auto& p = state.params;

            if (p.mute || (anyMicSolo && ! p.solo))
            {
                state.peakL.store (state.peakL.load() * 0.85f, std::memory_order_relaxed);
                state.peakR.store (state.peakR.load() * 0.85f, std::memory_order_relaxed);
                state.rmsL.store (0.0f, std::memory_order_relaxed);
                state.rmsR.store (0.0f, std::memory_order_relaxed);
                continue;
            }

            state.fx.process (micScratchL[static_cast<size_t> (m)].data(),
                              micScratchR[static_cast<size_t> (m)].data(),
                              numSamples, p,
                              reverbAccumL.data(), reverbAccumR.data());

            float micPanL = 1.0f, micPanR = 1.0f;
            if (p.pan < 0.0f)
            {
                micPanL = 1.0f;
                micPanR = 1.0f + p.pan;
            }
            else if (p.pan > 0.0f)
            {
                micPanL = 1.0f - p.pan;
                micPanR = 1.0f;
            }
            const float micGainL = p.level * micPanL;
            const float micGainR = p.level * micPanR;

            float blkPeakL = 0.0f, blkPeakR = 0.0f;
            float blkRmsSumL = 0.0f, blkRmsSumR = 0.0f;

            auto* sL = micScratchL[static_cast<size_t> (m)].data();
            auto* sR = micScratchR[static_cast<size_t> (m)].data();

            for (int i = 0; i < numSamples; ++i)
            {
                const float valL = sL[i] * micGainL;
                const float valR = sR[i] * micGainR;
                sL[i] = valL;
                sR[i] = valR;

                blkPeakL = std::max (blkPeakL, std::abs (valL));
                blkPeakR = std::max (blkPeakR, std::abs (valR));
                blkRmsSumL += valL * valL;
                blkRmsSumR += valR * valR;
            }

            const float blkRmsL = std::sqrt (blkRmsSumL / static_cast<float> (numSamples));
            const float blkRmsR = std::sqrt (blkRmsSumR / static_cast<float> (numSamples));
            const float prevL = state.peakL.load (std::memory_order_relaxed);
            const float prevR = state.peakR.load (std::memory_order_relaxed);
            state.peakL.store (std::max (blkPeakL, prevL * 0.88f), std::memory_order_relaxed);
            state.peakR.store (std::max (blkPeakR, prevR * 0.88f), std::memory_order_relaxed);
            state.rmsL.store (blkRmsL, std::memory_order_relaxed);
            state.rmsR.store (blkRmsR, std::memory_order_relaxed);

            // Output bus routing
            const int targetBus = (totalBuses > 1) ? std::clamp (p.outputBus, 0, totalBuses - 1) : 0;
            if (targetBus == 0 || ! multiOutMode.load() || p.sendToMaster)
            {
                if (output[0] != nullptr)
                    juce::FloatVectorOperations::add (output[0], sL, numSamples);
                if (output[1] != nullptr)
                    juce::FloatVectorOperations::add (output[1], sR, numSamples);
            }

            if (multiOutMode.load() && totalBuses > 1 && targetBus > 0)
            {
                if (output[targetBus * 2] != nullptr)
                    juce::FloatVectorOperations::add (output[targetBus * 2],     sL, numSamples);
                if (output[targetBus * 2 + 1] != nullptr)
                    juce::FloatVectorOperations::add (output[targetBus * 2 + 1], sR, numSamples);
            }
        }

        // 4. Master processing: Reverb return, Master Pan/Level, Soft Limiter
        if (output[0] != nullptr && output[1] != nullptr)
        {
            masterFX.process (output[0], output[1], numSamples,
                              reverbAccumL.data(), reverbAccumR.data(), masterStripParams);

            // Master metering
            float mPeakL = 0.0f, mPeakR = 0.0f;
            float mRmsSumL = 0.0f, mRmsSumR = 0.0f;
            for (int i = 0; i < numSamples; ++i)
            {
                mPeakL = std::max (mPeakL, std::abs (output[0][i]));
                mPeakR = std::max (mPeakR, std::abs (output[1][i]));
                mRmsSumL += output[0][i] * output[0][i];
                mRmsSumR += output[1][i] * output[1][i];
            }
            const float prevML = masterPeakL.load (std::memory_order_relaxed);
            const float prevMR = masterPeakR.load (std::memory_order_relaxed);
            masterPeakL.store (std::max (mPeakL, prevML * 0.88f), std::memory_order_relaxed);
            masterPeakR.store (std::max (mPeakR, prevMR * 0.88f), std::memory_order_relaxed);
            masterRmsL.store (std::sqrt (mRmsSumL / static_cast<float> (numSamples)), std::memory_order_relaxed);
            masterRmsR.store (std::sqrt (mRmsSumR / static_cast<float> (numSamples)), std::memory_order_relaxed);
        }
    }
    else
    {
        // Fallback for kits without microphone channel layout
        float masterL = 0.0f, masterR = 0.0f;
        panGains (globals.masterPan, masterL, masterR);
        const auto masterGainL = globals.masterLevel * masterL;
        const auto masterGainR = globals.masterLevel * masterR;

        for (auto& voicePtr : pool.voices)
        {
            auto& v = *voicePtr;
            if (! v.active) continue;
            if (v.instrument < 0 || v.instrument >= instrumentCount) { v.active = false; continue; }

            int targetBus = 0;
            if (multiOutMode.load() && totalBuses > 1)
            {
                const auto b = busForInstrument (v.instrument);
                targetBus = (b >= 0 && b < totalBuses) ? b : 0;
            }

            const auto& runtime = instruments_[static_cast<size_t> (v.instrument)];
            if (v.hit < 0 || v.hit >= static_cast<int> (runtime.hits.size())) { v.active = false; continue; }
            if (v.endsAt > 0 && endOfBlock > v.endsAt) { v.active = false; continue; }

            const auto& hit = runtime.hits[static_cast<size_t> (v.hit)];
            if (v.position >= hit.numFrames) { v.active = false; continue; }

            const auto& p = channelParams[static_cast<size_t> (v.instrument)];
            if (p.muted) continue;

            std::fill (v.mixL.begin(), v.mixL.begin() + numSamples, 0.0f);
            std::fill (v.mixR.begin(), v.mixR.begin() + numSamples, 0.0f);

            const double ratio = v.pitchRatio;
            const bool shifting = std::abs (ratio - 1.0) > 1.0e-5;

            for (int i = 0; i < numSamples; ++i)
            {
                if (v.position >= static_cast<double> (hit.numFrames)) break;
                float sampleL = 0.0f, sampleR = 0.0f;

                if (shifting)
                {
                    const int64_t pos = static_cast<int64_t> (v.position);
                    const float t = static_cast<float> (v.position - static_cast<double> (pos));
                    for (const auto& channelRef : hit.channels)
                    {
                        const float y0 = channelRef.handle.readSample (pos - 1, channelRef.channel);
                        const float y1 = channelRef.handle.readSample (pos,     channelRef.channel);
                        const float y2 = channelRef.handle.readSample (pos + 1, channelRef.channel);
                        const float y3 = channelRef.handle.readSample (pos + 2, channelRef.channel);

                        const float c1 = 0.5f * (y2 - y0);
                        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                        const float sample = ((c3 * t + c2) * t + c1) * t + y1;

                        sampleL += sample * channelRef.gainL;
                        sampleR += sample * channelRef.gainR;
                    }
                }
                else
                {
                    const int64_t pos = static_cast<int64_t> (v.position);
                    for (const auto& channelRef : hit.channels)
                    {
                        const float sample = channelRef.handle.readSample (pos, channelRef.channel);
                        sampleL += sample * channelRef.gainL;
                        sampleR += sample * channelRef.gainR;
                    }
                }

                v.mixL[static_cast<size_t> (i)] = sampleL;
                v.mixR[static_cast<size_t> (i)] = sampleR;
                v.position += ratio;
            }

            if (v.position >= static_cast<double> (hit.numFrames))
                v.active = false;

            float panL = 1.0f, panR = 1.0f;
            if (p.pan != 0.0f)
                panGains (p.pan, panL, panR);
            const auto gainL = v.velocity * p.level * panL * masterGainL;
            const auto gainR = v.velocity * p.level * panR * masterGainR;

            float* destL = output[targetBus * 2];
            float* destR = output[targetBus * 2 + 1];
            if (destL == nullptr || destR == nullptr)
            {
                destL = output[0];
                destR = output[1];
            }
            if (destL == nullptr || destR == nullptr)
                continue;

            const auto first = static_cast<size_t> (std::min<int64_t> (v.startSkip, numSamples));
            v.startSkip = std::max<int64_t> (0, v.startSkip - numSamples);
            const bool fading = v.attackSamples > 0 && v.attackGain < 1.0f;

            for (int i = static_cast<int> (first); i < numSamples; ++i)
            {
                auto gain = 1.0f;
                if (fading)
                {
                    v.attackGain = std::min (1.0f, v.attackGain + 1.0f / static_cast<float> (v.attackSamples));
                    gain = v.attackGain;
                }

                if (v.endsAt > 0 && v.chokeFadeSamples > 0)
                {
                    const auto elapsed = (sampleClock + i) - v.chokeStartSample;
                    if (elapsed >= v.chokeFadeSamples)
                        gain = 0.0f;
                    else if (elapsed >= 0)
                    {
                        const float t = static_cast<float> (elapsed) / static_cast<float> (v.chokeFadeSamples);
                        gain *= 0.5f * (1.0f + std::cos (static_cast<float> (pi) * t));
                    }
                }
                else if (v.endsAt > 0)
                {
                    const auto remaining = v.endsAt - (sampleClock + i);
                    if (remaining <= 0)
                        gain = 0.0f;
                    else if (remaining < static_cast<int64_t> (currentSampleRate * 0.015))
                    {
                        const float t = static_cast<float> (remaining) / static_cast<float> (currentSampleRate * 0.015);
                        gain *= 0.5f * (1.0f - std::cos (static_cast<float> (pi) * t));
                    }
                }

                const auto index = static_cast<size_t> (i);
                destL[i] += v.mixL[index] * gain * gainL;
                destR[i] += v.mixR[index] * gain * gainR;
            }
        }
    }

    sampleClock = endOfBlock;
}

} // namespace glorp
