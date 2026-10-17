/*
    MixerView.cpp - Implementation of per-mic drum mixer console and DSP rack.
*/

#include "MixerView.h"

namespace glorp
{

namespace
{
    juce::Colour getMicColor (const juce::String& name)
    {
        const auto n = name.toLowerCase();
        if (n.contains ("kick") || n.contains ("kdrum"))
            return juce::Colour (0xffe67e22); // Orange
        if (n.contains ("snare"))
            return juce::Colour (0xff2ecc71); // Emerald Green
        if (n.contains ("hihat") || n.contains ("hh"))
            return juce::Colour (0xfff1c40f); // Amber / Yellow
        if (n.contains ("tom"))
            return juce::Colour (0xff00d2d3); // Cyan
        if (n.contains ("ride") || n.contains ("crash") || n.contains ("cymbal") || n.contains ("splash") || n.contains ("china"))
            return juce::Colour (0xffff4757); // Vibrant Magenta
        if (n.contains ("overhead") || n.contains ("oh"))
            return juce::Colour (0xff3498db); // Sky Blue
        if (n.contains ("room") || n.contains ("amb"))
            return juce::Colour (0xff9b59b6); // Purple
        return juce::Colour (0xff1abc9c);     // Teal default
    }

    float linearToDb (float linear) noexcept
    {
        return linear > 1.0e-5f ? 20.0f * std::log10 (linear) : -100.0f;
    }

    float dbToNormalized (float db) noexcept
    {
        // Maps -60dB .. +6dB to 0.0 .. 1.0
        if (db <= -60.0f) return 0.0f;
        if (db >= 6.0f)   return 1.0f;
        return (db + 60.0f) / 66.0f;
    }

    float normalizedToGain (float norm) noexcept
    {
        // 0.0 = -inf (0.0), 0.75 = 0dB (1.0), 1.0 = +6dB (2.0)
        if (norm <= 0.01f) return 0.0f;
        if (norm <= 0.75f)
        {
            const float t = norm / 0.75f;
            return std::pow (t, 2.5f);
        }
        const float t = (norm - 0.75f) / 0.25f;
        return 1.0f + t * 1.0f;
    }

    float gainToNormalized (float gain) noexcept
    {
        if (gain <= 0.001f) return 0.0f;
        if (gain <= 1.0f)
            return std::pow (gain, 1.0f / 2.5f) * 0.75f;
        return 0.75f + std::clamp ((gain - 1.0f), 0.0f, 1.0f) * 0.25f;
    }

    juce::String formatDb (float gain)
    {
        if (gain <= 0.001f) return "-inf";
        const float db = linearToDb (gain);
        if (std::abs (db) < 0.05f) return "0.0 dB";
        return (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }
}

//==============================================================================
// HandButton
//==============================================================================

MixerView::HandButton::HandButton (const juce::String& name, bool isToggle)
    : juce::Button (name), isToggle_ (isToggle)
{
}

void MixerView::HandButton::paintButton (juce::Graphics& g, bool isHover, bool isDown)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto ink = juce::Colour (0xff11141a);

    // Comic offset drop shadow
    g.setColour (ink.withAlpha (0.4f));
    g.fillRoundedRectangle (r.translated (2.0f, 2.0f), 4.0f);

    juce::Colour bg;
    if (active)
        bg = activeColour;
    else if (isDown)
        bg = juce::Colour (0xff252b36);
    else if (isHover)
        bg = juce::Colour (0xff323a48);
    else
        bg = juce::Colour (0xff1c212a);

    g.setColour (bg);
    g.fillRoundedRectangle (r, 4.0f);

    g.setColour (active ? ink : (isHover ? juce::Colour (0xfff0f3f6) : juce::Colour (0xff8892a0)));
    g.drawRoundedRectangle (r, 4.0f, active ? 2.0f : 1.2f);

