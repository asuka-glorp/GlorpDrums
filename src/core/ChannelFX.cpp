/*
    ChannelFX.cpp - Implementation of per-mic channel strip DSP and mixer presets.
*/

#include "ChannelFX.h"
#include "XmlLite.h"

namespace glorp
{

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    inline float dbToGain (float db) noexcept
    {
        return std::pow (10.0f, db * 0.05f);
    }

    inline float softClip (float x) noexcept
    {
        // Transparent soft knee starting at |x| > 0.85, asymptotes to 1.0
        const float ax = std::abs (x);
        if (ax <= 0.85f)
            return x;

        const float sign = x < 0.0f ? -1.0f : 1.0f;
        const float excess = ax - 0.85f;
        return sign * (0.85f + 0.15f * std::tanh (excess / 0.15f));
    }
}

//==============================================================================
// ChannelStripFX
//==============================================================================

ChannelStripFX::ChannelStripFX() = default;
ChannelStripFX::~ChannelStripFX() = default;

void ChannelStripFX::prepare (double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentBlockSize = std::max (16, maxBlockSize);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (currentBlockSize);
    spec.numChannels = 2;

    compressor.prepare (spec);
    compressor.setThreshold (lastCompThresh);
    compressor.setRatio (lastCompRatio);
    compressor.setAttack (lastCompAttack);
    compressor.setRelease (lastCompRelease);

    // Envelope follower coefficients for transient shaper
    // Fast: attack ~2ms, release ~25ms
    fastAttackCoeff  = std::exp (-1.0f / (static_cast<float> (currentSampleRate) * 0.002f));
    fastReleaseCoeff = std::exp (-1.0f / (static_cast<float> (currentSampleRate) * 0.025f));

    // Slow: attack ~30ms, release ~150ms
    slowAttackCoeff  = std::exp (-1.0f / (static_cast<float> (currentSampleRate) * 0.030f));
    slowReleaseCoeff = std::exp (-1.0f / (static_cast<float> (currentSampleRate) * 0.150f));

    reset();
}

void ChannelStripFX::reset()
{
    lowShelfL.reset();  lowShelfR.reset();
    midPeakL.reset();   midPeakR.reset();
    highShelfL.reset(); highShelfR.reset();

    compressor.reset();

    fastEnvL = 0.0f; fastEnvR = 0.0f;
    slowEnvL = 0.0f; slowEnvR = 0.0f;

    // Force coefficient recalculation on next block
    lastLowGain = -999.0f;
}

void ChannelStripFX::updateFilters (const MicStripParams& params)
{
    if (std::abs (params.eqLowGainDb - lastLowGain) > 0.05f)
    {
        lastLowGain = params.eqLowGainDb;
        const auto gainLinear = dbToGain (lastLowGain);
        auto c = juce::dsp::IIR::Coefficients<float>::makeLowShelf (currentSampleRate, 80.0f, 0.707f, gainLinear);
        lowShelfL.coefficients = c;
        lowShelfR.coefficients = c;
    }

    if (std::abs (params.eqMidGainDb - lastMidGain) > 0.05f
        || std::abs (params.eqMidFreq - lastMidFreq) > 1.0f
        || std::abs (params.eqMidQ - lastMidQ) > 0.02f)
    {
        lastMidGain = params.eqMidGainDb;
        lastMidFreq = std::clamp (params.eqMidFreq, 100.0f, 10000.0f);
        lastMidQ    = std::clamp (params.eqMidQ, 0.2f, 5.0f);
        const auto gainLinear = dbToGain (lastMidGain);
        auto c = juce::dsp::IIR::Coefficients<float>::makePeakFilter (currentSampleRate, lastMidFreq, lastMidQ, gainLinear);
        midPeakL.coefficients = c;
        midPeakR.coefficients = c;
    }

    if (std::abs (params.eqHighGainDb - lastHighGain) > 0.05f)
    {
        lastHighGain = params.eqHighGainDb;
        const auto gainLinear = dbToGain (lastHighGain);
        auto c = juce::dsp::IIR::Coefficients<float>::makeHighShelf (currentSampleRate, 10000.0f, 0.707f, gainLinear);
        highShelfL.coefficients = c;
        highShelfR.coefficients = c;
    }

    if (params.compEnabled)
    {
        if (std::abs (params.compThresholdDb - lastCompThresh) > 0.1f)
        {
            lastCompThresh = params.compThresholdDb;
            compressor.setThreshold (lastCompThresh);
        }
        if (std::abs (params.compRatio - lastCompRatio) > 0.1f)
        {
            lastCompRatio = params.compRatio;
            compressor.setRatio (lastCompRatio);
        }
        if (std::abs (params.compAttackMs - lastCompAttack) > 0.5f)
        {
            lastCompAttack = params.compAttackMs;
            compressor.setAttack (lastCompAttack);
        }
        if (std::abs (params.compReleaseMs - lastCompRelease) > 1.0f)
        {
            lastCompRelease = params.compReleaseMs;
            compressor.setRelease (lastCompRelease);
        }
    }
}

