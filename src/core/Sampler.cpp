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
int Sampler::windowLatency = 0;

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
    windowLatency = 0;

    for (auto& v : pool.voices)
    {
        v->shifter[0].setSearchRadius (192);
        v->shifter[1].setSearchRadius (192);
        v->shifter[0].prepare (currentSampleRate, currentBlockSize);
        v->shifter[1].prepare (currentSampleRate, currentBlockSize);
        v->shifterPrepared = true;
    }

    recomputeLatency();
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

    if (resolved.empty())
    {
        error = "None of this kit's sample files could be read. Check the kit directory is still in place.";
        return false;
    }

    instruments_ = std::move (resolved);
    kitChannels_ = kit.channels;
    instrumentCount = static_cast<int> (instruments_.size());

    // The number of buses in use is bounded by what was built; anything beyond
    // that shares the last bus rather than being dropped.
    busCount = std::min (instrumentCount, GLORP_MAX_INSTRUMENT_BUSES);
    kitTitle = kit.title;

    channelParams.assign (static_cast<size_t> (instrumentCount), ChannelParams {});
    roundRobinCursor.assign (static_cast<size_t> (instrumentCount), 0);

    // A kit with more instruments than buses is a hard error, not a silent
    // truncation, so surface it rather than hiding some of the kit.
    if (missingFiles > 0)
    {
        error = juce::String (missingFiles) + " of " + juce::String (kit.instruments.size())
              + " instruments had unreadable sample files and were skipped.";
    }

    recomputeLatency();
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
    instrumentCount = 0;
    busCount = 0;
    kitTitle.clear();

    anySolo.store (false);
    reportedLatency.store (0);
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
    }

    anySolo.store (false);
    recomputeSoloFlags();
    recomputeLatency();
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