    g.setColour (active ? ink : (isHover ? juce::Colours::white : juce::Colour (0xffd0d6de)));
    g.setFont (displayFont (std::min (r.getHeight() * 0.58f, 13.0f)));
    g.drawText (getButtonText(), r, juce::Justification::centred, false);
}

//==============================================================================
// ChannelStripComponent
//==============================================================================

MixerView::ChannelStripComponent::ChannelStripComponent (MixerView& o, PluginProcessor& proc, int chIdx)
    : owner (o), processor (proc), channelIndex_ (chIdx)
{
    phaseBtn.setButtonText (juce::CharPointer_UTF8 ("\xc3\x98")); // Ø
    phaseBtn.activeColour = juce::Colour (0xff00ffcc); // Neon Cyan
    phaseBtn.onClick = [this]() {
        auto params = processor.getSampler().getMicStripParams (channelIndex_);
        params.phaseInvert = ! params.phaseInvert;
        processor.getSampler().setMicStripParams (channelIndex_, params);
        phaseBtn.setActive (params.phaseInvert);
    };
    addAndMakeVisible (phaseBtn);

    fxBtn.setButtonText ("FX");
    fxBtn.activeColour = juce::Colour (0xfff39c12);
    fxBtn.onClick = [this]() {
        owner.channelFXPopup->openForChannel (channelIndex_);
    };
    addAndMakeVisible (fxBtn);

    soloBtn.setButtonText ("S");
    soloBtn.activeColour = juce::Colour (0xfff1c40f);
    soloBtn.onClick = [this]() {
        auto params = processor.getSampler().getMicStripParams (channelIndex_);
        params.solo = ! params.solo;
        processor.getSampler().setMicStripParams (channelIndex_, params);
        soloBtn.setActive (params.solo);
    };
    addAndMakeVisible (soloBtn);

    muteBtn.setButtonText ("M");
    muteBtn.activeColour = juce::Colour (0xffe74c3c);
    muteBtn.onClick = [this]() {
        auto params = processor.getSampler().getMicStripParams (channelIndex_);
        params.mute = ! params.mute;
        processor.getSampler().setMicStripParams (channelIndex_, params);
        muteBtn.setActive (params.mute);
    };
    addAndMakeVisible (muteBtn);

    updateFromState();
}

void MixerView::ChannelStripComponent::updateFromState()
{
    const auto params = processor.getSampler().getMicStripParams (channelIndex_);
    phaseBtn.setActive (params.phaseInvert);
    soloBtn.setActive (params.solo);
    muteBtn.setActive (params.mute);
    const bool fxActive = params.compEnabled || params.saturationDrive > 0.05f
                          || std::abs (params.transientAttack) > 0.05f
                          || std::abs (params.eqLowGainDb) > 0.5f
                          || std::abs (params.eqMidGainDb) > 0.5f
                          || std::abs (params.eqHighGainDb) > 0.5f;
    fxBtn.setActive (fxActive);
    repaint();
}

void MixerView::ChannelStripComponent::resized()
{
    const auto r = getLocalBounds().toFloat();
    phaseBtn.setBounds (r.getX() + 6.0f, 40.0f, (r.getWidth() - 16.0f) * 0.48f, 22.0f);
    fxBtn.setBounds (r.getX() + 8.0f + (r.getWidth() - 16.0f) * 0.48f, 40.0f, (r.getWidth() - 16.0f) * 0.48f, 22.0f);

    panBarBounds = juce::Rectangle<float> (r.getX() + 8.0f, 68.0f, r.getWidth() - 16.0f, 18.0f);

    soloBtn.setBounds (r.getX() + 6.0f, 92.0f, (r.getWidth() - 16.0f) * 0.48f, 22.0f);
    muteBtn.setBounds (r.getX() + 8.0f + (r.getWidth() - 16.0f) * 0.48f, 92.0f, (r.getWidth() - 16.0f) * 0.48f, 22.0f);

    // Fader & Meter area: height from y=122 to bottom - 30px
    const float faderTop = 122.0f;
    const float faderBottom = r.getBottom() - 32.0f;
    const float faderH = faderBottom - faderTop;

    faderTrackBounds = juce::Rectangle<float> (r.getX() + 14.0f, faderTop, 24.0f, faderH);
    meterBounds      = juce::Rectangle<float> (r.getX() + 46.0f, faderTop, 16.0f, faderH);

    const auto params = processor.getSampler().getMicStripParams (channelIndex_);
    const float norm = gainToNormalized (params.level);
    const float thumbY = faderTrackBounds.getBottom() - norm * faderTrackBounds.getHeight() - 10.0f;
    faderThumbBounds = juce::Rectangle<float> (faderTrackBounds.getX() - 4.0f, thumbY, faderTrackBounds.getWidth() + 8.0f, 20.0f);
}

void MixerView::ChannelStripComponent::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto name = processor.getSampler().micStripTitle (channelIndex_);
    const auto accent = getMicColor (name);
    const auto params = processor.getSampler().getMicStripParams (channelIndex_);

    // Strip background
    g.setColour (juce::Colour (0xff141820));
    g.fillRoundedRectangle (r, 6.0f);

    // Subtle drop border
    g.setColour (juce::Colour (0xff252b38));
    g.drawRoundedRectangle (r, 6.0f, 1.5f);