void ChannelStripFX::process (float* left, float* right, int numSamples,
                              const MicStripParams& params,
                              float* reverbAccumL, float* reverbAccumR)
{
    if (numSamples <= 0)
        return;

    // 1. Polarity / Phase Invert
    if (params.phaseInvert)
    {
        juce::FloatVectorOperations::multiply (left, -1.0f, numSamples);
        juce::FloatVectorOperations::multiply (right, -1.0f, numSamples);
    }

    if (! params.fxEnabled)
        return;

    updateFilters (params);

    // 2. 3-Band Parametric EQ
    if (params.eqEnabled)
    {
        if (std::abs (lastLowGain) > 0.05f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                left[i]  = lowShelfL.processSample (left[i]);
                right[i] = lowShelfR.processSample (right[i]);
            }
        }

        if (std::abs (lastMidGain) > 0.05f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                left[i]  = midPeakL.processSample (left[i]);
                right[i] = midPeakR.processSample (right[i]);
            }
        }

        if (std::abs (lastHighGain) > 0.05f)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                left[i]  = highShelfL.processSample (left[i]);
                right[i] = highShelfR.processSample (right[i]);
            }
        }
    }

    // 3. VCA Compressor
    if (params.compEnabled)
    {
        float* channels[2] = { left, right };
        juce::dsp::AudioBlock<float> block (channels, 2, static_cast<size_t> (numSamples));
        juce::dsp::ProcessContextReplacing<float> context (block);
        compressor.process (context);

        if (params.compMakeupDb > 0.05f)
        {
            const float makeup = dbToGain (params.compMakeupDb);
            juce::FloatVectorOperations::multiply (left, makeup, numSamples);
            juce::FloatVectorOperations::multiply (right, makeup, numSamples);
        }
    }

    // 4. Tape / Tube Saturation (harmonic waveshaper)
    if (params.saturationDrive > 0.01f)
    {
        const float driveAmount = 1.0f + 2.5f * params.saturationDrive;
        const float normFactor = 1.0f / (1.0f + 1.2f * params.saturationDrive);

        for (int i = 0; i < numSamples; ++i)
        {
            left[i]  = std::tanh (left[i]  * driveAmount) * normFactor;
            right[i] = std::tanh (right[i] * driveAmount) * normFactor;
        }
    }

    // 5. Transient Shaper (attack punch & sustain body)
    if (std::abs (params.transientAttack) > 0.01f || std::abs (params.transientSustain) > 0.01f)
    {
        const float attCoeff = params.transientAttack;
        const float susCoeff = params.transientSustain;

        for (int i = 0; i < numSamples; ++i)
        {
            const float inAbsL = std::abs (left[i]);
            const float inAbsR = std::abs (right[i]);

            // Left envelope followers
            if (inAbsL > fastEnvL)
                fastEnvL = inAbsL + fastAttackCoeff * (fastEnvL - inAbsL);
            else
                fastEnvL = inAbsL + fastReleaseCoeff * (fastEnvL - inAbsL);

            if (inAbsL > slowEnvL)
                slowEnvL = inAbsL + slowAttackCoeff * (slowEnvL - inAbsL);
            else
                slowEnvL = inAbsL + slowReleaseCoeff * (slowEnvL - inAbsL);

            // Right envelope followers
            if (inAbsR > fastEnvR)
                fastEnvR = inAbsR + fastAttackCoeff * (fastEnvR - inAbsR);
            else
                fastEnvR = inAbsR + fastReleaseCoeff * (fastEnvR - inAbsR);

            if (inAbsR > slowEnvR)
                slowEnvR = inAbsR + slowAttackCoeff * (slowEnvR - inAbsR);
            else
                slowEnvR = inAbsR + slowReleaseCoeff * (slowEnvR - inAbsR);

            const float diffL = std::max (0.0f, fastEnvL - slowEnvL);
            const float diffR = std::max (0.0f, fastEnvR - slowEnvR);

            const float gainL = std::clamp (1.0f + attCoeff * 2.0f * (diffL / (fastEnvL + 0.01f))
                                                 + susCoeff * 1.2f * (slowEnvL / (fastEnvL + 0.01f)),
                                            0.1f, 3.0f);
            const float gainR = std::clamp (1.0f + attCoeff * 2.0f * (diffR / (fastEnvR + 0.01f))
                                                 + susCoeff * 1.2f * (slowEnvR / (fastEnvR + 0.01f)),
                                            0.1f, 3.0f);

            left[i]  *= gainL;
            right[i] *= gainR;
        }
    }

    // 6. Reverb Send Accumulation
    if (params.reverbSend > 0.005f && reverbAccumL != nullptr && reverbAccumR != nullptr)
    {
        const float sendGain = params.reverbSend * 0.707f;
        for (int i = 0; i < numSamples; ++i)
        {
            reverbAccumL[i] += left[i]  * sendGain;
            reverbAccumR[i] += right[i] * sendGain;
        }
    }
}

