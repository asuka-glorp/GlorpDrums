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
        resolveInstrumentChannels (instrument, runtime);

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
    for (int i = 0; i < instrumentCount; ++i)
    {
        const auto n = instruments_[static_cast<size_t> (i)].name.toLowerCase();
        if (n.contains ("hat") || n.contains ("hh") || instruments_[static_cast<size_t> (i)].group.equalsIgnoreCase ("hihat"))
        {
            if (n.contains ("pedal") || n.contains ("foot"))
                hiHat.pedal = i;
            else if (n.contains ("semi"))
                hiHat.semiOpen = i;
            else if (n.contains ("open"))
                hiHat.open = i;
            else if (n.contains ("closed") || n.contains ("close"))
                hiHat.closed = i;
        }
    }

    for (int i = 0; i < instrumentCount; ++i)
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

    instruments_.clear();
    kitChannels_.clear();
    channelParams.clear();
    roundRobinCursor.clear();
    instrumentBuses.clear();
    instrumentCount = 0;
    busCount = 0;
    kitTitle.clear();

    anySolo.store (false);
    hiHat = {};
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
void Sampler::resolveInstrumentChannels (const Instrument& instrument, InstrumentRuntime& out)
{
    out.name = instrument.displayName;
    out.group = instrument.group;

    for (size_t s = 0; s < instrument.samples.size(); ++s)
    {
        const auto& sources = instrument.resolvedChannels[s];

        if (sources.empty())
            continue;

        ResolvedHit hit;
        hit.name = instrument.samples[s].name;
        hit.power = instrument.samples[s].power;

        const auto channelCount = sources.size();
        // Normalize to prevent clipping when summing multiple channels.
        // For 15-channel kits (Crocell), 1/sqrt(15)*3.2 = 0.826 per channel,
        // which sums to >12x overshoot. Use 1/N for >2 channels instead.
        const float kitGainFactor = (channelCount <= 2) ? 1.0f : 1.0f;
        const auto normalisation = (channelCount <= 2)
            ? (1.0f / std::sqrt (static_cast<float> (channelCount))) * kitGainFactor
            : (1.0f / static_cast<float> (channelCount));

        for (size_t c = 0; c < channelCount; ++c)
        {
            const auto& source = sources[c];
            const auto& ref = instrument.samples[s].audioFiles[static_cast<size_t> (source.audioFileIndex)];

            ChannelRef channelRef;
            channelRef.handle = sampleCache.open (instrument.samplePath (ref));
            channelRef.channel = ref.fileChannel;

            if (! channelRef.handle.valid)
                continue;

            if (channelCount == 1)
            {
                channelRef.gainL = normalisation;
                channelRef.gainR = normalisation;
            }
            else
            {
                const auto chName = juce::String (ref.channel).toLowerCase();
                if (chName.contains ("left") || chName.endsWith ("l") || chName.contains ("_l") || chName.contains (".l"))
                {
                    channelRef.gainL = normalisation * 1.414f;
                    channelRef.gainR = 0.0f;
                }
                else if (chName.contains ("right") || chName.endsWith ("r") || chName.contains ("_r") || chName.contains (".r"))
                {
                    channelRef.gainL = 0.0f;
                    channelRef.gainR = normalisation * 1.414f;
                }
                else
                {
                    channelRef.gainL = normalisation;
                    channelRef.gainR = normalisation;
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

    // E-drum continuous hi-hat articulation selection based on pedal CC#4
    if (note >= 0 && (instrument == hiHat.closed || instrument == hiHat.semiOpen || instrument == hiHat.open))
    {
        const float pedal = hiHatPedal.load (std::memory_order_relaxed);
        if (pedal >= 0.70f && hiHat.closed >= 0)
            instrument = hiHat.closed;
        else if (pedal >= 0.30f && hiHat.semiOpen >= 0)
            instrument = hiHat.semiOpen;
        else if (hiHat.open >= 0)
            instrument = hiHat.open;
    }

    const auto& runtime = instruments_[static_cast<size_t> (instrument)];

    if (runtime.hits.empty())
        return;

    // Hitting any member of a group silences the others, which is how DrumGizmo
    // handles open/closed hi-hats. Without it a closed hat piles on top of an
    // open one and neither is audible.
    if (runtime.group.isNotEmpty())
    {
        for (int i = 0; i < instrumentCount; ++i)
        {
            if (i == instrument)
                continue;

            if (instruments_[static_cast<size_t> (i)].group == runtime.group)
                chokeInstrument (i, 25, timeInSamples);
        }
    }

    for (const auto& target : runtime.chokeTargets)
    {
        if (target.instrumentIndex >= 0 && target.instrumentIndex < instrumentCount)
            chokeInstrument (target.instrumentIndex, static_cast<int64_t> (target.chokeTimeMs), timeInSamples);
    }

    auto* voice = allocateVoice();

    if (voice == nullptr)
        return;

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

    // Dynamics knob: controls the velocity response curve and sample layer selection.
    // At 0.5: natural 1:1 linear response.
    // Below 0.5: compressed dynamics (softer hits are brought up, punchy and consistent).
    // Above 0.5: expanded dynamics (exaggerated difference between soft and loud hits).
    if (std::abs (p.dynamics - 0.5f) > 0.01f)
    {
        const float dynExp = std::pow (2.0f, (p.dynamics - 0.5f) * 3.0f);
        effectiveVel = std::pow (std::clamp (effectiveVel, 0.01f, 1.0f), dynExp);
    }

    const auto hitIndex = chooseHit (instrument, effectiveVel);
    const auto& hit = runtime.hits[static_cast<size_t> (hitIndex)];

    float pitch = p.pitch;
    float level = velocityWithCurve (effectiveVel, globals.velCurve);

    // Humanise & subtle random drift:
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

    // Sample-rate conversion via varispeed: if the kit was recorded at a
    // different rate than the host, fold the ratio into the playback speed.
    // A 48k kit at 44.1k host plays at 48/44.1 = 1.088x speed, which raises
    // the pitch by exactly the right amount to compensate.
    const double rateRatio = (kitSampleRate > 0.0 && currentSampleRate > 0.0)
                                 ? kitSampleRate / currentSampleRate
                                 : 1.0;
    voice->pitchRatio = std::pow (2.0, pitch / 12.0) * rateRatio;

    voice->velocity = std::clamp (level, 0.0f, 2.0f);
    voice->pitchSemitones = pitch;
    voice->startedAt = timeInSamples;
    voice->endsAt = 0;
    voice->attackGain = globals.attackMs > 0.0f ? 0.0f : 1.0f;
    voice->attackSamples = static_cast<int> (globals.attackMs * 0.001 * currentSampleRate);

    // Discard output so the hit starts exactly where the note asked for.
    voice->startSkip = std::max<int64_t> (0, timeInSamples - sampleClock);

    // Varispeed: output duration = input duration / pitchRatio.
    const auto naturalEnd = timeInSamples + static_cast<int64_t> (static_cast<double> (hit.numFrames) / voice->pitchRatio);

    voice->endsAtAbsolute = naturalEnd;

    // The decay control fades the tail out sooner, or lets it ring on.
    if (globals.decayScale < 0.999f)
        voice->endsAt = timeInSamples + static_cast<int64_t> (static_cast<double> (hit.numFrames) * globals.decayScale / voice->pitchRatio);
    else
        voice->endsAt = 0;

    pushHitEvent ({ instrument, note, voice->velocity, pitch });
}

void Sampler::chokeInstrument (int instrument, int64_t fadeMs, int64_t timeInSamples)
{
    const auto fadeSamples = static_cast<int64_t> (static_cast<double> (fadeMs) * currentSampleRate * 0.001);

    for (auto& voicePtr : pool.voices)
    {
        auto& v = *voicePtr;

        if (v.active && v.instrument == instrument)
            v.endsAt = timeInSamples + std::max<int64_t> (1, fadeSamples);
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
    }
}

void Sampler::setHiHatPedal (float pedalValue) noexcept
{
    const float clamped = std::clamp (pedalValue, 0.0f, 1.0f);
    const float prev = hiHatPedal.exchange (clamped, std::memory_order_relaxed);

    // Choke open hi-hats when pedal moves from open towards closed
    if (prev < 0.55f && clamped >= 0.60f)
    {
        if (hiHat.open >= 0)
            chokeInstrument (hiHat.open, 30, sampleClock);
        if (hiHat.semiOpen >= 0)
            chokeInstrument (hiHat.semiOpen, 30, sampleClock);

        // Firm downward foot stomp: trigger pedal chick
        if (clamped - prev > 0.30f && clamped >= 0.80f)
        {
            const int chickInst = hiHat.pedal >= 0 ? hiHat.pedal : hiHat.closed;
            if (chickInst >= 0)
            {
                const float vel = std::clamp ((clamped - prev) * 1.5f, 0.35f, 1.0f);
                noteOn (chickInst, -1, vel, sampleClock);
            }
        }
    }
}

void Sampler::chokeCymbal (int instrumentIndex, int64_t fadeMs, int64_t timeInSamples)
{
    if (instrumentIndex < 0 || instrumentIndex >= instrumentCount)
        return;

    chokeInstrument (instrumentIndex, fadeMs, timeInSamples);

    const auto name = instruments_[static_cast<size_t> (instrumentIndex)].name.toLowerCase();
    juce::String stem = name;
    for (const char* suffix : { "edge", "bow", "bell", "choke", "crash", "ride", "splash", "china" })
    {
        if (stem.endsWith (suffix) && stem.length() > static_cast<int> (std::strlen (suffix)))
        {
            stem = stem.dropLastCharacters (static_cast<int> (std::strlen (suffix)));
            break;
        }
    }

    for (int i = 0; i < instrumentCount; ++i)
    {
        if (i == instrumentIndex)
            continue;

        const auto otherName = instruments_[static_cast<size_t> (i)].name.toLowerCase();
        if (otherName.startsWith (stem))
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
            juce::FloatVectorOperations::clear (output[c], numSamples);

        return;
    }

    KitUnlock unlock { kitLock };

    const auto totalBuses = numOutputChannels / 2;
    const auto activeBuses = std::min (busCount, totalBuses);
    // Clear everything first: unused buses must be silent, and voices
    // accumulate into the ones that are used.
    for (int c = 0; c < numOutputChannels; ++c)
        juce::FloatVectorOperations::clear (output[c], numSamples);

    if (! kitLoaded.load() || activeBuses <= 0)
    {
        sampleClock += numSamples;
        return;
    }

    float masterL = 0.0f, masterR = 0.0f;
    panGains (globals.masterPan, masterL, masterR);

    const auto masterGainL = globals.masterLevel * masterL;
    const auto masterGainR = globals.masterLevel * masterR;

    const auto endOfBlock = sampleClock + numSamples;

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

        int targetBus = 0;
        if (multiOutMode.load() && totalBuses > 1)
        {
            const auto b = busForInstrument (v.instrument);
            targetBus = (b >= 0 && b < totalBuses) ? b : 0;
        }

        const auto& runtime = instruments_[static_cast<size_t> (v.instrument)];

        if (v.hit < 0 || v.hit >= static_cast<int> (runtime.hits.size()))
        {
            v.active = false;
            continue;
        }

        // Expired: either the sample ran out or a choke finished.
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

        // Scratch buffers are preallocated in setSampleRateAndBlock().
        // No allocation on the audio thread.

        // 1. Sum the instrument's channels into a stereo pair, resampling at
        //    the varispeed ratio. Cubic interpolation preserves transients far
        //    better than WSOLA for drum sounds, and introduces zero latency.
        std::fill (v.mixL.begin(), v.mixL.begin() + numSamples, 0.0f);
        std::fill (v.mixR.begin(), v.mixR.begin() + numSamples, 0.0f);

        const double ratio = v.pitchRatio;
        const bool shifting = std::abs (ratio - 1.0) > 1.0e-5;

        for (int i = 0; i < numSamples; ++i)
        {
            if (v.position >= static_cast<double> (hit.numFrames))
                break;

            float sampleL = 0.0f, sampleR = 0.0f;

            if (shifting)
            {
                // Cubic Hermite interpolation at fractional position
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
                // Fast path: 1:1 speed, direct exact sample read
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

        const float* sourceL = v.mixL.data();
        const float* sourceR = v.mixR.data();

        // 3. Instrument pan and level, then the master, then accumulate.
        float panL = 0.0f, panR = 0.0f;
        panGains (p.pan, panL, panR);

        const auto gainL = v.velocity * p.level * panL * masterGainL;
        const auto gainR = v.velocity * p.level * panR * masterGainR;

        float* destL = output[targetBus * 2];
        float* destR = output[targetBus * 2 + 1];

        const auto first = static_cast<size_t> (std::min<int64_t> (v.startSkip, numSamples));
        v.startSkip = std::max<int64_t> (0, v.startSkip - numSamples);

        // Attack fade-in.
        const bool fading = v.attackSamples > 0 && v.attackGain < 1.0f;

        for (int i = static_cast<int> (first); i < numSamples; ++i)
        {
            auto gain = 1.0f;

            if (fading)
            {
                v.attackGain = std::min (1.0f, v.attackGain + 1.0f / static_cast<float> (v.attackSamples));
                gain = v.attackGain;
            }

            // Choke and decay fade-outs ramp down over a few milliseconds rather
            // than cutting, so a closing hi-hat does not click.
            if (v.endsAt > 0)
            {
                const auto remaining = v.endsAt - (sampleClock + i);

                if (remaining < static_cast<int64_t> (currentSampleRate * 0.005))
                    gain *= std::max (0.0f, static_cast<float> (remaining)
                                            / static_cast<float> (currentSampleRate * 0.005));
            }

            const auto index = static_cast<size_t> (i);

            destL[i] += sourceL[index] * gain * gainL;
            destR[i] += sourceR[index] * gain * gainR;
        }
    }

    sampleClock = endOfBlock;
}

} // namespace glorp