    // Top Channel Badge
    auto badgeR = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 4.0f, r.getWidth() - 8.0f, 28.0f);
    g.setColour (accent.withAlpha (0.22f));
    g.fillRoundedRectangle (badgeR, 4.0f);
    g.setColour (accent);
    g.drawRoundedRectangle (badgeR, 4.0f, 1.5f);

    g.setColour (juce::Colours::white);
    g.setFont (displayFont (11.0f));
    g.drawFittedText (name.toUpperCase(), badgeR.reduced (2.0f).toNearestInt(), juce::Justification::centred, 1);

    // Pan Bar
    g.setColour (juce::Colour (0xff0f131a));
    g.fillRoundedRectangle (panBarBounds, 3.0f);
    g.setColour (juce::Colour (0xff323a48));
    g.drawRoundedRectangle (panBarBounds, 3.0f, 1.0f);

    const float centerX = panBarBounds.getCentreX();
    const float panOffset = params.pan * (panBarBounds.getWidth() * 0.45f);
    if (std::abs (params.pan) > 0.02f)
    {
        g.setColour (juce::Colour (0xff3498db));
        if (params.pan > 0.0f)
            g.fillRect (centerX, panBarBounds.getY() + 2.0f, panOffset, panBarBounds.getHeight() - 4.0f);
        else
            g.fillRect (centerX + panOffset, panBarBounds.getY() + 2.0f, -panOffset, panBarBounds.getHeight() - 4.0f);
    }
    // Center tick
    g.setColour (juce::Colour (0xff636e72));
    g.drawVerticalLine (static_cast<int> (centerX), panBarBounds.getY(), panBarBounds.getBottom());

    g.setColour (juce::Colour (0xffc0c8d4));
    g.setFont (displayFont (9.0f));
    juce::String panStr = "C";
    if (params.pan < -0.05f) panStr = "L" + juce::String (static_cast<int> (-params.pan * 100.0f));
    else if (params.pan > 0.05f) panStr = "R" + juce::String (static_cast<int> (params.pan * 100.0f));
    g.drawText (panStr, panBarBounds, juce::Justification::centred, false);

    // Fader Track
    g.setColour (juce::Colour (0xff0a0d12));
    g.fillRoundedRectangle (faderTrackBounds, 4.0f);
    g.setColour (juce::Colour (0xff28313e));
    g.drawRoundedRectangle (faderTrackBounds, 4.0f, 1.0f);

    // 0dB Tick mark
    const float zeroY = faderTrackBounds.getBottom() - 0.75f * faderTrackBounds.getHeight();
    g.setColour (juce::Colour (0xff8892a0));
    g.drawHorizontalLine (static_cast<int> (zeroY), faderTrackBounds.getX() - 3.0f, faderTrackBounds.getX());

    // LED Meter
    g.setColour (juce::Colour (0xff080b0f));
    g.fillRoundedRectangle (meterBounds, 3.0f);
    g.setColour (juce::Colour (0xff202632));
    g.drawRoundedRectangle (meterBounds, 3.0f, 1.0f);

    const float peakL = processor.getSampler().getMicPeakL (channelIndex_);
    const float rmsL  = processor.getSampler().getMicRmsL  (channelIndex_);
    const float normPeak = dbToNormalized (linearToDb (peakL));
    const float normRms  = dbToNormalized (linearToDb (rmsL));

    const float meterH = meterBounds.getHeight() - 4.0f;
    const float meterW = meterBounds.getWidth() - 4.0f;
    const float meterX = meterBounds.getX() + 2.0f;
    const float meterY = meterBounds.getY() + 2.0f;

    // Draw active RMS meter bar
    if (normRms > 0.01f)
    {
        const float fillH = normRms * meterH;
        juce::ColourGradient grad (juce::Colour (0xff2ecc71), meterX, meterY + meterH,
                                   juce::Colour (0xffe74c3c), meterX, meterY, false);
        grad.addColour (0.70, juce::Colour (0xfff1c40f));
        g.setGradientFill (grad);
        g.fillRect (meterX, meterY + meterH - fillH, meterW, fillH);
    }

    // Peak hold line
    if (normPeak > 0.02f)
    {
        const float peakY = meterY + meterH - normPeak * meterH;
        g.setColour (normPeak >= 0.98f ? juce::Colour (0xffff0055) : juce::Colours::white);
        g.fillRect (meterX, peakY - 1.0f, meterW, 2.0f);
    }

    // Fader Thumb
    const float normGain = gainToNormalized (params.level);
    const float thumbY = faderTrackBounds.getBottom() - normGain * faderTrackBounds.getHeight() - 10.0f;
    auto thumbR = juce::Rectangle<float> (faderTrackBounds.getX() - 5.0f, thumbY, faderTrackBounds.getWidth() + 10.0f, 20.0f);

    // Drop shadow
    g.setColour (juce::Colour (0x66000000));
    g.fillRoundedRectangle (thumbR.translated (2.0f, 2.0f), 3.0f);

    // Thumb cap
    juce::ColourGradient thumbGrad (juce::Colour (0xff3a4454), thumbR.getX(), thumbR.getY(),
                                    juce::Colour (0xff1f242e), thumbR.getX(), thumbR.getBottom(), false);
    g.setGradientFill (thumbGrad);
    g.fillRoundedRectangle (thumbR, 3.0f);

    g.setColour (juce::Colour (0xff606c7e));
    g.drawRoundedRectangle (thumbR, 3.0f, 1.2f);

    // Center indicator line
    g.setColour (accent);
    g.fillRect (thumbR.getX() + 2.0f, thumbR.getCentreY() - 1.0f, thumbR.getWidth() - 4.0f, 2.0f);

    // Numeric dB Display
    auto dbRect = juce::Rectangle<float> (r.getX() + 4.0f, r.getBottom() - 26.0f, r.getWidth() - 8.0f, 20.0f);
    g.setColour (juce::Colour (0xff0a0d12));
    g.fillRoundedRectangle (dbRect, 3.0f);
    g.setColour (juce::Colour (0xff222a36));
    g.drawRoundedRectangle (dbRect, 3.0f, 1.0f);

    g.setColour (params.level > 1.0f ? juce::Colour (0xffff6b81) : juce::Colour (0xffd0d6de));
    g.setFont (displayFont (10.0f));
    g.drawText (formatDb (params.level), dbRect, juce::Justification::centred, false);
}

void MixerView::ChannelStripComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (panBarBounds.contains (p))
    {
        draggingPan = true;
        mouseDrag (e);
    }
    else if (p.y >= faderTrackBounds.getY() - 10.0f && p.y <= faderTrackBounds.getBottom() + 10.0f)
    {
        draggingFader = true;
        mouseDrag (e);
    }
}

void MixerView::ChannelStripComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingPan)
    {
        const float relX = (e.position.x - panBarBounds.getX()) / panBarBounds.getWidth();
        const float newPan = std::clamp (relX * 2.0f - 1.0f, -1.0f, 1.0f);
        auto params = processor.getSampler().getMicStripParams (channelIndex_);
        params.pan = newPan;
        processor.getSampler().setMicStripParams (channelIndex_, params);
        repaint();
    }
    else if (draggingFader)
    {
        const float norm = std::clamp ((faderTrackBounds.getBottom() - e.position.y) / faderTrackBounds.getHeight(), 0.0f, 1.0f);
        const float newLevel = normalizedToGain (norm);
        auto params = processor.getSampler().getMicStripParams (channelIndex_);
        params.level = newLevel;
        processor.getSampler().setMicStripParams (channelIndex_, params);
        repaint();
    }
}

void MixerView::ChannelStripComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (panBarBounds.contains (e.position))
    {
        auto params = processor.getSampler().getMicStripParams (channelIndex_);
        params.pan = 0.0f;
        processor.getSampler().setMicStripParams (channelIndex_, params);
        repaint();
    }
    else if (faderTrackBounds.expanded (10.0f, 0.0f).contains (e.position))
    {
        auto params = processor.getSampler().getMicStripParams (channelIndex_);
        params.level = 1.0f; // 0dB
        processor.getSampler().setMicStripParams (channelIndex_, params);
        repaint();
    }
}

//==============================================================================
// MasterStripComponent
//==============================================================================

MixerView::MasterStripComponent::MasterStripComponent (MixerView& o, PluginProcessor& proc)
    : owner (o), processor (proc)
{
    limiterBtn.setButtonText ("LIMITER");
    limiterBtn.activeColour = juce::Colour (0xff2ecc71);
    limiterBtn.onClick = [this]() {
        auto params = processor.getSampler().getMasterStripParams();
        params.limiterEnabled = ! params.limiterEnabled;
        processor.getSampler().setMasterStripParams (params);
        limiterBtn.setActive (params.limiterEnabled);
    };
    addAndMakeVisible (limiterBtn);

    fxBtn.setButtonText ("REVERB FX");
    fxBtn.activeColour = juce::Colour (0xff9b59b6);
    fxBtn.onClick = [this]() {
        owner.masterFXPopup->open();
    };
    addAndMakeVisible (fxBtn);

    updateFromState();
}