//==============================================================================
// MasterFX
//==============================================================================

MasterFX::MasterFX() = default;
MasterFX::~MasterFX() = default;

void MasterFX::prepare (double sampleRate, int maxBlockSize)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    currentBlockSize = std::max (16, maxBlockSize);

    reverbWetL.resize (static_cast<size_t> (currentBlockSize));
    reverbWetR.resize (static_cast<size_t> (currentBlockSize));

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = currentSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (currentBlockSize);
    spec.numChannels = 2;

    reverb.prepare (spec);
    reset();
}

void MasterFX::reset()
{
    reverb.reset();
}

void MasterFX::process (float* masterL, float* masterR, int numSamples,
                        const float* reverbSendL, const float* reverbSendR,
                        const MasterStripParams& params)
{
    if (numSamples <= 0)
        return;

    // 1. Process algorithmic reverb
    if (params.reverbReturn > 0.01f && reverbSendL != nullptr && reverbSendR != nullptr)
    {
        std::copy (reverbSendL, reverbSendL + numSamples, reverbWetL.data());
        std::copy (reverbSendR, reverbSendR + numSamples, reverbWetR.data());

        juce::dsp::Reverb::Parameters rParams;
        rParams.roomSize   = std::clamp (params.reverbSize, 0.0f, 1.0f);
        rParams.damping    = std::clamp (params.reverbDamp, 0.0f, 1.0f);
        rParams.wetLevel   = 1.0f;
        rParams.dryLevel   = 0.0f;
        rParams.width      = std::clamp (params.reverbWidth, 0.0f, 1.0f);
        rParams.freezeMode = 0.0f;

        if (rParams.roomSize != lastReverbParams.roomSize
            || rParams.damping != lastReverbParams.damping
            || rParams.width != lastReverbParams.width)
        {
            lastReverbParams = rParams;
            reverb.setParameters (rParams);
        }

        float* revChannels[2] = { reverbWetL.data(), reverbWetR.data() };
        juce::dsp::AudioBlock<float> revBlock (revChannels, 2, static_cast<size_t> (numSamples));
        juce::dsp::ProcessContextReplacing<float> revContext (revBlock);
        reverb.process (revContext);

        const float wetGain = params.reverbReturn;
        for (int i = 0; i < numSamples; ++i)
        {
            masterL[i] += reverbWetL[static_cast<size_t> (i)] * wetGain;
            masterR[i] += reverbWetR[static_cast<size_t> (i)] * wetGain;
        }
    }

    // 2. Master pan and level (balance panning for stereo bus)
    float panL = 1.0f, panR = 1.0f;
    if (params.pan < 0.0f)
    {
        panL = 1.0f;
        panR = 1.0f + params.pan;
    }
    else if (params.pan > 0.0f)
    {
        panL = 1.0f - params.pan;
        panR = 1.0f;
    }

    const float finalGainL = params.level * panL;
    const float finalGainR = params.level * panR;

    for (int i = 0; i < numSamples; ++i)
    {
        float outL = masterL[i] * finalGainL;
        float outR = masterR[i] * finalGainR;

        if (params.limiterEnabled)
        {
            outL = softClip (outL);
            outR = softClip (outR);
        }

        masterL[i] = outL;
        masterR[i] = outR;
    }
}