void Sampler::recomputeLatency()
{
    // Only instruments that are actually tuned contribute latency. Taking the
    // maximum keeps host compensation valid for every bus; an untuned bus simply
    // is not delayed.
    if (windowLatency == 0)
    {
        PitchShifter probe;
        probe.prepare (currentSampleRate, currentBlockSize);
        probe.setSemitones (1.0f);
        windowLatency = probe.latencySamples();
    }

    int worst = 0;

    for (const auto& p : channelParams)
        if (p.pitch != 0.0f)
            worst = std::max (worst, windowLatency);

    reportedLatency.store (worst);
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

    p.level = std::max (0.0f, params.level);
    p.pan = std::clamp (params.pan, -1.0f, 1.0f);
    p.pitch = std::clamp (params.pitchSemitones, -PitchShifter::maxSemitones, PitchShifter::maxSemitones);
    p.spread = std::clamp (params.tuneSpread, 0.0f, 100.0f);
    p.roundRobin = params.autoRoundRobin;
    p.selectedSample = params.selectedSample;
    p.mute = params.mute;
    p.solo = params.solo;

    anySolo.store (anySolo.load() || params.solo, std::memory_order_relaxed);
    recomputeSoloFlags();
    recomputeLatency();
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
    out.autoRoundRobin = p.roundRobin;
    out.mute = p.mute;
    out.solo = p.solo;
    out.selectedSample = p.selectedSample;

    return out;
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

        // Keep a steady perceived level whether an instrument has one channel or
        // eight, so swapping kits does not change how loud everything is.
        const auto normalisation = 1.0f / std::sqrt (static_cast<float> (channelCount));

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
            else if (c % 2 == 0)
            {
                channelRef.gainL = normalisation;
                channelRef.gainR = 0.0f;
            }
            else
            {
                channelRef.gainL = 0.0f;
                channelRef.gainR = normalisation;
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

int Sampler::chooseHit (int instrumentIndex)
{
    if (instrumentIndex < 0 || instrumentIndex >= instrumentCount)
        return 0;

    const auto numHits = static_cast<int> (instruments_[static_cast<size_t> (instrumentIndex)].hits.size());

    if (numHits <= 0)
        return 0;

    const auto& p = channelParams[static_cast<size_t> (instrumentIndex)];

    if (! p.roundRobin)
        return std::clamp (p.selectedSample, 0, numHits - 1);

    auto& cursor = roundRobinCursor[static_cast<size_t> (instrumentIndex)];
    const auto chosen = cursor % numHits;
    cursor = (cursor + 1) % numHits;

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

    auto* voice = allocateVoice();

    if (voice == nullptr)
        return;

    const auto& p = channelParams[static_cast<size_t> (instrument)];

    if (p.muted)
        return;

    const auto hitIndex = chooseHit (instrument);
    const auto& hit = runtime.hits[static_cast<size_t> (hitIndex)];

    float pitch = p.pitch;
    float level = velocityWithCurve (velocity, globals.velCurve);

    // Humanise: a little velocity scatter and a touch of tuning drift, which is
    // what stops repeated hits from sounding machine-gunned.
    if (globals.humanize > 0.0f)
    {
        level *= 1.0f + nextRandom() * 0.15f * globals.humanize;
        pitch += nextRandom() * p.spread * globals.humanize;
    }
    else if (p.spread > 0.0f)
    {
        pitch += nextRandom() * p.spread;
    }

    pitch = std::clamp (pitch, -PitchShifter::maxSemitones, PitchShifter::maxSemitones);

    voice->active = true;
    voice->instrument = instrument;
    voice->hit = hitIndex;
    voice->position = 0;
    voice->velocity = std::clamp (level, 0.0f, 2.0f);
    voice->pitchSemitones = pitch;
    voice->startedAt = timeInSamples;
    voice->endsAt = 0;
    voice->attackGain = globals.attackMs > 0.0f ? 0.0f : 1.0f;
    voice->attackSamples = static_cast<int> (globals.attackMs * 0.001 * currentSampleRate);

    // Discard output so the hit starts exactly where the note asked for.
    voice->startSkip = std::max<int64_t> (0, timeInSamples - sampleClock);

    voice->shifter[0].reset();
    voice->shifter[1].reset();
    voice->shifter[0].setSemitones (pitch);
    voice->shifter[1].setSemitones (pitch);

    const auto naturalEnd = timeInSamples + hit.numFrames;

    voice->endsAtAbsolute = naturalEnd;

    // The decay control fades the tail out sooner, or lets it ring on.
    if (globals.decayScale < 0.999f)
        voice->endsAt = timeInSamples + static_cast<int64_t> (static_cast<double> (hit.numFrames) * globals.decayScale);
    else
        voice->endsAt = 0;

    if (voice->mixL.size() < static_cast<size_t> (currentBlockSize))
    {
        voice->mixL.resize (static_cast<size_t> (currentBlockSize));
        voice->mixR.resize (static_cast<size_t> (currentBlockSize));
        voice->shiftedL.resize (static_cast<size_t> (currentBlockSize));
        voice->shiftedR.resize (static_cast<size_t> (currentBlockSize));
        voice->channelScratch.resize (static_cast<size_t> (currentBlockSize));
    }

    // A tuned voice has to run past the end of the sample to flush its window.
    if (pitch != 0.0f)
        voice->endsAtAbsolute = naturalEnd + voice->shifter[0].latencySamples() + 2;

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

        const auto bus = busForInstrument (v.instrument);

        if (bus < 0 || bus >= totalBuses)
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

        // Make sure the voice scratch is big enough for this block.
        if (v.mixL.size() < static_cast<size_t> (numSamples))
        {
            const auto size = static_cast<size_t> (numSamples);
            v.mixL.resize (size);
            v.mixR.resize (size);
            v.shiftedL.resize (size);
            v.shiftedR.resize (size);
            v.channelScratch.resize (size);
        }

        // 1. Sum the instrument's channels into a stereo pair. This happens
        //    before tuning so the shifter only runs twice per voice rather than
        //    twice per sample channel.
        std::fill (v.mixL.begin(), v.mixL.begin() + numSamples, 0.0f);
        std::fill (v.mixR.begin(), v.mixR.begin() + numSamples, 0.0f);

        for (const auto& channelRef : hit.channels)
        {
            const auto remaining = hit.numFrames - v.position;

            if (remaining <= 0)
                break;

            const auto toRead = static_cast<int> (std::min<int64_t> (numSamples, remaining));

            if (toRead <= 0)
                continue;

            v.channelScratch.assign (static_cast<size_t> (toRead), 0.0f);
            channelRef.handle.read (v.position, toRead, channelRef.channel, v.channelScratch.data());

            if (channelRef.gainL != 0.0f)
                for (int i = 0; i < toRead; ++i)
                    v.mixL[static_cast<size_t> (i)] += v.channelScratch[static_cast<size_t> (i)] * channelRef.gainL;

            if (channelRef.gainR != 0.0f)
                for (int i = 0; i < toRead; ++i)
                    v.mixR[static_cast<size_t> (i)] += v.channelScratch[static_cast<size_t> (i)] * channelRef.gainR;
        }

        // 2. Tune, if this voice is shifted. At zero the shifter is a bit-exact
        //    bypass, so the common case costs nothing.
        const float* sourceL = v.mixL.data();
        const float* sourceR = v.mixR.data();

        if (v.pitchSemitones != 0.0f && v.shifterPrepared)
        {
            v.shifter[0].processMono (v.mixL.data(), numSamples, v.shiftedL.data(), numSamples);
            v.shifter[1].processMono (v.mixR.data(), numSamples, v.shiftedR.data(), numSamples);
            sourceL = v.shiftedL.data();
            sourceR = v.shiftedR.data();
        }

        // 3. Instrument pan and level, then the master, then accumulate.
        float panL = 0.0f, panR = 0.0f;
        panGains (p.pan, panL, panR);

        const auto gainL = v.velocity * p.level * panL * masterGainL;
        const auto gainR = v.velocity * p.level * panR * masterGainR;

        float* destL = output[bus * 2];
        float* destR = output[bus * 2 + 1];

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