void MixerView::MasterStripComponent::updateFromState()
{
    const auto params = processor.getSampler().getMasterStripParams();
    limiterBtn.setActive (params.limiterEnabled);
    repaint();
}

void MixerView::MasterStripComponent::resized()
{
    const auto r = getLocalBounds().toFloat();
    limiterBtn.setBounds (r.getX() + 8.0f, 40.0f, r.getWidth() - 16.0f, 22.0f);
    panBarBounds = juce::Rectangle<float> (r.getX() + 8.0f, 68.0f, r.getWidth() - 16.0f, 18.0f);
    fxBtn.setBounds (r.getX() + 8.0f, 92.0f, r.getWidth() - 16.0f, 22.0f);

    const float faderTop = 122.0f;
    const float faderBottom = r.getBottom() - 32.0f;
    const float faderH = faderBottom - faderTop;

    faderTrackBounds = juce::Rectangle<float> (r.getX() + 14.0f, faderTop, 28.0f, faderH);
    meterLBounds     = juce::Rectangle<float> (r.getX() + 48.0f, faderTop, 12.0f, faderH);
    meterRBounds     = juce::Rectangle<float> (r.getX() + 63.0f, faderTop, 12.0f, faderH);
}

void MixerView::MasterStripComponent::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto params = processor.getSampler().getMasterStripParams();

    g.setColour (juce::Colour (0xff12161f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (juce::Colour (0xffe74c3c)); // Red master border
    g.drawRoundedRectangle (r, 6.0f, 2.0f);

    // Master Header Badge
    auto badgeR = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 4.0f, r.getWidth() - 8.0f, 28.0f);
    g.setColour (juce::Colour (0x33e74c3c));
    g.fillRoundedRectangle (badgeR, 4.0f);
    g.setColour (juce::Colour (0xffe74c3c));
    g.drawRoundedRectangle (badgeR, 4.0f, 1.5f);

    g.setColour (juce::Colours::white);
    g.setFont (displayFont (12.0f));
    g.drawText ("MASTER BUS", badgeR, juce::Justification::centred, false);

    // Master Pan Bar
    g.setColour (juce::Colour (0xff0a0d12));
    g.fillRoundedRectangle (panBarBounds, 3.0f);
    g.setColour (juce::Colour (0xff323a48));
    g.drawRoundedRectangle (panBarBounds, 3.0f, 1.0f);

    const float centerX = panBarBounds.getCentreX();
    const float panOffset = params.pan * (panBarBounds.getWidth() * 0.45f);
    if (std::abs (params.pan) > 0.02f)
    {
        g.setColour (juce::Colour (0xffe74c3c));
        if (params.pan > 0.0f)
            g.fillRect (centerX, panBarBounds.getY() + 2.0f, panOffset, panBarBounds.getHeight() - 4.0f);
        else
            g.fillRect (centerX + panOffset, panBarBounds.getY() + 2.0f, -panOffset, panBarBounds.getHeight() - 4.0f);
    }
    g.setColour (juce::Colour (0xff636e72));
    g.drawVerticalLine (static_cast<int> (centerX), panBarBounds.getY(), panBarBounds.getBottom());

    // Fader Track
    g.setColour (juce::Colour (0xff0a0d12));
    g.fillRoundedRectangle (faderTrackBounds, 4.0f);
    g.setColour (juce::Colour (0xff28313e));
    g.drawRoundedRectangle (faderTrackBounds, 4.0f, 1.0f);

    // 0dB mark
    const float zeroY = faderTrackBounds.getBottom() - 0.75f * faderTrackBounds.getHeight();
    g.setColour (juce::Colour (0xff8892a0));
    g.drawHorizontalLine (static_cast<int> (zeroY), faderTrackBounds.getX() - 3.0f, faderTrackBounds.getX());

    // Dual meters
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto mR = (ch == 0 ? meterLBounds : meterRBounds);
        g.setColour (juce::Colour (0xff080b0f));
        g.fillRoundedRectangle (mR, 2.0f);
        g.setColour (juce::Colour (0xff202632));
        g.drawRoundedRectangle (mR, 2.0f, 1.0f);

        const float peak = (ch == 0 ? processor.getSampler().getMasterPeakL() : processor.getSampler().getMasterPeakR());
        const float rms  = (ch == 0 ? processor.getSampler().getMasterRmsL()  : processor.getSampler().getMasterRmsR());
        const float normPeak = dbToNormalized (linearToDb (peak));
        const float normRms  = dbToNormalized (linearToDb (rms));

        const float mH = mR.getHeight() - 4.0f;
        const float mW = mR.getWidth() - 4.0f;
        const float mX = mR.getX() + 2.0f;
        const float mY = mR.getY() + 2.0f;

        if (normRms > 0.01f)
        {
            const float fillH = normRms * mH;
            juce::ColourGradient grad (juce::Colour (0xff2ecc71), mX, mY + mH,
                                       juce::Colour (0xffe74c3c), mX, mY, false);
            grad.addColour (0.70, juce::Colour (0xfff1c40f));
            g.setGradientFill (grad);
            g.fillRect (mX, mY + mH - fillH, mW, fillH);
        }

        if (normPeak > 0.02f)
        {
            const float peakY = mY + mH - normPeak * mH;
            g.setColour (normPeak >= 0.98f ? juce::Colour (0xffff0055) : juce::Colours::white);
            g.fillRect (mX, peakY - 1.0f, mW, 2.0f);
        }
    }

    // Master Fader Cap
    const float normGain = gainToNormalized (params.level);
    const float thumbY = faderTrackBounds.getBottom() - normGain * faderTrackBounds.getHeight() - 10.0f;
    auto thumbR = juce::Rectangle<float> (faderTrackBounds.getX() - 4.0f, thumbY, faderTrackBounds.getWidth() + 8.0f, 20.0f);

    g.setColour (juce::Colour (0x66000000));
    g.fillRoundedRectangle (thumbR.translated (2.0f, 2.0f), 3.0f);

    juce::ColourGradient thumbGrad (juce::Colour (0xff4a1218), thumbR.getX(), thumbR.getY(),
                                    juce::Colour (0xff1f0c0f), thumbR.getX(), thumbR.getBottom(), false);
    g.setGradientFill (thumbGrad);
    g.fillRoundedRectangle (thumbR, 3.0f);
    g.setColour (juce::Colour (0xffe74c3c));
    g.drawRoundedRectangle (thumbR, 3.0f, 1.2f);
    g.fillRect (thumbR.getX() + 2.0f, thumbR.getCentreY() - 1.0f, thumbR.getWidth() - 4.0f, 2.0f);

    // Master dB display
    auto dbRect = juce::Rectangle<float> (r.getX() + 6.0f, r.getBottom() - 26.0f, r.getWidth() - 12.0f, 20.0f);
    g.setColour (juce::Colour (0xff0a0d12));
    g.fillRoundedRectangle (dbRect, 3.0f);
    g.setColour (juce::Colour (0xff323a48));
    g.drawRoundedRectangle (dbRect, 3.0f, 1.0f);

    g.setColour (params.level > 1.0f ? juce::Colour (0xffff6b81) : juce::Colour (0xffd0d6de));
    g.setFont (displayFont (10.0f));
    g.drawText (formatDb (params.level), dbRect, juce::Justification::centred, false);
}