//==============================================================================
// Factory Mixer Presets
//==============================================================================

MixerPreset MixerPreset::createDefaultClean (int numMics, const std::vector<juce::String>& names)
{
    MixerPreset p;
    p.name = "Default Clean";
    p.master.level = 0.85f;
    p.master.limiterEnabled = true;
    p.master.reverbReturn = 0.20f;
    p.master.reverbSize = 0.45f;
    p.micNames = names;

    p.micStrips.resize (static_cast<size_t> (numMics));
    for (size_t i = 0; i < static_cast<size_t> (numMics); ++i)
    {
        p.micStrips[i].level = 1.0f;
        p.micStrips[i].pan = 0.0f;
        p.micStrips[i].eqEnabled = true;
        p.micStrips[i].compEnabled = false;
        p.micStrips[i].saturationDrive = 0.0f;
        p.micStrips[i].transientAttack = 0.0f;
        p.micStrips[i].transientSustain = 0.0f;
        p.micStrips[i].reverbSend = 0.15f;
    }
    return p;
}

MixerPreset MixerPreset::createMassiveRock (int numMics, const std::vector<juce::String>& names)
{
    MixerPreset p;
    p.name = "Massive Rock";
    p.master.level = 0.88f;
    p.master.limiterEnabled = true;
    p.master.reverbReturn = 0.35f;
    p.master.reverbSize = 0.65f;
    p.master.reverbDamp = 0.30f;
    p.micNames = names;

    p.micStrips.resize (static_cast<size_t> (numMics));
    for (size_t i = 0; i < static_cast<size_t> (numMics); ++i)
    {
        const auto n = (i < names.size()) ? names[i].toLowerCase() : juce::String();
        auto& s = p.micStrips[i];
        s.level = 1.0f;
        s.eqEnabled = true;

        if (n.contains ("kick") || n.contains ("kdrum"))
        {
            s.level = 1.15f;
            s.eqLowGainDb = 4.5f;     // Sub punch
            s.eqMidGainDb = -3.5f;    // Scoop boxy 400Hz
            s.eqMidFreq = 400.0f;
            s.eqHighGainDb = 3.0f;    // Beater click
            s.compEnabled = true;
            s.compThresholdDb = -16.0f;
            s.compRatio = 4.0f;
            s.compAttackMs = 25.0f;   // Let punch through
            s.compReleaseMs = 80.0f;
            s.compMakeupDb = 2.5f;
            s.saturationDrive = 0.15f;
            s.transientAttack = 0.25f;
            s.reverbSend = 0.05f;
        }
        else if (n.contains ("snare"))
        {
            s.level = 1.10f;
            s.eqLowGainDb = 2.0f;     // Low body
            s.eqMidGainDb = 2.5f;     // 1.2k crack
            s.eqMidFreq = 1200.0f;
            s.eqHighGainDb = 4.0f;    // Air sizzle
            s.compEnabled = true;
            s.compThresholdDb = -14.0f;
            s.compRatio = 4.0f;
            s.compAttackMs = 18.0f;
            s.compReleaseMs = 120.0f;
            s.compMakeupDb = 2.0f;
            s.saturationDrive = 0.25f; // Warm tube drive
            s.transientAttack = 0.35f; // Extra snare crack
            s.reverbSend = 0.30f;
            if (n.contains ("bottom") || n.contains ("snr_bot"))
                s.phaseInvert = true; // Invert bottom snare mic phase
        }
        else if (n.contains ("overhead") || n.contains ("oh"))
        {
            s.level = 0.95f;
            s.eqLowGainDb = -4.0f;    // High-pass rumble
            s.eqHighGainDb = 3.0f;    // Shimmer
            s.compEnabled = true;
            s.compThresholdDb = -18.0f;
            s.compRatio = 2.5f;
            s.saturationDrive = 0.10f;
            s.reverbSend = 0.20f;
        }
        else if (n.contains ("room") || n.contains ("amb"))
        {
            s.level = 1.15f;          // Loud hyped room
            s.eqLowGainDb = 3.0f;
            s.eqHighGainDb = -2.0f;
            s.compEnabled = true;     // Heavy room smashing
            s.compThresholdDb = -22.0f;
            s.compRatio = 6.0f;
            s.compAttackMs = 5.0f;
            s.compReleaseMs = 150.0f;
            s.compMakeupDb = 4.5f;
            s.saturationDrive = 0.30f;
            s.transientSustain = 0.35f;
            s.reverbSend = 0.40f;
        }
        else // Toms, Percussion
        {
            s.level = 1.0f;
            s.compEnabled = true;
            s.compThresholdDb = -15.0f;
            s.compRatio = 3.5f;
            s.transientAttack = 0.20f;
            s.reverbSend = 0.25f;
        }
    }
    return p;
}