void MixerView::MasterStripComponent::mouseDown (const juce::MouseEvent& e)
{
    if (panBarBounds.contains (e.position))
    {
        draggingPan = true;
        mouseDrag (e);
    }
    else if (e.position.y >= faderTrackBounds.getY() - 10.0f && e.position.y <= faderTrackBounds.getBottom() + 10.0f)
    {
        draggingFader = true;
        mouseDrag (e);
    }
}

void MixerView::MasterStripComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingPan)
    {
        const float relX = (e.position.x - panBarBounds.getX()) / panBarBounds.getWidth();
        const float newPan = std::clamp (relX * 2.0f - 1.0f, -1.0f, 1.0f);
        auto params = processor.getSampler().getMasterStripParams();
        params.pan = newPan;
        processor.getSampler().setMasterStripParams (params);
        repaint();
    }
    else if (draggingFader)
    {
        const float norm = std::clamp ((faderTrackBounds.getBottom() - e.position.y) / faderTrackBounds.getHeight(), 0.0f, 1.0f);
        const float newLevel = normalizedToGain (norm);
        auto params = processor.getSampler().getMasterStripParams();
        params.level = newLevel;
        processor.getSampler().setMasterStripParams (params);
        repaint();
    }
}

void MixerView::MasterStripComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (panBarBounds.contains (e.position))
    {
        auto params = processor.getSampler().getMasterStripParams();
        params.pan = 0.0f;
        processor.getSampler().setMasterStripParams (params);
        repaint();
    }
    else if (faderTrackBounds.expanded (10.0f, 0.0f).contains (e.position))
    {
        auto params = processor.getSampler().getMasterStripParams();
        params.level = 0.85f;
        processor.getSampler().setMasterStripParams (params);
        repaint();
    }
}

//==============================================================================
// ChannelFXPopup
//==============================================================================

MixerView::ChannelFXPopup::ChannelFXPopup (MixerView& o, PluginProcessor& proc)
    : owner (o), processor (proc)
{
    closeBtn.onClick = [this]() { close(); };
    addAndMakeVisible (closeBtn);

    eqToggle.onClick = [this]() {
        if (currentChannel >= 0)
        {
            auto params = processor.getSampler().getMicStripParams (currentChannel);
            params.eqEnabled = ! params.eqEnabled;
            processor.getSampler().setMicStripParams (currentChannel, params);
            eqToggle.setActive (params.eqEnabled);
        }
    };
    addAndMakeVisible (eqToggle);

    compToggle.onClick = [this]() {
        if (currentChannel >= 0)
        {
            auto params = processor.getSampler().getMicStripParams (currentChannel);
            params.compEnabled = ! params.compEnabled;
            processor.getSampler().setMicStripParams (currentChannel, params);
            compToggle.setActive (params.compEnabled);
        }
    };
    addAndMakeVisible (compToggle);

    // Build Knobs
    eqLowGainKnob  = std::make_unique<Knob> ("LOW 80Hz", -12.0f, 12.0f, 0.0f, "dB");
    eqMidGainKnob  = std::make_unique<Knob> ("MID GAIN", -12.0f, 12.0f, 0.0f, "dB");
    eqMidFreqKnob  = std::make_unique<Knob> ("MID FREQ", 200.0f, 6000.0f, 1000.0f, "Hz");
    eqHighGainKnob = std::make_unique<Knob> ("HIGH 10k", -12.0f, 12.0f, 0.0f, "dB");

    compThreshKnob  = std::make_unique<Knob> ("THRESH", -30.0f, 0.0f, -14.0f, "dB");
    compRatioKnob   = std::make_unique<Knob> ("RATIO", 1.0f, 8.0f, 3.0f, ":1");
    compAttackKnob  = std::make_unique<Knob> ("ATTACK", 1.0f, 50.0f, 15.0f, "ms");
    compReleaseKnob = std::make_unique<Knob> ("RELEASE", 20.0f, 300.0f, 100.0f, "ms");
    compMakeupKnob  = std::make_unique<Knob> ("MAKEUP", 0.0f, 12.0f, 0.0f, "dB");

    satDriveKnob    = std::make_unique<Knob> ("SAT DRIVE", 0.0f, 1.0f, 0.0f, "");
    transAttackKnob = std::make_unique<Knob> ("ATTACK", -1.0f, 1.0f, 0.0f, "");
    transSustainKnob= std::make_unique<Knob> ("SUSTAIN", -1.0f, 1.0f, 0.0f, "");

    auto bindKnob = [this](Knob& k, auto setter) {
        k.onValueChanged = [this, setter](float v) {
            if (currentChannel >= 0)
            {
                auto params = processor.getSampler().getMicStripParams (currentChannel);
                setter (params, v);
                processor.getSampler().setMicStripParams (currentChannel, params);
            }
        };
    };

    bindKnob (*eqLowGainKnob,  [](auto& p, float v){ p.eqLowGainDb = v; });
    bindKnob (*eqMidGainKnob,  [](auto& p, float v){ p.eqMidGainDb = v; });
    bindKnob (*eqMidFreqKnob,  [](auto& p, float v){ p.eqMidFreq = v; });
    bindKnob (*eqHighGainKnob, [](auto& p, float v){ p.eqHighGainDb = v; });

    bindKnob (*compThreshKnob,  [](auto& p, float v){ p.compThresholdDb = v; });
    bindKnob (*compRatioKnob,   [](auto& p, float v){ p.compRatio = v; });
    bindKnob (*compAttackKnob,  [](auto& p, float v){ p.compAttackMs = v; });
    bindKnob (*compReleaseKnob, [](auto& p, float v){ p.compReleaseMs = v; });
    bindKnob (*compMakeupKnob,  [](auto& p, float v){ p.compMakeupDb = v; });

    bindKnob (*satDriveKnob,     [](auto& p, float v){ p.saturationDrive = v; });
    bindKnob (*transAttackKnob,  [](auto& p, float v){ p.transientAttack = v; });
    bindKnob (*transSustainKnob, [](auto& p, float v){ p.transientSustain = v; });

    addAndMakeVisible (*eqLowGainKnob);
    addAndMakeVisible (*eqMidGainKnob);
    addAndMakeVisible (*eqMidFreqKnob);
    addAndMakeVisible (*eqHighGainKnob);

    addAndMakeVisible (*compThreshKnob);
    addAndMakeVisible (*compRatioKnob);
    addAndMakeVisible (*compAttackKnob);
    addAndMakeVisible (*compReleaseKnob);
    addAndMakeVisible (*compMakeupKnob);

    addAndMakeVisible (*satDriveKnob);
    addAndMakeVisible (*transAttackKnob);
    addAndMakeVisible (*transSustainKnob);

    setVisible (false);
}

void MixerView::ChannelFXPopup::openForChannel (int chIdx)
{
    currentChannel = chIdx;
    if (currentChannel < 0 || currentChannel >= processor.getSampler().numMicStrips())
        return;

    const auto p = processor.getSampler().getMicStripParams (currentChannel);
    eqToggle.setActive (p.eqEnabled);
    compToggle.setActive (p.compEnabled);

    eqLowGainKnob->setValue (p.eqLowGainDb);
    eqMidGainKnob->setValue (p.eqMidGainDb);
    eqMidFreqKnob->setValue (p.eqMidFreq);
    eqHighGainKnob->setValue (p.eqHighGainDb);

    compThreshKnob->setValue (p.compThresholdDb);
    compRatioKnob->setValue (p.compRatio);
    compAttackKnob->setValue (p.compAttackMs);
    compReleaseKnob->setValue (p.compReleaseMs);
    compMakeupKnob->setValue (p.compMakeupDb);

    satDriveKnob->setValue (p.saturationDrive);
    transAttackKnob->setValue (p.transientAttack);
    transSustainKnob->setValue (p.transientSustain);

    setVisible (true);
    toFront (true);
    resized();
    repaint();
}

void MixerView::ChannelFXPopup::close()
{
    setVisible (false);
    currentChannel = -1;
    owner.refreshChannels();
}

void MixerView::ChannelFXPopup::resized()
{
    closeBtn.setBounds (getWidth() - 90, 14, 76, 24);

    // Section 1: 3-Band Parametric EQ
    eqToggle.setBounds (24, 52, 90, 22);
    eqLowGainKnob->setBounds  (124, 46, 60, 60);
    eqMidFreqKnob->setBounds  (194, 46, 60, 60);
    eqMidGainKnob->setBounds  (264, 46, 60, 60);
    eqHighGainKnob->setBounds (334, 46, 60, 60);

    // Section 2: VCA Compressor
    compToggle.setBounds (24, 126, 90, 22);
    compThreshKnob->setBounds  (124, 120, 60, 60);
    compRatioKnob->setBounds   (194, 120, 60, 60);
    compAttackKnob->setBounds  (264, 120, 60, 60);
    compReleaseKnob->setBounds (334, 120, 60, 60);
    compMakeupKnob->setBounds  (404, 120, 60, 60);

    // Section 3: Saturation & Transient Shaper
    satDriveKnob->setBounds     (124, 194, 60, 60);
    transAttackKnob->setBounds  (244, 194, 60, 60);
    transSustainKnob->setBounds (314, 194, 60, 60);
}

void MixerView::ChannelFXPopup::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);

    // Backdrop shadow
    g.setColour (juce::Colour (0x99000000));
    g.fillRoundedRectangle (r.translated (4.0f, 4.0f), 8.0f);

    // Container body
    g.setColour (juce::Colour (0xff171c26));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour (0xfff39c12)); // Warm gold border
    g.drawRoundedRectangle (r, 8.0f, 2.0f);

    // Header
    const auto name = currentChannel >= 0 ? processor.getSampler().micStripTitle (currentChannel) : "";
    g.setColour (juce::Colours::white);
    g.setFont (displayFont (15.0f));
    g.drawText ("CHANNEL FX RACK: " + name.toUpperCase(), 24, 14, getWidth() - 120, 24, juce::Justification::left, false);

    // Dividing lines
    g.setColour (juce::Colour (0xff252c3c));
    g.drawHorizontalLine (112, 20.0f, getWidth() - 20.0f);
    g.drawHorizontalLine (186, 20.0f, getWidth() - 20.0f);

    // Section labels
    g.setColour (juce::Colour (0xff8892a0));
    g.setFont (displayFont (11.0f));
    g.drawText ("3-BAND EQ", 24, 78, 90, 20, juce::Justification::left, false);
    g.drawText ("VCA COMP", 24, 152, 90, 20, juce::Justification::left, false);
    g.drawText ("TAPE & TRANSIENT", 24, 214, 96, 20, juce::Justification::left, false);
}

void MixerView::ChannelFXPopup::mouseDown (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
}