MixerPreset MixerPreset::createVintageWarmth (int numMics, const std::vector<juce::String>& names)
{
    MixerPreset p;
    p.name = "Vintage Warmth";
    p.master.level = 0.85f;
    p.master.limiterEnabled = true;
    p.master.reverbReturn = 0.30f;
    p.master.reverbSize = 0.55f;
    p.master.reverbDamp = 0.60f;     // Dark warm reverb
    p.micNames = names;

    p.micStrips.resize (static_cast<size_t> (numMics));
    for (size_t i = 0; i < static_cast<size_t> (numMics); ++i)
    {
        auto& s = p.micStrips[i];
        s.level = 1.0f;
        s.eqEnabled = true;
        s.eqLowGainDb = 2.0f;         // Warm low end
        s.eqHighGainDb = -2.5f;       // Tamed tape top end
        s.saturationDrive = 0.35f;    // Generous tape drive
        s.compEnabled = true;
        s.compThresholdDb = -12.0f;
        s.compRatio = 2.0f;           // Gentle vintage glue
        s.compAttackMs = 30.0f;
        s.compReleaseMs = 150.0f;
        s.transientAttack = -0.10f;   // Rounded vintage transient
        s.transientSustain = 0.20f;   // Warm woody resonance
        s.reverbSend = 0.25f;
    }
    return p;
}

MixerPreset MixerPreset::createPunchyStudio (int numMics, const std::vector<juce::String>& names)
{
    MixerPreset p;
    p.name = "Punchy Studio";
    p.master.level = 0.86f;
    p.master.limiterEnabled = true;
    p.master.reverbReturn = 0.15f;
    p.master.reverbSize = 0.35f;     // Tight studio room
    p.master.reverbDamp = 0.40f;
    p.micNames = names;

    p.micStrips.resize (static_cast<size_t> (numMics));
    for (size_t i = 0; i < static_cast<size_t> (numMics); ++i)
    {
        const auto n = (i < names.size()) ? names[i].toLowerCase() : juce::String();
        auto& s = p.micStrips[i];
        s.level = 1.0f;
        s.eqEnabled = true;
        s.compEnabled = true;
        s.compThresholdDb = -15.0f;
        s.compRatio = 4.0f;
        s.compAttackMs = 15.0f;
        s.compReleaseMs = 90.0f;
        s.compMakeupDb = 1.5f;
        s.transientAttack = 0.40f;    // Extreme punchy attack!
        s.transientSustain = -0.15f;  // Tight gating tail
        s.saturationDrive = 0.08f;
        s.reverbSend = 0.10f;

        if (n.contains ("kick"))
        {
            s.eqLowGainDb = 3.0f;
            s.eqMidGainDb = -3.0f;
            s.eqMidFreq = 500.0f;
            s.eqHighGainDb = 2.5f;
        }
        else if (n.contains ("snare"))
        {
            s.eqLowGainDb = 1.5f;
            s.eqMidGainDb = 3.0f;
            s.eqMidFreq = 2000.0f;
            s.eqHighGainDb = 3.0f;
        }
    }
    return p;
}

MixerPreset MixerPreset::createDryTight (int numMics, const std::vector<juce::String>& names)
{
    MixerPreset p;
    p.name = "Dry & Tight";
    p.master.level = 0.85f;
    p.master.limiterEnabled = true;
    p.master.reverbReturn = 0.0f;     // Zero reverb!
    p.micNames = names;

    p.micStrips.resize (static_cast<size_t> (numMics));
    for (size_t i = 0; i < static_cast<size_t> (numMics); ++i)
    {
        const auto n = (i < names.size()) ? names[i].toLowerCase() : juce::String();
        auto& s = p.micStrips[i];
        s.level = 1.0f;
        s.eqEnabled = true;
        s.compEnabled = true;
        s.compThresholdDb = -16.0f;
        s.compRatio = 4.5f;
        s.compAttackMs = 10.0f;
        s.compReleaseMs = 60.0f;      // Fast recovery
        s.transientAttack = 0.30f;
        s.transientSustain = -0.40f;  // Deadened tight tails
        s.saturationDrive = 0.05f;
        s.reverbSend = 0.0f;

        if (n.contains ("room") || n.contains ("amb"))
        {
            s.level = 0.20f;          // Heavily duck room/ambience
            s.mute = false;
        }
    }
    return p;
}

//==============================================================================
// XML Serialization
//==============================================================================

juce::String MixerPreset::toXmlString() const
{
    juce::XmlElement xml ("GlorpMixPreset");
    xml.setAttribute ("name", name);
    xml.setAttribute ("version", "1.0");

    auto* mXml = xml.createNewChildElement ("Master");
    mXml->setAttribute ("level", static_cast<double> (master.level));
    mXml->setAttribute ("pan", static_cast<double> (master.pan));
    mXml->setAttribute ("limiter", master.limiterEnabled);
    mXml->setAttribute ("reverbReturn", static_cast<double> (master.reverbReturn));
    mXml->setAttribute ("reverbSize", static_cast<double> (master.reverbSize));
    mXml->setAttribute ("reverbDamp", static_cast<double> (master.reverbDamp));
    mXml->setAttribute ("reverbWidth", static_cast<double> (master.reverbWidth));

    for (size_t i = 0; i < micStrips.size(); ++i)
    {
        const auto& s = micStrips[i];
        auto* chXml = xml.createNewChildElement ("Channel");
        chXml->setAttribute ("index", static_cast<int> (i));
        if (i < micNames.size())
            chXml->setAttribute ("name", micNames[i]);

        chXml->setAttribute ("level", static_cast<double> (s.level));
        chXml->setAttribute ("pan", static_cast<double> (s.pan));
        chXml->setAttribute ("mute", s.mute);
        chXml->setAttribute ("solo", s.solo);
        chXml->setAttribute ("phaseInvert", s.phaseInvert);

        chXml->setAttribute ("eqEnabled", s.eqEnabled);
        chXml->setAttribute ("eqLowGainDb", static_cast<double> (s.eqLowGainDb));
        chXml->setAttribute ("eqMidGainDb", static_cast<double> (s.eqMidGainDb));
        chXml->setAttribute ("eqMidFreq", static_cast<double> (s.eqMidFreq));
        chXml->setAttribute ("eqMidQ", static_cast<double> (s.eqMidQ));
        chXml->setAttribute ("eqHighGainDb", static_cast<double> (s.eqHighGainDb));

        chXml->setAttribute ("compEnabled", s.compEnabled);
        chXml->setAttribute ("compThresholdDb", static_cast<double> (s.compThresholdDb));
        chXml->setAttribute ("compRatio", static_cast<double> (s.compRatio));
        chXml->setAttribute ("compAttackMs", static_cast<double> (s.compAttackMs));
        chXml->setAttribute ("compReleaseMs", static_cast<double> (s.compReleaseMs));
        chXml->setAttribute ("compMakeupDb", static_cast<double> (s.compMakeupDb));

        chXml->setAttribute ("saturationDrive", static_cast<double> (s.saturationDrive));
        chXml->setAttribute ("transientAttack", static_cast<double> (s.transientAttack));
        chXml->setAttribute ("transientSustain", static_cast<double> (s.transientSustain));
        chXml->setAttribute ("reverbSend", static_cast<double> (s.reverbSend));
    }

    return xml.toString();
}