//==============================================================================
// MasterFXPopup
//==============================================================================

MixerView::MasterFXPopup::MasterFXPopup (MixerView& o, PluginProcessor& proc)
    : owner (o), processor (proc)
{
    closeBtn.onClick = [this]() { close(); };
    addAndMakeVisible (closeBtn);

    limiterToggle.onClick = [this]() {
        auto params = processor.getSampler().getMasterStripParams();
        params.limiterEnabled = ! params.limiterEnabled;
        processor.getSampler().setMasterStripParams (params);
        limiterToggle.setActive (params.limiterEnabled);
    };
    addAndMakeVisible (limiterToggle);

    revReturnKnob = std::make_unique<Knob> ("REVERB WET", 0.0f, 1.0f, 0.25f, "");
    revSizeKnob   = std::make_unique<Knob> ("ROOM SIZE", 0.0f, 1.0f, 0.45f, "");
    revDampKnob   = std::make_unique<Knob> ("DAMPING", 0.0f, 1.0f, 0.40f, "");
    revWidthKnob  = std::make_unique<Knob> ("WIDTH", 0.0f, 1.0f, 0.85f, "");

    revReturnKnob->onValueChanged = [this](float v) {
        auto params = processor.getSampler().getMasterStripParams();
        params.reverbReturn = v;
        processor.getSampler().setMasterStripParams (params);
    };
    revSizeKnob->onValueChanged = [this](float v) {
        auto params = processor.getSampler().getMasterStripParams();
        params.reverbSize = v;
        processor.getSampler().setMasterStripParams (params);
    };
    revDampKnob->onValueChanged = [this](float v) {
        auto params = processor.getSampler().getMasterStripParams();
        params.reverbDamp = v;
        processor.getSampler().setMasterStripParams (params);
    };
    revWidthKnob->onValueChanged = [this](float v) {
        auto params = processor.getSampler().getMasterStripParams();
        params.reverbWidth = v;
        processor.getSampler().setMasterStripParams (params);
    };

    addAndMakeVisible (*revReturnKnob);
    addAndMakeVisible (*revSizeKnob);
    addAndMakeVisible (*revDampKnob);
    addAndMakeVisible (*revWidthKnob);

    setVisible (false);
}

void MixerView::MasterFXPopup::open()
{
    const auto p = processor.getSampler().getMasterStripParams();
    limiterToggle.setActive (p.limiterEnabled);
    revReturnKnob->setValue (p.reverbReturn);
    revSizeKnob->setValue (p.reverbSize);
    revDampKnob->setValue (p.reverbDamp);
    revWidthKnob->setValue (p.reverbWidth);

    setVisible (true);
    toFront (true);
    resized();
    repaint();
}

void MixerView::MasterFXPopup::close()
{
    setVisible (false);
    owner.refreshChannels();
}

void MixerView::MasterFXPopup::resized()
{
    closeBtn.setBounds (getWidth() - 90, 14, 76, 24);
    limiterToggle.setBounds (24, 60, 100, 24);

    revReturnKnob->setBounds (144, 52, 60, 60);
    revSizeKnob->setBounds   (214, 52, 60, 60);
    revDampKnob->setBounds   (284, 52, 60, 60);
    revWidthKnob->setBounds  (354, 52, 60, 60);
}

void MixerView::MasterFXPopup::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);

    g.setColour (juce::Colour (0x99000000));
    g.fillRoundedRectangle (r.translated (4.0f, 4.0f), 8.0f);

    g.setColour (juce::Colour (0xff171c26));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour (0xff9b59b6)); // Purple border
    g.drawRoundedRectangle (r, 8.0f, 2.0f);

    g.setColour (juce::Colours::white);
    g.setFont (displayFont (15.0f));
    g.drawText ("MASTER REVERB & DYNAMICS", 24, 14, getWidth() - 120, 24, juce::Justification::left, false);
}

void MixerView::MasterFXPopup::mouseDown (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
}

//==============================================================================
// MixerView
//==============================================================================

MixerView::MixerView (PluginProcessor& proc)
    : processor (proc)
{
    channelContainer = std::make_unique<juce::Component>();
    channelViewport.setViewedComponent (channelContainer.get(), false);
    channelViewport.setScrollBarsShown (false, true, false, false);
    addAndMakeVisible (channelViewport);

    masterStrip = std::make_unique<MasterStripComponent> (*this, processor);
    addAndMakeVisible (*masterStrip);

    // Preset buttons
    presetCleanBtn.onClick = [this]() { applyPreset (0); };
    presetRockBtn.onClick  = [this]() { applyPreset (1); };
    presetWarmBtn.onClick  = [this]() { applyPreset (2); };
    presetPunchBtn.onClick = [this]() { applyPreset (3); };
    presetDryBtn.onClick   = [this]() { applyPreset (4); };

    savePresetBtn.onClick  = [this]() { promptSavePreset(); };
    loadPresetBtn.onClick  = [this]() { promptLoadPreset(); };

    addAndMakeVisible (presetCleanBtn);
    addAndMakeVisible (presetRockBtn);
    addAndMakeVisible (presetWarmBtn);
    addAndMakeVisible (presetPunchBtn);
    addAndMakeVisible (presetDryBtn);
    addAndMakeVisible (savePresetBtn);
    addAndMakeVisible (loadPresetBtn);

    channelFXPopup = std::make_unique<ChannelFXPopup> (*this, processor);
    masterFXPopup  = std::make_unique<MasterFXPopup> (*this, processor);
    addChildComponent (*channelFXPopup);
    addChildComponent (*masterFXPopup);

    refreshChannels();
    startTimerHz (30); // 30Hz meter redraw
}

MixerView::~MixerView()
{
    stopTimer();
}

void MixerView::refreshChannels()
{
    channelStrips.clear();
    const int numMics = processor.getSampler().numMicStrips();

    for (int i = 0; i < numMics; ++i)
    {
        auto strip = std::make_unique<ChannelStripComponent> (*this, processor, i);
        channelContainer->addAndMakeVisible (*strip);
        channelStrips.push_back (std::move (strip));
    }

    if (masterStrip != nullptr)
        masterStrip->updateFromState();

    resized();
    repaint();
}