bool MixerPreset::fromXmlString (const juce::String& xmlText, MixerPreset& outPreset)
{
    const auto xml = juce::parseXML (xmlText);
    if (xml == nullptr || ! xml->hasTagName ("GlorpMixPreset"))
        return false;

    outPreset.name = xml->getStringAttribute ("name", "User Preset");

    if (auto* mXml = xml->getChildByName ("Master"))
    {
        outPreset.master.level = static_cast<float> (mXml->getDoubleAttribute ("level", 0.85));
        outPreset.master.pan = static_cast<float> (mXml->getDoubleAttribute ("pan", 0.0));
        outPreset.master.limiterEnabled = mXml->getBoolAttribute ("limiter", true);
        outPreset.master.reverbReturn = static_cast<float> (mXml->getDoubleAttribute ("reverbReturn", 0.25));
        outPreset.master.reverbSize = static_cast<float> (mXml->getDoubleAttribute ("reverbSize", 0.45));
        outPreset.master.reverbDamp = static_cast<float> (mXml->getDoubleAttribute ("reverbDamp", 0.40));
        outPreset.master.reverbWidth = static_cast<float> (mXml->getDoubleAttribute ("reverbWidth", 0.85));
    }

    outPreset.micStrips.clear();
    outPreset.micNames.clear();

    for (auto* chXml : xml->getChildWithTagNameIterator ("Channel"))
    {
        MicStripParams s;
        s.level = static_cast<float> (chXml->getDoubleAttribute ("level", 1.0));
        s.pan = static_cast<float> (chXml->getDoubleAttribute ("pan", 0.0));
        s.mute = chXml->getBoolAttribute ("mute", false);
        s.solo = chXml->getBoolAttribute ("solo", false);
        s.phaseInvert = chXml->getBoolAttribute ("phaseInvert", false);

        s.eqEnabled = chXml->getBoolAttribute ("eqEnabled", true);
        s.eqLowGainDb = static_cast<float> (chXml->getDoubleAttribute ("eqLowGainDb", 0.0));
        s.eqMidGainDb = static_cast<float> (chXml->getDoubleAttribute ("eqMidGainDb", 0.0));
        s.eqMidFreq = static_cast<float> (chXml->getDoubleAttribute ("eqMidFreq", 1000.0));
        s.eqMidQ = static_cast<float> (chXml->getDoubleAttribute ("eqMidQ", 1.0));
        s.eqHighGainDb = static_cast<float> (chXml->getDoubleAttribute ("eqHighGainDb", 0.0));

        s.compEnabled = chXml->getBoolAttribute ("compEnabled", false);
        s.compThresholdDb = static_cast<float> (chXml->getDoubleAttribute ("compThresholdDb", -14.0));
        s.compRatio = static_cast<float> (chXml->getDoubleAttribute ("compRatio", 3.0));
        s.compAttackMs = static_cast<float> (chXml->getDoubleAttribute ("compAttackMs", 15.0));
        s.compReleaseMs = static_cast<float> (chXml->getDoubleAttribute ("compReleaseMs", 100.0));
        s.compMakeupDb = static_cast<float> (chXml->getDoubleAttribute ("compMakeupDb", 0.0));

        s.saturationDrive = static_cast<float> (chXml->getDoubleAttribute ("saturationDrive", 0.0));
        s.transientAttack = static_cast<float> (chXml->getDoubleAttribute ("transientAttack", 0.0));
        s.transientSustain = static_cast<float> (chXml->getDoubleAttribute ("transientSustain", 0.0));
        s.reverbSend = static_cast<float> (chXml->getDoubleAttribute ("reverbSend", 0.0));

        outPreset.micStrips.push_back (s);
        outPreset.micNames.push_back (chXml->getStringAttribute ("name"));
    }

    return true;
}

} // namespace glorp