void MixerView::openChannelFX (int channelIndex)
{
    if (channelFXPopup != nullptr)
        channelFXPopup->openForChannel (channelIndex);
}

void MixerView::openMasterFX()
{
    if (masterFXPopup != nullptr)
        masterFXPopup->open();
}

void MixerView::applyPreset (int presetIndex)
{
    const int numMics = processor.getSampler().numMicStrips();
    std::vector<juce::String> names;
    for (int i = 0; i < numMics; ++i)
        names.push_back (processor.getSampler().micStripTitle (i));

    MixerPreset p;
    switch (presetIndex)
    {
        case 0: p = MixerPreset::createDefaultClean (numMics, names); activePresetName = "Default Clean"; break;
        case 1: p = MixerPreset::createMassiveRock  (numMics, names); activePresetName = "Massive Rock";  break;
        case 2: p = MixerPreset::createVintageWarmth(numMics, names); activePresetName = "Vintage Warmth"; break;
        case 3: p = MixerPreset::createPunchyStudio (numMics, names); activePresetName = "Punchy Studio"; break;
        case 4: p = MixerPreset::createDryTight     (numMics, names); activePresetName = "Dry & Tight";    break;
        default: break;
    }

    processor.getSampler().applyMixerPreset (p);
    for (auto& s : channelStrips)
        s->updateFromState();

    if (masterStrip != nullptr)
        masterStrip->updateFromState();

    repaint();
}

void MixerView::promptSavePreset()
{
    const auto dir = processor.getSettings().getConfigDirectory().getChildFile ("mix_presets");
    dir.createDirectory();

    auto file = dir.getChildFile (activePresetName.toLowerCase().replace (" ", "_") + ".glorpmix");
    const auto preset = processor.getSampler().getCurrentMixerPreset (activePresetName);
    file.replaceWithText (preset.toXmlString());
}

void MixerView::promptLoadPreset()
{
    const auto dir = processor.getSettings().getConfigDirectory().getChildFile ("mix_presets");
    if (! dir.isDirectory())
        return;

    juce::PopupMenu m;
    const auto files = dir.findChildFiles (juce::File::findFiles, false, "*.glorpmix");
    if (files.isEmpty())
    {
        m.addItem (1, "(No saved .glorpmix presets yet)", false);
        m.showMenuAsync (juce::PopupMenu::Options());
        return;
    }

    int id = 1;
    for (const auto& f : files)
        m.addItem (id++, f.getFileNameWithoutExtension());

    m.showMenuAsync (juce::PopupMenu::Options(), [this, files](int result) {
        if (result > 0 && result <= files.size())
        {
            const auto text = files[result - 1].loadFileAsString();
            MixerPreset p;
            if (MixerPreset::fromXmlString (text, p))
            {
                activePresetName = p.name;
                processor.getSampler().applyMixerPreset (p);
                for (auto& s : channelStrips)
                    s->updateFromState();
                if (masterStrip != nullptr)
                    masterStrip->updateFromState();
                repaint();
            }
        }
    });
}

void MixerView::timerCallback()
{
    // Repaint strips and meters
    for (auto& s : channelStrips)
        s->repaint();

    if (masterStrip != nullptr)
        masterStrip->repaint();
}

void MixerView::resized()
{
    const auto r = getLocalBounds().toFloat();

    // Top control bar
    const float btnY = 10.0f;
    const float btnH = 26.0f;
    presetCleanBtn.setBounds (16,  btnY, 68, btnH);
    presetRockBtn.setBounds  (88,  btnY, 68, btnH);
    presetWarmBtn.setBounds  (160, btnY, 68, btnH);
    presetPunchBtn.setBounds (232, btnY, 68, btnH);
    presetDryBtn.setBounds   (304, btnY, 68, btnH);

    savePresetBtn.setBounds  (386, btnY, 110, btnH);
    loadPresetBtn.setBounds  (500, btnY, 80,  btnH);

    // Channel Area vs Master Strip
    const float contentY = 46.0f;
    const float contentH = r.getHeight() - contentY - 8.0f;
    const float masterW = 100.0f;

    const float viewW = r.getWidth() - masterW - 20.0f;
    channelViewport.setBounds (12, static_cast<int> (contentY), static_cast<int> (viewW), static_cast<int> (contentH));

    const float stripW = 86.0f;
    const float containerW = std::max (viewW, static_cast<float> (channelStrips.size()) * (stripW + 4.0f) + 12.0f);
    channelContainer->setBounds (0, 0, static_cast<int> (containerW), static_cast<int> (contentH));

    float curX = 4.0f;
    for (auto& s : channelStrips)
    {
        s->setBounds (static_cast<int> (curX), 0, static_cast<int> (stripW), static_cast<int> (contentH));
        curX += stripW + 4.0f;
    }

    masterStrip->setBounds (static_cast<int> (r.getWidth() - masterW - 8.0f), static_cast<int> (contentY),
                            static_cast<int> (masterW), static_cast<int> (contentH));

    if (channelFXPopup != nullptr)
        channelFXPopup->setBounds (50, 40, std::min (480, getWidth() - 100), 270);

    if (masterFXPopup != nullptr)
        masterFXPopup->setBounds (getWidth() - 460, 40, 430, 130);
}

void MixerView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0f1218));

    // Preset label
    g.setColour (juce::Colour (0xff8892a0));
    g.setFont (displayFont (11.0f));
    g.drawText ("ACTIVE PRESET: " + activePresetName.toUpperCase(),
                getWidth() - 280, 14, 160, 20, juce::Justification::right, false);
}

void MixerView::mouseDown (const juce::MouseEvent& e)
{
    // Dismiss popups if clicking outside
    if (channelFXPopup->isOpen() && ! channelFXPopup->getBounds().contains (e.position.toInt()))
        channelFXPopup->close();

    if (masterFXPopup->isOpen() && ! masterFXPopup->getBounds().contains (e.position.toInt()))
        masterFXPopup->close();
}

} // namespace glorp
