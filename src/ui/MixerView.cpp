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
    const auto ink = theme.ink;

    // Comic offset drop shadow (light mode only)
    if (! theme.isDark)
    {
        g.setColour (ink.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (2.0f, 2.0f), 4.0f);
    }

    juce::Colour bg;
    if (active)
        bg = activeColour;
    else if (isDown)
        bg = theme.isDark ? juce::Colour (0xff282a30) : juce::Colour (0xffe5e9dc);
    else if (isHover)
        bg = theme.isDark ? juce::Colour (0xff24252a) : juce::Colour (0xfff4f6ee);
    else
        bg = theme.panel;

    g.setColour (bg);
    g.fillRoundedRectangle (r, 4.0f);

    const auto borderCol = isHover ? theme.accent : theme.panelEdge;
    drawSketchRect (g, r, borderCol, active ? 2.0f : 1.4f);

    const bool whiteText = theme.isDark || (active && (activeColour == juce::Colour (0xffe74c3c) || activeColour == juce::Colour (0xff111215)));
    g.setColour (whiteText ? juce::Colours::white : (active ? juce::Colour (0xff111215) : theme.ink));
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

    bleedBtn.setButtonText ("BLD");
    bleedBtn.activeColour = juce::Colour (0xffe67e22);
    bleedBtn.onClick = [this]() {
        owner.openBleedMatrix (channelIndex_);
    };
    addAndMakeVisible (bleedBtn);

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

    busBtn.onClick = [this]() {
        juce::PopupMenu menu;
        const int currentBus = processor.getSampler().getMicOutputBus (channelIndex_);
        for (int b = 0; b < 16; ++b)
        {
            const juce::String busLabel = (b == 0) ? "Out 1/2 (Master Mix)"
                                                   : ("Out " + juce::String (b * 2 + 1) + "/" + juce::String (b * 2 + 2));
            menu.addItem (busLabel, true, b == currentBus, [this, b]() {
                processor.getSampler().setMicOutputBus (channelIndex_, b);
                updateFromState();
            });
        }
        juce::PopupMenu::Options opts;
        opts = opts.withTargetComponent (&busBtn).withParentComponent (&owner);
        menu.showMenuAsync (opts);
    };
    addAndMakeVisible (busBtn);

    updateFromState();
    setTheme (owner.theme);
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

    const auto& bm = processor.getSampler().getBleedMatrix();
    bool hasBleed = false;
    for (int p = 0; p < BleedMatrix::maxPieces; ++p)
    {
        if (bm.getCellGain (p, channelIndex_) > 0.05f && ! bm.isCellMuted (p, channelIndex_))
        {
            hasBleed = true;
            break;
        }
    }
    bleedBtn.setActive (hasBleed && bm.getMode() != BleedMatrix::Mode::disabled);

    const int b = processor.getSampler().getMicOutputBus (channelIndex_);
    busBtn.setButtonText (b == 0 ? "M 1/2" : (juce::String (b * 2 + 1) + "/" + juce::String (b * 2 + 2)));
    busBtn.activeColour = (b == 0) ? juce::Colour (0xffea8c16) : juce::Colour (0xff3498db);
    busBtn.setActive (b > 0);

    repaint();
}

void MixerView::ChannelStripComponent::resized()
{
    const auto r = getLocalBounds().toFloat();
    const bool isCompact = (r.getHeight() < 300.0f);

    const float btnGap = 2.0f;
    const float topBtnW = (r.getWidth() - 12.0f - btnGap * 2.0f) / 3.0f;
    const float topBtnY = isCompact ? 30.0f : 38.0f;
    const float topBtnH = isCompact ? 19.0f : 22.0f;

    phaseBtn.setBounds (r.getX() + 6.0f, topBtnY, topBtnW, topBtnH);
    fxBtn.setBounds (r.getX() + 6.0f + topBtnW + btnGap, topBtnY, topBtnW, topBtnH);
    bleedBtn.setBounds (r.getX() + 6.0f + (topBtnW + btnGap) * 2.0f, topBtnY, topBtnW, topBtnH);

    const float panY = isCompact ? 52.0f : 64.0f;
    const float panH = isCompact ? 15.0f : 18.0f;
    panBarBounds = juce::Rectangle<float> (r.getX() + 8.0f, panY, r.getWidth() - 16.0f, panH);

    const float soloMuteY = isCompact ? 70.0f : 86.0f;
    const float soloMuteH = isCompact ? 19.0f : 22.0f;
    soloBtn.setBounds (r.getX() + 6.0f, soloMuteY, (r.getWidth() - 16.0f) * 0.48f, soloMuteH);
    muteBtn.setBounds (r.getX() + 8.0f + (r.getWidth() - 16.0f) * 0.48f, soloMuteY, (r.getWidth() - 16.0f) * 0.48f, soloMuteH);

    // Fader & Meter area
    const float faderTop = isCompact ? 94.0f : 112.0f;
    const float faderBottom = isCompact ? (r.getBottom() - 44.0f) : (r.getBottom() - 56.0f);
    const float faderH = std::max (30.0f, faderBottom - faderTop);

    faderTrackBounds = juce::Rectangle<float> (r.getX() + 14.0f, faderTop, 24.0f, faderH);
    meterBounds      = juce::Rectangle<float> (r.getX() + 46.0f, faderTop, 16.0f, faderH);

    const auto params = processor.getSampler().getMicStripParams (channelIndex_);
    const float norm = gainToNormalized (params.level);
    const float thumbY = faderTrackBounds.getBottom() - norm * faderTrackBounds.getHeight() - (isCompact ? 8.0f : 10.0f);
    faderThumbBounds = juce::Rectangle<float> (faderTrackBounds.getX() - 4.0f, thumbY, faderTrackBounds.getWidth() + 8.0f, isCompact ? 16.0f : 20.0f);

    const float busH = isCompact ? 18.0f : 22.0f;
    busBtn.setBounds (r.getX() + 6.0f, r.getBottom() - (busH + 3.0f), r.getWidth() - 12.0f, busH);
}

void MixerView::ChannelStripComponent::setTheme (const Theme& t)
{
    theme = t;
    phaseBtn.setTheme (t);
    fxBtn.setTheme (t);
    bleedBtn.setTheme (t);
    soloBtn.setTheme (t);
    muteBtn.setTheme (t);
    busBtn.setTheme (t);
    repaint();
    repaint();
}

void MixerView::ChannelStripComponent::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto name = processor.getSampler().micStripTitle (channelIndex_);
    const auto accent = getMicColor (name);
    const auto params = processor.getSampler().getMicStripParams (channelIndex_);
    const auto ink = theme.ink;

    // Comic offset drop shadow (light mode only)
    if (! theme.isDark)
    {
        g.setColour (ink.withAlpha (0.32f));
        g.fillRoundedRectangle (r.translated (3.0f, 3.0f), 6.0f);
    }

    // Strip background
    g.setColour (theme.panel);
    g.fillRoundedRectangle (r, 6.0f);

    // Sketched ink border
    drawSketchRect (g, r, theme.panelEdge, 1.8f, static_cast<uint32_t> (channelIndex_ * 7 + 13));

    // Top Channel Badge
    auto badgeR = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 4.0f, r.getWidth() - 8.0f, 26.0f);
    g.setColour (accent);
    g.fillRoundedRectangle (badgeR, 4.0f);
    drawSketchRect (g, badgeR, theme.isDark ? theme.panelEdge : ink, 1.5f, static_cast<uint32_t> (channelIndex_ * 11 + 3));

    g.setColour (juce::Colours::white);
    g.setFont (displayFont (11.0f));
    g.drawFittedText (name.toUpperCase(), badgeR.reduced (2.0f).toNearestInt(), juce::Justification::centred, 1);

    // Pan Bar: trough slot with border
    g.setColour (theme.trough);
    g.fillRoundedRectangle (panBarBounds, 3.0f);
    drawSketchRect (g, panBarBounds, theme.panelEdge, 1.2f, 99);

    const float centerX = panBarBounds.getCentreX();
    const float panOffset = params.pan * (panBarBounds.getWidth() * 0.45f);
    if (std::abs (params.pan) > 0.02f)
    {
        g.setColour (theme.accent);
        if (params.pan > 0.0f)
            g.fillRect (centerX, panBarBounds.getY() + 2.0f, panOffset, panBarBounds.getHeight() - 4.0f);
        else
            g.fillRect (centerX + panOffset, panBarBounds.getY() + 2.0f, -panOffset, panBarBounds.getHeight() - 4.0f);
    }
    // Center tick
    g.setColour (theme.ink);
    g.drawVerticalLine (static_cast<int> (centerX), panBarBounds.getY(), panBarBounds.getBottom());

    g.setColour (theme.ink);
    g.setFont (displayFont (9.0f));
    juce::String panStr = "C";
    if (params.pan < -0.05f) panStr = "L" + juce::String (static_cast<int> (-params.pan * 100.0f));
    else if (params.pan > 0.05f) panStr = "R" + juce::String (static_cast<int> (params.pan * 100.0f));
    g.drawText (panStr, panBarBounds, juce::Justification::centred, false);

    // Fader Track: sketched vertical slot
    g.setColour (theme.trough);
    g.fillRoundedRectangle (faderTrackBounds, 4.0f);
    drawSketchRect (g, faderTrackBounds, theme.panelEdge, 1.4f, 101);

    // 0dB Tick mark
    const float zeroY = faderTrackBounds.getBottom() - 0.75f * faderTrackBounds.getHeight();
    g.setColour (theme.ink.withAlpha (0.6f));
    g.drawHorizontalLine (static_cast<int> (zeroY), faderTrackBounds.getX() - 4.0f, faderTrackBounds.getX());

    // LED Meter: retro comic meter capsule
    g.setColour (theme.trough);
    g.fillRoundedRectangle (meterBounds, 3.0f);
    drawSketchRect (g, meterBounds, theme.panelEdge, 1.3f, 202);

    const float peakL = processor.getSampler().getMicPeakL (channelIndex_);
    const float rmsL  = processor.getSampler().getMicRmsL  (channelIndex_);
    const float normPeak = dbToNormalized (linearToDb (peakL));
    const float normRms  = dbToNormalized (linearToDb (rmsL));

    const float meterH = meterBounds.getHeight() - 4.0f;
    const float meterW = meterBounds.getWidth() - 4.0f;
    const float meterX = meterBounds.getX() + 2.0f;
    const float meterY = meterBounds.getY() + 2.0f;

    // Draw active RMS meter bar in discrete comic segments
    if (normRms > 0.01f)
    {
        constexpr int numSegs = 16;
        const int activeSegs = std::clamp (static_cast<int> (normRms * numSegs), 1, numSegs);
        const float segH = meterH / numSegs;

        for (int s = 0; s < activeSegs; ++s)
        {
            const float sy = meterY + meterH - (s + 1) * segH;
            juce::Colour segCol = juce::Colour (0xff2ecc71); // Green
            if (s >= 13) segCol = juce::Colour (0xffe74c3c); // Red peak
            else if (s >= 10) segCol = juce::Colour (0xfff1c40f); // Amber / Yellow

            g.setColour (segCol);
            g.fillRoundedRectangle (meterX, sy + 0.5f, meterW, segH - 1.0f, 1.0f);
        }
    }

    // Peak hold line
    if (normPeak > 0.02f)
    {
        const float peakY = meterY + meterH - normPeak * meterH;
        g.setColour (normPeak >= 0.98f ? juce::Colour (0xffe74c3c) : theme.ink);
        g.fillRect (meterX, peakY - 1.0f, meterW, 2.0f);
    }

    // Fader Thumb: hand-drawn beveled paper cap with ink border & drop shadow
    const float normGain = gainToNormalized (params.level);
    const float thumbY = faderTrackBounds.getBottom() - normGain * faderTrackBounds.getHeight() - 10.0f;
    auto thumbR = juce::Rectangle<float> (faderTrackBounds.getX() - 5.0f, thumbY, faderTrackBounds.getWidth() + 10.0f, 20.0f);

    // Drop shadow
    if (! theme.isDark)
    {
        g.setColour (ink.withAlpha (0.4f));
        g.fillRoundedRectangle (thumbR.translated (2.0f, 2.0f), 3.0f);
    }

    // Thumb cap
    g.setColour (theme.panel);
    g.fillRoundedRectangle (thumbR, 3.0f);
    drawSketchRect (g, thumbR, theme.panelEdge, 1.8f, 55);

    // Center indicator stripe
    g.setColour (accent);
    g.fillRect (thumbR.getX() + 2.0f, thumbR.getCentreY() - 1.5f, thumbR.getWidth() - 4.0f, 3.0f);

    // Numeric dB Display
    const bool isCompact = (r.getHeight() < 300.0f);
    const float dbY = isCompact ? (r.getBottom() - 41.0f) : (r.getBottom() - 50.0f);
    auto dbRect = juce::Rectangle<float> (r.getX() + 4.0f, dbY, r.getWidth() - 8.0f, isCompact ? 16.0f : 18.0f);
    g.setColour (theme.trough);
    g.fillRoundedRectangle (dbRect, 3.0f);
    drawSketchRect (g, dbRect, theme.panelEdge, 1.2f, 77);

    g.setColour (params.level > 1.0f ? juce::Colour (0xffd03020) : theme.ink);
    g.setFont (displayFont (isCompact ? 8.5f : 9.5f));
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
    setTheme (owner.theme);
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
    const bool isCompact = (r.getHeight() < 300.0f);

    const float btnH = isCompact ? 19.0f : 22.0f;
    limiterBtn.setBounds (r.getX() + 8.0f, isCompact ? 30.0f : 40.0f, r.getWidth() - 16.0f, btnH);
    panBarBounds = juce::Rectangle<float> (r.getX() + 8.0f, isCompact ? 52.0f : 68.0f, r.getWidth() - 16.0f, isCompact ? 15.0f : 18.0f);
    fxBtn.setBounds (r.getX() + 8.0f, isCompact ? 70.0f : 92.0f, r.getWidth() - 16.0f, btnH);

    const float faderTop = isCompact ? 94.0f : 122.0f;
    const float faderBottom = isCompact ? (r.getBottom() - 28.0f) : (r.getBottom() - 32.0f);
    const float faderH = std::max (30.0f, faderBottom - faderTop);

    faderTrackBounds = juce::Rectangle<float> (r.getX() + 14.0f, faderTop, 28.0f, faderH);
    meterLBounds     = juce::Rectangle<float> (r.getX() + 48.0f, faderTop, 12.0f, faderH);
    meterRBounds     = juce::Rectangle<float> (r.getX() + 63.0f, faderTop, 12.0f, faderH);
}

void MixerView::MasterStripComponent::setTheme (const Theme& t)
{
    theme = t;
    fxBtn.setTheme (t);
    limiterBtn.setTheme (t);
    repaint();
}

void MixerView::MasterStripComponent::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto params = processor.getSampler().getMasterStripParams();
    const auto ink = theme.ink;

    // Drop shadow (light mode only)
    if (! theme.isDark)
    {
        g.setColour (ink.withAlpha (0.25f));
        g.fillRoundedRectangle (r.translated (3.0f, 3.0f), 6.0f);
    }

    // Master Paper Panel
    g.setColour (theme.panel);
    g.fillRoundedRectangle (r, 6.0f);
    drawSketchRect (g, r, theme.panelEdge, 2.0f, 90);

    // Master Header Badge: Gold/Amber warm comic pill
    auto badgeR = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 4.0f, r.getWidth() - 8.0f, 28.0f);
    if (! theme.isDark)
    {
        g.setColour (ink.withAlpha (0.25f));
        g.fillRoundedRectangle (badgeR.translated (1.5f, 1.5f), 4.0f);
    }
    g.setColour (juce::Colour (0xffffeaa7)); // warm golden comic pill
    g.fillRoundedRectangle (badgeR, 4.0f);
    drawSketchRect (g, badgeR, theme.isDark ? theme.panelEdge : ink, 1.5f, 91);

    g.setColour (juce::Colour (0xff101114));
    g.setFont (displayFont (12.5f));
    g.drawText ("MASTER BUS", badgeR, juce::Justification::centred, false);

    // Master Pan Bar
    g.setColour (theme.trough);
    g.fillRoundedRectangle (panBarBounds, 3.0f);
    drawSketchRect (g, panBarBounds, theme.panelEdge, 1.2f, 92);

    const float centerX = panBarBounds.getCentreX();
    const float panOffset = params.pan * (panBarBounds.getWidth() * 0.45f);
    if (std::abs (params.pan) > 0.02f)
    {
        g.setColour (theme.accent);
        if (params.pan > 0.0f)
            g.fillRect (centerX, panBarBounds.getY() + 2.0f, panOffset, panBarBounds.getHeight() - 4.0f);
        else
            g.fillRect (centerX + panOffset, panBarBounds.getY() + 2.0f, -panOffset, panBarBounds.getHeight() - 4.0f);
    }
    g.setColour (theme.ink);
    g.drawVerticalLine (static_cast<int> (centerX), panBarBounds.getY(), panBarBounds.getBottom());

    // Fader Track: paper trough
    g.setColour (theme.trough);
    g.fillRoundedRectangle (faderTrackBounds, 4.0f);
    drawSketchRect (g, faderTrackBounds, theme.panelEdge, 1.2f, 93);

    // 0dB mark
    const float zeroY = faderTrackBounds.getBottom() - 0.75f * faderTrackBounds.getHeight();
    g.setColour (theme.ink.withAlpha (0.6f));
    g.drawHorizontalLine (static_cast<int> (zeroY), faderTrackBounds.getX() - 3.0f, faderTrackBounds.getX());

    // Dual meters (L and R) with discrete comic segments
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto mR = (ch == 0 ? meterLBounds : meterRBounds);
        g.setColour (theme.trough);
        g.fillRoundedRectangle (mR, 2.0f);
        drawSketchRect (g, mR, theme.panelEdge, 1.0f, static_cast<uint32_t> (94 + ch));

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
            constexpr int numSegs = 16;
            const int activeSegs = std::clamp (static_cast<int> (normRms * numSegs), 1, numSegs);
            const float segH = mH / numSegs;

            for (int s = 0; s < activeSegs; ++s)
            {
                const float sy = mY + mH - (s + 1) * segH;
                juce::Colour segCol = juce::Colour (0xff2ecc71); // Green
                if (s >= 13) segCol = juce::Colour (0xffe74c3c); // Red peak
                else if (s >= 10) segCol = juce::Colour (0xfff1c40f); // Amber / Yellow

                g.setColour (segCol);
                g.fillRoundedRectangle (mX, sy + 0.5f, mW, segH - 1.0f, 1.0f);
            }
        }

        if (normPeak > 0.02f)
        {
            const float peakY = mY + mH - normPeak * mH;
            g.setColour (normPeak >= 0.98f ? juce::Colour (0xffe74c3c) : theme.ink);
            g.fillRect (mX, peakY - 1.0f, mW, 2.0f);
        }
    }

    // Master Fader Cap: clean paper cap with ink border & crimson stripe
    const float normGain = gainToNormalized (params.level);
    const float thumbY = faderTrackBounds.getBottom() - normGain * faderTrackBounds.getHeight() - 10.0f;
    auto thumbR = juce::Rectangle<float> (faderTrackBounds.getX() - 4.0f, thumbY, faderTrackBounds.getWidth() + 8.0f, 20.0f);

    if (! theme.isDark)
    {
        g.setColour (ink.withAlpha (0.4f));
        g.fillRoundedRectangle (thumbR.translated (2.0f, 2.0f), 3.0f);
    }

    g.setColour (theme.panel);
    g.fillRoundedRectangle (thumbR, 3.0f);
    drawSketchRect (g, thumbR, theme.panelEdge, 1.8f, 97);

    // Crimson master stripe
    g.setColour (juce::Colour (0xffe74c3c));
    g.fillRect (thumbR.getX() + 2.0f, thumbR.getCentreY() - 1.5f, thumbR.getWidth() - 4.0f, 3.0f);

    // Master dB display
    const bool isCompact = (r.getHeight() < 300.0f);
    auto dbRect = juce::Rectangle<float> (r.getX() + 6.0f, isCompact ? (r.getBottom() - 24.0f) : (r.getBottom() - 26.0f),
                                          r.getWidth() - 12.0f, isCompact ? 18.0f : 20.0f);
    g.setColour (theme.trough);
    g.fillRoundedRectangle (dbRect, 3.0f);
    drawSketchRect (g, dbRect, theme.panelEdge, 1.2f, 98);

    g.setColour (params.level > 1.0f ? juce::Colour (0xffd03020) : theme.ink);
    g.setFont (displayFont (isCompact ? 9.0f : 10.0f));
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
    setTheme (owner.theme);
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

void MixerView::ChannelFXPopup::setTheme (const Theme& t)
{
    theme = t;
    closeBtn.setTheme (t);
    eqToggle.setTheme (t);
    compToggle.setTheme (t);
    if (eqLowGainKnob) eqLowGainKnob->setTheme (t);
    if (eqMidFreqKnob) eqMidFreqKnob->setTheme (t);
    if (eqMidGainKnob) eqMidGainKnob->setTheme (t);
    if (eqHighGainKnob) eqHighGainKnob->setTheme (t);
    if (compThreshKnob) compThreshKnob->setTheme (t);
    if (compRatioKnob) compRatioKnob->setTheme (t);
    if (compAttackKnob) compAttackKnob->setTheme (t);
    if (compReleaseKnob) compReleaseKnob->setTheme (t);
    if (compMakeupKnob) compMakeupKnob->setTheme (t);
    if (satDriveKnob) satDriveKnob->setTheme (t);
    if (transAttackKnob) transAttackKnob->setTheme (t);
    if (transSustainKnob) transSustainKnob->setTheme (t);
    repaint();
}

void MixerView::ChannelFXPopup::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto ink = theme.ink;

    // Backdrop shadow (light mode only)
    if (! theme.isDark)
    {
        g.setColour (ink.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (4.0f, 4.0f), 8.0f);
    }

    // Card background
    g.setColour (theme.panel);
    g.fillRoundedRectangle (r, 8.0f);
    drawSketchRect (g, r, theme.panelEdge, 2.2f, 101);

    // Header banner
    auto headerR = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 4.0f, r.getWidth() - 8.0f, 32.0f);
    g.setColour (juce::Colour (0xffffeaa7)); // Golden banner
    g.fillRoundedRectangle (headerR, 6.0f);
    drawSketchRect (g, headerR, theme.isDark ? theme.panelEdge : ink, 1.4f, 102);

    const auto name = currentChannel >= 0 ? processor.getSampler().micStripTitle (currentChannel) : "";
    g.setColour (juce::Colour (0xff101114));
    g.setFont (displayFont (16.0f));
    g.drawText ("CHANNEL FX RACK: " + name.toUpperCase(), 24, 14, getWidth() - 120, 24, juce::Justification::left, false);

    // Dividing lines
    g.setColour (theme.ink.withAlpha (0.25f));
    g.drawHorizontalLine (112, 20.0f, getWidth() - 20.0f);
    g.drawHorizontalLine (186, 20.0f, getWidth() - 20.0f);

    // Section labels
    g.setColour (theme.ink);
    g.setFont (displayFont (11.5f));
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
    setTheme (owner.theme);
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

void MixerView::MasterFXPopup::setTheme (const Theme& t)
{
    theme = t;
    closeBtn.setTheme (t);
    limiterToggle.setTheme (t);
    if (revReturnKnob) revReturnKnob->setTheme (t);
    if (revSizeKnob) revSizeKnob->setTheme (t);
    if (revDampKnob) revDampKnob->setTheme (t);
    if (revWidthKnob) revWidthKnob->setTheme (t);
    repaint();
}

void MixerView::MasterFXPopup::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto ink = theme.ink;

    // Backdrop shadow (light mode only)
    if (! theme.isDark)
    {
        g.setColour (ink.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (4.0f, 4.0f), 8.0f);
    }

    // Card background
    g.setColour (theme.panel);
    g.fillRoundedRectangle (r, 8.0f);
    drawSketchRect (g, r, theme.panelEdge, 2.2f, 103);

    // Header banner
    auto headerR = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 4.0f, r.getWidth() - 8.0f, 32.0f);
    g.setColour (theme.isDark ? juce::Colour (0xff3a3c44) : juce::Colour (0xffdfe6e9));
    g.fillRoundedRectangle (headerR, 6.0f);
    drawSketchRect (g, headerR, theme.isDark ? theme.panelEdge : ink, 1.4f, 104);

    g.setColour (theme.isDark ? juce::Colours::white : ink);
    g.setFont (displayFont (15.0f));
    g.drawText ("MASTER REVERB & DYNAMICS", 24, 14, getWidth() - 120, 24, juce::Justification::left, false);
}

void MixerView::MasterFXPopup::mouseDown (const juce::MouseEvent& e)
{
    juce::ignoreUnused (e);
}

//==============================================================================
// Vector Drum Piece Icon Renderer (Hand-drawn comic/sketch style)
//==============================================================================
static void drawPieceVectorIcon (juce::Graphics& g, BleedPiece piece, juce::Rectangle<float> bounds,
                                 juce::Colour colour, float strokeWidth)
{
    g.setColour (colour);
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();

    switch (piece)
    {
        case BleedPiece::kick:
        {
            const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.38f;
            g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, strokeWidth);
            g.fillEllipse (cx - r * 0.28f, cy - r * 0.28f, r * 0.56f, r * 0.56f);
            juce::Path spurs;
            spurs.startNewSubPath (cx - r * 0.707f, cy + r * 0.707f);
            spurs.lineTo (cx - r * 1.15f, cy + r * 1.15f);
            spurs.startNewSubPath (cx + r * 0.707f, cy + r * 0.707f);
            spurs.lineTo (cx + r * 1.15f, cy + r * 1.15f);
            g.strokePath (spurs, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case BleedPiece::snare:
        {
            const float w = bounds.getWidth() * 0.82f;
            const float h = bounds.getHeight() * 0.52f;
            const float x = cx - w * 0.5f;
            const float y = cy - h * 0.5f;
            g.drawEllipse (x, y, w, h * 0.45f, strokeWidth);
            juce::Path shell;
            shell.startNewSubPath (x, y + h * 0.225f);
            shell.lineTo (x, y + h);
            shell.startNewSubPath (x + w, y + h * 0.225f);
            shell.lineTo (x + w, y + h);
            shell.startNewSubPath (x, y + h);
            shell.quadraticTo (cx, y + h + h * 0.25f, x + w, y + h);
            // Snare wires across bottom
            shell.startNewSubPath (x + w * 0.18f, y + h * 0.65f);
            shell.lineTo (x + w * 0.34f, y + h * 0.52f);
            shell.lineTo (x + w * 0.50f, y + h * 0.65f);
            shell.lineTo (x + w * 0.66f, y + h * 0.52f);
            shell.lineTo (x + w * 0.82f, y + h * 0.65f);
            g.strokePath (shell, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case BleedPiece::tom1:
        case BleedPiece::tom2:
        {
            const float scale = (piece == BleedPiece::tom1 ? 0.72f : 0.80f);
            const float w = bounds.getWidth() * scale;
            const float h = bounds.getHeight() * (scale * 0.75f);
            const float x = cx - w * 0.5f;
            const float y = cy - h * 0.5f;
            g.drawEllipse (x, y, w, h * 0.40f, strokeWidth);
            juce::Path shell;
            shell.startNewSubPath (x, y + h * 0.20f);
            shell.lineTo (x, y + h);
            shell.startNewSubPath (x + w, y + h * 0.20f);
            shell.lineTo (x + w, y + h);
            shell.startNewSubPath (x, y + h);
            shell.quadraticTo (cx, y + h + h * 0.25f, x + w, y + h);
            g.strokePath (shell, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case BleedPiece::floorTom:
        {
            const float w = bounds.getWidth() * 0.80f;
            const float h = bounds.getHeight() * 0.58f;
            const float x = cx - w * 0.5f;
            const float y = cy - h * 0.55f;
            g.drawEllipse (x, y, w, h * 0.38f, strokeWidth);
            juce::Path shell;
            shell.startNewSubPath (x, y + h * 0.19f);
            shell.lineTo (x, y + h);
            shell.startNewSubPath (x + w, y + h * 0.19f);
            shell.lineTo (x + w, y + h);
            shell.startNewSubPath (x, y + h);
            shell.quadraticTo (cx, y + h + h * 0.22f, x + w, y + h);
            // 3 Floor Tom legs
            shell.startNewSubPath (x + w * 0.15f, y + h * 0.6f);
            shell.lineTo (x - 2.0f, bounds.getBottom() - 1.0f);
            shell.startNewSubPath (x + w * 0.85f, y + h * 0.6f);
            shell.lineTo (x + w + 2.0f, bounds.getBottom() - 1.0f);
            shell.startNewSubPath (cx, y + h * 0.8f);
            shell.lineTo (cx, bounds.getBottom() - 1.0f);
            g.strokePath (shell, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case BleedPiece::hihat:
        {
            const float w = bounds.getWidth() * 0.82f;
            g.drawLine (cx, bounds.getY() + 1.0f, cx, bounds.getBottom() - 1.0f, strokeWidth);
            juce::Path cymbals;
            // Top cymbal arc
            cymbals.startNewSubPath (cx - w * 0.5f, cy - 2.5f);
            cymbals.quadraticTo (cx, cy - 6.0f, cx + w * 0.5f, cy - 2.5f);
            // Bottom cymbal arc
            cymbals.startNewSubPath (cx - w * 0.5f, cy + 2.5f);
            cymbals.quadraticTo (cx, cy + 6.0f, cx + w * 0.5f, cy + 2.5f);
            // Stand pedal base line
            cymbals.startNewSubPath (cx - w * 0.35f, bounds.getBottom() - 1.0f);
            cymbals.lineTo (cx + w * 0.35f, bounds.getBottom() - 1.0f);
            g.strokePath (cymbals, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case BleedPiece::ride:
        {
            const float w = bounds.getWidth() * 0.86f;
            juce::Path p;
            p.startNewSubPath (cx - w * 0.5f, cy + 2.0f);
            p.quadraticTo (cx - w * 0.14f, cy - 3.5f, cx - w * 0.10f, cy - 3.5f);
            p.quadraticTo (cx, cy - 8.0f, cx + w * 0.10f, cy - 3.5f);
            p.quadraticTo (cx + w * 0.14f, cy - 3.5f, cx + w * 0.5f, cy + 2.0f);
            p.startNewSubPath (cx, cy - 1.0f);
            p.lineTo (cx, bounds.getBottom() - 1.0f);
            g.strokePath (p, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case BleedPiece::crash1:
        case BleedPiece::crash2:
        {
            const float w = bounds.getWidth() * 0.82f;
            const float tilt = (piece == BleedPiece::crash1 ? -2.5f : 2.5f);
            juce::Path p;
            p.startNewSubPath (cx - w * 0.5f, cy + tilt + 1.5f);
            p.quadraticTo (cx, cy + tilt - 4.5f, cx + w * 0.5f, cy - tilt - 1.5f);
            p.startNewSubPath (cx, cy);
            p.lineTo (cx, bounds.getBottom() - 1.0f);
            // Vibrato slash accents
            p.startNewSubPath (cx - w * 0.35f, cy - 5.0f);
            p.lineTo (cx - w * 0.45f, cy - 8.0f);
            p.startNewSubPath (cx + w * 0.35f, cy - 5.0f);
            p.lineTo (cx + w * 0.45f, cy - 8.0f);
            g.strokePath (p, juce::PathStrokeType (strokeWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case BleedPiece::count:
        default:
            break;
    }
}

//==============================================================================
// BleedMatrixPopup
//==============================================================================

MixerView::BleedMatrixPopup::BleedMatrixPopup (MixerView& o, PluginProcessor& proc)
    : owner (o), processor (proc)
{
    closeBtn.onClick = [this]() { close(); };
    addAndMakeVisible (closeBtn);

    // Preset buttons
    naturalBtn.setButtonText ("NATURAL");
    naturalBtn.onClick = [this]() {
        processor.getSampler().getBleedMatrix().resetToNatural();
        updateFromState();
    };
    addAndMakeVisible (naturalBtn);

    cleanBtn.setButtonText ("CLEAN (DRY)");
    cleanBtn.onClick = [this]() {
        processor.getSampler().getBleedMatrix().resetToClean();
        updateFromState();
    };
    addAndMakeVisible (cleanBtn);

    liveBtn.setButtonText ("LIVE ROOM");
    liveBtn.onClick = [this]() {
        processor.getSampler().getBleedMatrix().resetToLive();
        updateFromState();
    };
    addAndMakeVisible (liveBtn);

    ohOnlyBtn.setButtonText ("OH ONLY");
    ohOnlyBtn.onClick = [this]() {
        const int numMics = processor.getSampler().numMicStrips();
        std::vector<std::string> names;
        for (int i = 0; i < numMics; ++i)
            names.push_back (processor.getSampler().micStripTitle (i).toStdString());
        processor.getSampler().getBleedMatrix().resetToOverheadsOnly (names);
        updateFromState();
    };
    addAndMakeVisible (ohOnlyBtn);

    // Modes
    hybridModeBtn.setButtonText ("HYBRID ACOUSTIC");
    hybridModeBtn.activeColour = juce::Colour (0xff2ecc71);
    hybridModeBtn.onClick = [this]() {
        processor.getSampler().getBleedMatrix().setMode (BleedMatrix::Mode::hybridAcoustic);
        updateFromState();
    };
    addAndMakeVisible (hybridModeBtn);

    recordedModeBtn.setButtonText ("RECORDED ONLY");
    recordedModeBtn.activeColour = juce::Colour (0xff3498db);
    recordedModeBtn.onClick = [this]() {
        processor.getSampler().getBleedMatrix().setMode (BleedMatrix::Mode::recordedOnly);
        updateFromState();
    };
    addAndMakeVisible (recordedModeBtn);

    disabledModeBtn.setButtonText ("MUTE BLEED");
    disabledModeBtn.activeColour = juce::Colour (0xffe74c3c);
    disabledModeBtn.onClick = [this]() {
        processor.getSampler().getBleedMatrix().setMode (BleedMatrix::Mode::disabled);
        updateFromState();
    };
    addAndMakeVisible (disabledModeBtn);

    masterBleedKnob = std::make_unique<Knob> ("BLEED LEVEL", 0.0f, 2.0f, 1.0f, "x");
    masterBleedKnob->onValueChanged = [this](float v) {
        processor.getSampler().getBleedMatrix().setMasterBleed (v);
        repaint();
    };
    addAndMakeVisible (*masterBleedKnob);

    sympResonanceKnob = std::make_unique<Knob> ("SNARE BUZZ", 0.0f, 1.0f, 0.35f, "");
    sympResonanceKnob->onValueChanged = [this](float v) {
        processor.getSampler().getBleedMatrix().setSympatheticResonance (v);
        repaint();
    };
    addAndMakeVisible (*sympResonanceKnob);

    updateFromState();
    setTheme (owner.theme);
}

void MixerView::BleedMatrixPopup::open (int highlightMic)
{
    highlightedMicIndex = highlightMic;
    updateFromState();
    owner.setBleedMatrixOpen (true);
    repaint();
}

void MixerView::BleedMatrixPopup::close()
{
    highlightedMicIndex = -1;
    owner.setBleedMatrixOpen (false);
}

void MixerView::BleedMatrixPopup::updateFromState()
{
    const auto& bm = processor.getSampler().getBleedMatrix();
    if (masterBleedKnob)
        masterBleedKnob->setValue (bm.getMasterBleed());
    if (sympResonanceKnob)
        sympResonanceKnob->setValue (bm.getSympatheticResonance());

    hybridModeBtn.setActive (bm.getMode() == BleedMatrix::Mode::hybridAcoustic);
    recordedModeBtn.setActive (bm.getMode() == BleedMatrix::Mode::recordedOnly);
    disabledModeBtn.setActive (bm.getMode() == BleedMatrix::Mode::disabled);

    repaint();
}

void MixerView::BleedMatrixPopup::setTheme (const Theme& t)
{
    theme = t;
    closeBtn.setTheme (t);
    naturalBtn.setTheme (t);
    cleanBtn.setTheme (t);
    liveBtn.setTheme (t);
    ohOnlyBtn.setTheme (t);
    hybridModeBtn.setTheme (t);
    recordedModeBtn.setTheme (t);
    disabledModeBtn.setTheme (t);
    if (masterBleedKnob) masterBleedKnob->setTheme (t);
    if (sympResonanceKnob) sympResonanceKnob->setTheme (t);
    repaint();
}

void MixerView::BleedMatrixPopup::resized()
{
    closeBtn.setBounds (getWidth() - 76, 6, 68, 22);

    const int topBtnY = 32;
    naturalBtn.setBounds (12,  topBtnY, 68, 22);
    cleanBtn.setBounds   (84,  topBtnY, 82, 22);
    liveBtn.setBounds    (170, topBtnY, 78, 22);
    ohOnlyBtn.setBounds  (252, topBtnY, 68, 22);

    hybridModeBtn.setBounds   (326, topBtnY, 116, 22);
    recordedModeBtn.setBounds (446, topBtnY, 108, 22);
    disabledModeBtn.setBounds (558, topBtnY, 94,  22);

    if (masterBleedKnob)
        masterBleedKnob->setBounds (getWidth() - 146, 24, 52, 52);
    if (sympResonanceKnob)
        sympResonanceKnob->setBounds (getWidth() - 76,  24, 52, 52);

    gridBounds = juce::Rectangle<int> (12, 80, getWidth() - 24, getHeight() - 102);

    const int numMics = std::min (processor.getSampler().numMicStrips(), 32);
    headerColWidth = 104;
    colWidth = (numMics > 0) ? std::max (60, (gridBounds.getWidth() - headerColWidth) / numMics) : 70;
    rowHeight = std::clamp ((gridBounds.getHeight() - 2) / (BleedMatrix::maxPieces + 1), 16, 24);
}

bool MixerView::BleedMatrixPopup::getCellAtPos (juce::Point<int> pos, int& outPiece, int& outMic) const
{
    const int numMics = std::min (processor.getSampler().numMicStrips(), 32);
    if (numMics <= 0)
        return false;

    const int x0 = gridBounds.getX() + headerColWidth;
    const int y0 = gridBounds.getY() + rowHeight;

    if (pos.x < x0 || pos.y < y0)
        return false;

    const int c = (pos.x - x0) / colWidth;
    const int r = (pos.y - y0) / rowHeight;

    if (c >= 0 && c < numMics && r >= 0 && r < BleedMatrix::maxPieces)
    {
        outPiece = r;
        outMic = c;
        return true;
    }
    return false;
}

bool MixerView::BleedMatrixPopup::getRowMuteAtPos (juce::Point<int> pos, int& outPiece) const
{
    const int startX = gridBounds.getX();
    const int startY = gridBounds.getY();
    const int y0 = startY + rowHeight;

    if (pos.x < startX || pos.x > startX + headerColWidth || pos.y < y0)
        return false;

    const int r = (pos.y - y0) / rowHeight;
    if (r >= 0 && r < BleedMatrix::maxPieces)
    {
        // [M] button sits in the rightmost 20px of the row header cell
        if (pos.x >= (startX + headerColWidth - 22) && pos.x <= (startX + headerColWidth - 4))
        {
            outPiece = r;
            return true;
        }
    }
    return false;
}

bool MixerView::BleedMatrixPopup::getColMuteAtPos (juce::Point<int> pos, int& outMic) const
{
    const int numMics = std::min (processor.getSampler().numMicStrips(), 32);
    if (numMics <= 0)
        return false;

    const int startX = gridBounds.getX() + headerColWidth;
    const int startY = gridBounds.getY();

    if (pos.x < startX || pos.y < startY || pos.y > startY + rowHeight)
        return false;

    const int c = (pos.x - startX) / colWidth;
    if (c >= 0 && c < numMics)
    {
        const int colX = startX + c * colWidth;
        // [M] button sits in the rightmost 20px of the col header cell
        if (pos.x >= (colX + colWidth - 20) && pos.x <= (colX + colWidth - 2))
        {
            outMic = c;
            return true;
        }
    }
    return false;
}

void MixerView::BleedMatrixPopup::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto ink = theme.ink;

    // Drop shadow (light mode)
    if (! theme.isDark)
    {
        g.setColour (ink.withAlpha (0.30f));
        g.fillRoundedRectangle (r.translated (3.0f, 3.0f), 6.0f);
    }

    // Panel background
    g.setColour (theme.panel);
    g.fillRoundedRectangle (r, 6.0f);
    drawSketchRect (g, r, theme.panelEdge, 2.0f, 204);

    // Header banner
    auto headerR = juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 3.0f, r.getWidth() - 8.0f, 26.0f);
    g.setColour (theme.isDark ? juce::Colour (0xff34363d) : juce::Colour (0xffdfe6e9));
    g.fillRoundedRectangle (headerR, 5.0f);
    drawSketchRect (g, headerR, theme.isDark ? theme.panelEdge : ink, 1.4f, 205);

    g.setColour (theme.isDark ? juce::Colours::white : ink);
    g.setFont (displayFont (13.5f));
    g.drawText ("ACOUSTIC BLEED & LEAKAGE MATRIX", 16, 7, 300, 18, juce::Justification::left, false);

    g.setColour (theme.ink.withAlpha (0.65f));
    g.setFont (displayFont (9.5f));
    g.drawText ("Logical Pieces x Mic Channels Leakage Factor", 305, 8, 300, 16, juce::Justification::left, false);

    const int numMics = std::min (processor.getSampler().numMicStrips(), 32);
    if (numMics <= 0)
    {
        g.setColour (theme.ink);
        g.setFont (displayFont (13.0f));
        g.drawText ("(Load a multi-microphone drum kit to view and tweak acoustic bleed)",
                    getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    auto& bm = processor.getSampler().getBleedMatrix();

    // Table Header Row
    const int startX = gridBounds.getX();
    const int startY = gridBounds.getY();

    // Corner cell
    auto cornerR = juce::Rectangle<float> (static_cast<float> (startX), static_cast<float> (startY),
                                           static_cast<float> (headerColWidth), static_cast<float> (rowHeight));
    g.setColour (theme.trough);
    g.fillRoundedRectangle (cornerR, 3.0f);
    drawSketchRect (g, cornerR, theme.panelEdge, 1.2f, 301);
    g.setColour (theme.ink);
    g.setFont (displayFont (9.5f));
    g.drawText ("PIECE \\ MIC", cornerR, juce::Justification::centred, false);

    // Column Headers (Mic names with channel accent color matching top strips)
    for (int m = 0; m < numMics; ++m)
    {
        auto colR = juce::Rectangle<float> (static_cast<float> (startX + headerColWidth + m * colWidth),
                                            static_cast<float> (startY),
                                            static_cast<float> (colWidth - 1),
                                            static_cast<float> (rowHeight));
        const bool isHighlighted = (m == highlightedMicIndex || m == hoveredMic);
        const auto micName = processor.getSampler().micStripTitle (m);
        const auto micAccent = getMicColor (micName);

        // Header background with strip accent tint
        g.setColour (isHighlighted ? micAccent.withAlpha (0.35f) : theme.trough);
        g.fillRoundedRectangle (colR, 3.0f);

        // Top accent strip matching the channel strip header above
        g.setColour (micAccent);
        g.fillRect (colR.getX(), colR.getY(), colR.getWidth(), 3.0f);

        drawSketchRect (g, colR, isHighlighted ? juce::Colour (0xffe67e22) : theme.panelEdge, isHighlighted ? 1.6f : 1.1f, static_cast<uint32_t> (310 + m));

        // Check if all cells in column are muted
        bool allColMuted = true;
        for (int p = 0; p < BleedMatrix::maxPieces; ++p)
        {
            if (! bm.isCellMuted (p, m))
            {
                allColMuted = false;
                break;
            }
        }

        // Column Mute button [M]
        auto colMuteR = juce::Rectangle<float> (colR.getRight() - 17.0f, colR.getY() + 4.0f, 14.0f, static_cast<float> (rowHeight - 6));
        if (allColMuted)
        {
            g.setColour (juce::Colour (0xffe74c3c));
            g.fillRoundedRectangle (colMuteR, 2.0f);
            g.setColour (juce::Colours::white);
        }
        else
        {
            g.setColour (hoveredColMuteMic == m ? theme.ink.withAlpha (0.25f) : theme.panel.withAlpha (0.4f));
            g.fillRoundedRectangle (colMuteR, 2.0f);
            g.setColour (theme.ink.withAlpha (0.7f));
        }
        g.setFont (displayFont (8.0f));
        g.drawText ("M", colMuteR, juce::Justification::centred, false);

        // Column label
        auto textR = juce::Rectangle<int> (static_cast<int> (colR.getX() + 2.0f), static_cast<int> (colR.getY() + 2.0f),
                                           static_cast<int> (colR.getWidth() - 18.0f), static_cast<int> (colR.getHeight() - 2.0f));
        g.setColour (isHighlighted ? (theme.isDark ? juce::Colours::white : theme.ink) : theme.ink);
        g.setFont (displayFont (9.0f));
        g.drawFittedText (micName, textR, juce::Justification::centred, 1);
    }

    // Rows (9 logical pieces)
    for (int p = 0; p < BleedMatrix::maxPieces; ++p)
    {
        const int rowY = startY + (p + 1) * rowHeight;
        const auto pieceEnum = static_cast<BleedPiece> (p);

        // Row Header (Piece vector icon + name + macro mute)
        auto rowHdrR = juce::Rectangle<float> (static_cast<float> (startX),
                                               static_cast<float> (rowY),
                                               static_cast<float> (headerColWidth),
                                               static_cast<float> (rowHeight - 1));
        const bool isRowHovered = (hoveredPiece == p);
        g.setColour (isRowHovered ? theme.trough.brighter (0.08f) : theme.trough);
        g.fillRoundedRectangle (rowHdrR, 3.0f);
        drawSketchRect (g, rowHdrR, theme.panelEdge, 1.1f, static_cast<uint32_t> (400 + p));

        // Vector Icon on the left
        auto iconR = juce::Rectangle<float> (rowHdrR.getX() + 3.0f, rowHdrR.getY() + 2.0f,
                                             static_cast<float> (rowHeight - 5), static_cast<float> (rowHeight - 5));
        drawPieceVectorIcon (g, pieceEnum, iconR, theme.ink, 1.3f);

        // Piece Name text
        auto nameR = juce::Rectangle<float> (rowHdrR.getX() + static_cast<float> (rowHeight), rowHdrR.getY(),
                                             rowHdrR.getWidth() - static_cast<float> (rowHeight) - 20.0f, rowHdrR.getHeight());
        g.setColour (theme.ink);
        g.setFont (displayFont (9.5f));
        g.drawText (bleedPieceDisplayName (pieceEnum), nameR, juce::Justification::centredLeft, false);

        // Check if all cells in row are muted
        bool allRowMuted = true;
        for (int m = 0; m < numMics; ++m)
        {
            if (! bm.isCellMuted (p, m))
            {
                allRowMuted = false;
                break;
            }
        }

        // Row Mute button [M]
        auto rowMuteR = juce::Rectangle<float> (rowHdrR.getRight() - 17.0f, rowHdrR.getY() + 3.0f, 14.0f, static_cast<float> (rowHeight - 6));
        if (allRowMuted)
        {
            g.setColour (juce::Colour (0xffe74c3c));
            g.fillRoundedRectangle (rowMuteR, 2.0f);
            g.setColour (juce::Colours::white);
        }
        else
        {
            g.setColour (hoveredRowMutePiece == p ? theme.ink.withAlpha (0.25f) : theme.panel.withAlpha (0.4f));
            g.fillRoundedRectangle (rowMuteR, 2.0f);
            g.setColour (theme.ink.withAlpha (0.7f));
        }
        g.setFont (displayFont (8.0f));
        g.drawText ("M", rowMuteR, juce::Justification::centred, false);

        // Cells for this piece
        for (int m = 0; m < numMics; ++m)
        {
            auto cellR = juce::Rectangle<float> (static_cast<float> (startX + headerColWidth + m * colWidth),
                                                 static_cast<float> (rowY),
                                                 static_cast<float> (colWidth - 1),
                                                 static_cast<float> (rowHeight - 1));
            const float cellGain = bm.getCellGain (p, m);
            const bool isMuted = bm.isCellMuted (p, m);
            const bool isCellHovered = (hoveredPiece == p && hoveredMic == m);
            const bool isHighlighted = (m == highlightedMicIndex) || isCellHovered;

            // Background base
            g.setColour (theme.panel.withAlpha (0.85f));
            g.fillRoundedRectangle (cellR, 2.0f);

            if (isMuted)
            {
                // Muted state: soft red fill + slash
                g.setColour (juce::Colour (0x28e74c3c));
                g.fillRoundedRectangle (cellR, 2.0f);
                g.setColour (juce::Colour (0x60e74c3c));
                g.drawLine (cellR.getX() + 4.0f, cellR.getBottom() - 4.0f, cellR.getRight() - 4.0f, cellR.getY() + 4.0f, 1.2f);
            }
            else
            {
                // Heatmap fill bar: bipolar around unity 1.0f (0 dB)
                const float centerTick = cellR.getX() + cellR.getWidth() * 0.5f;
                if (cellGain > 1.02f)
                {
                    // Boost: extends to the right in warm amber
                    const float boostRatio = std::clamp ((cellGain - 1.0f) / 1.0f, 0.0f, 1.0f);
                    const float barW = (cellR.getWidth() * 0.48f) * boostRatio;
                    auto barR = juce::Rectangle<float> (centerTick, cellR.getY() + 1.0f, barW, cellR.getHeight() - 2.0f);
                    g.setColour (juce::Colour (0x45f39c12));
                    g.fillRoundedRectangle (barR, 1.5f);
                }
                else if (cellGain < 0.98f)
                {
                    // Cut: extends to the left in cool cyan
                    const float cutRatio = std::clamp ((1.0f - cellGain) / 1.0f, 0.0f, 1.0f);
                    const float barW = (cellR.getWidth() * 0.48f) * cutRatio;
                    auto barR = juce::Rectangle<float> (centerTick - barW, cellR.getY() + 1.0f, barW, cellR.getHeight() - 2.0f);
                    g.setColour (juce::Colour (0x353498db));
                    g.fillRoundedRectangle (barR, 1.5f);
                }

                // Unity center tick
                g.setColour (theme.panelEdge.withAlpha (0.4f));
                g.drawVerticalLine (static_cast<int> (centerTick), cellR.getY() + 2.0f, cellR.getBottom() - 2.0f);
            }

            // Cell border
            drawSketchRect (g, cellR, isHighlighted ? juce::Colour (0xffe67e22) : theme.panelEdge, isHighlighted ? 1.5f : 1.0f, static_cast<uint32_t> (500 + p * 32 + m));

            // Cell Text
            g.setFont (displayFont (8.5f));
            if (isMuted)
            {
                g.setColour (juce::Colour (0xffe74c3c));
                g.drawText ("MUTE", cellR, juce::Justification::centred, false);
            }
            else
            {
                g.setColour (cellGain > 1.05f ? juce::Colour (0xffd35400) : theme.ink);
                const float db = (cellGain <= 0.001f) ? -60.0f : linearToDb (cellGain);
                juce::String label = (std::abs (db) < 0.05f) ? "0.0 dB" : ((db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB");
                g.drawText (label, cellR, juce::Justification::centred, false);
            }
        }
    }

    // Floating Tooltip Bubble when dragging or wheeling a cell
    if (isEditingValue && draggingPiece >= 0 && draggingMic >= 0)
    {
        auto bubbleR = juce::Rectangle<float> (static_cast<float> (activeEditPos.x - 34),
                                               static_cast<float> (activeEditPos.y - 32),
                                               68.0f, 22.0f);
        // Keep inside bounds
        bubbleR.setX (std::clamp (bubbleR.getX(), 4.0f, static_cast<float> (getWidth() - 74)));
        bubbleR.setY (std::max (4.0f, bubbleR.getY()));

        // Bubble drop shadow
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (bubbleR.translated (2.0f, 2.0f), 5.0f);

        // Bubble body
        g.setColour (theme.isDark ? juce::Colour (0xff2d3436) : juce::Colour (0xff2c3e50));
        g.fillRoundedRectangle (bubbleR, 5.0f);
        drawSketchRect (g, bubbleR, juce::Colour (0xffe67e22), 1.5f, 999);

        g.setColour (juce::Colours::white);
        g.setFont (displayFont (10.0f));
        juce::String tipText = (bm.isCellMuted (draggingPiece, draggingMic))
                                 ? "MUTED"
                                 : ((activeEditDb > 0.0f ? "+" : "") + juce::String (activeEditDb, 1) + " dB");
        g.drawText (tipText, bubbleR, juce::Justification::centred, false);
    }

    // Bottom Help Hint
    auto hintR = juce::Rectangle<float> (static_cast<float> (startX),
                                         static_cast<float> (getHeight() - 20),
                                         static_cast<float> (getWidth() - 24), 16.0f);
    g.setColour (theme.ink.withAlpha (0.6f));
    g.setFont (displayFont (9.0f));
    g.drawText ("Drag cell up/down or scroll wheel | Right-click/Shift-click to toggle MUTE | Double-click to reset (0 dB) | [M] buttons for row/col mute",
                hintR, juce::Justification::centred, false);
}

void MixerView::BleedMatrixPopup::mouseDown (const juce::MouseEvent& e)
{
    auto& bm = processor.getSampler().getBleedMatrix();
    const auto pos = e.getPosition();

    // 1. Check Row Mute [M] click
    int pMute = -1;
    if (getRowMuteAtPos (pos, pMute))
    {
        const int numMics = std::min (processor.getSampler().numMicStrips(), 32);
        bool anyUnmuted = false;
        for (int m = 0; m < numMics; ++m)
        {
            if (! bm.isCellMuted (pMute, m))
            {
                anyUnmuted = true;
                break;
            }
        }
        for (int m = 0; m < numMics; ++m)
            bm.setCellMute (pMute, m, anyUnmuted);

        repaint();
        return;
    }

    // 2. Check Col Mute [M] click
    int colMute = -1;
    if (getColMuteAtPos (pos, colMute))
    {
        bool anyUnmuted = false;
        for (int p = 0; p < BleedMatrix::maxPieces; ++p)
        {
            if (! bm.isCellMuted (p, colMute))
            {
                anyUnmuted = true;
                break;
            }
        }
        for (int p = 0; p < BleedMatrix::maxPieces; ++p)
            bm.setCellMute (p, colMute, anyUnmuted);

        repaint();
        return;
    }

    // 3. Check Cell click
    int p = -1, m = -1;
    if (getCellAtPos (pos, p, m))
    {
        if (e.mods.isRightButtonDown() || e.mods.isShiftDown() || e.mods.isAltDown())
        {
            bm.setCellMute (p, m, ! bm.isCellMuted (p, m));
            repaint();
            return;
        }

        draggingPiece = p;
        draggingMic = m;
        dragStartGain = bm.getCellGain (p, m);
        dragStartPos = pos;
        isEditingValue = true;
        const float db = (dragStartGain <= 0.001f) ? -60.0f : linearToDb (dragStartGain);
        activeEditDb = db;
        activeEditPos = pos;
        repaint();
    }
}

void MixerView::BleedMatrixPopup::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingPiece >= 0 && draggingMic >= 0)
    {
        const int deltaY = dragStartPos.y - e.y;
        const float newGain = std::clamp (dragStartGain + static_cast<float> (deltaY) * 0.015f, 0.0f, 2.0f);
        auto& bm = processor.getSampler().getBleedMatrix();
        bm.setCellGain (draggingPiece, draggingMic, newGain);
        if (bm.isCellMuted (draggingPiece, draggingMic) && newGain > 0.01f)
            bm.setCellMute (draggingPiece, draggingMic, false);

        activeEditDb = (newGain <= 0.001f) ? -60.0f : linearToDb (newGain);
        activeEditPos = e.getPosition();
        isEditingValue = true;
        repaint();
    }
}

void MixerView::BleedMatrixPopup::mouseUp (const juce::MouseEvent& /*e*/)
{
    draggingPiece = -1;
    draggingMic = -1;
    isEditingValue = false;
    repaint();
}

void MixerView::BleedMatrixPopup::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto& bm = processor.getSampler().getBleedMatrix();
    const auto pos = e.getPosition();

    // Double-click row header -> reset row to 0 dB
    int pRow = -1;
    if (getRowMuteAtPos (pos, pRow) || (pos.x >= gridBounds.getX() && pos.x <= gridBounds.getX() + headerColWidth
        && pos.y >= gridBounds.getY() + rowHeight))
    {
        const int p = (pos.y - (gridBounds.getY() + rowHeight)) / rowHeight;
        if (p >= 0 && p < BleedMatrix::maxPieces)
        {
            const int numMics = std::min (processor.getSampler().numMicStrips(), 32);
            for (int m = 0; m < numMics; ++m)
            {
                bm.setCellGain (p, m, 1.0f);
                bm.setCellMute (p, m, false);
            }
            repaint();
            return;
        }
    }

    // Double-click col header -> reset col to 0 dB
    int colMute = -1;
    if (getColMuteAtPos (pos, colMute) || (pos.y >= gridBounds.getY() && pos.y <= gridBounds.getY() + rowHeight
        && pos.x >= gridBounds.getX() + headerColWidth))
    {
        const int m = (pos.x - (gridBounds.getX() + headerColWidth)) / colWidth;
        const int numMics = std::min (processor.getSampler().numMicStrips(), 32);
        if (m >= 0 && m < numMics)
        {
            for (int p = 0; p < BleedMatrix::maxPieces; ++p)
            {
                bm.setCellGain (p, m, 1.0f);
                bm.setCellMute (p, m, false);
            }
            repaint();
            return;
        }
    }

    // Double-click cell -> reset cell to 0 dB
    int p = -1, m = -1;
    if (getCellAtPos (pos, p, m))
    {
        bm.setCellGain (p, m, 1.0f);
        bm.setCellMute (p, m, false);
        repaint();
    }
}

void MixerView::BleedMatrixPopup::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    int p = -1, m = -1;
    bool needRepaint = false;

    if (getRowMuteAtPos (pos, p))
    {
        if (hoveredRowMutePiece != p) { hoveredRowMutePiece = p; needRepaint = true; }
        hoveredColMuteMic = -1;
        hoveredPiece = -1;
        hoveredMic = -1;
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    else if (getColMuteAtPos (pos, m))
    {
        if (hoveredColMuteMic != m) { hoveredColMuteMic = m; needRepaint = true; }
        hoveredRowMutePiece = -1;
        hoveredPiece = -1;
        hoveredMic = -1;
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    else if (getCellAtPos (pos, p, m))
    {
        if (hoveredPiece != p || hoveredMic != m) { hoveredPiece = p; hoveredMic = m; needRepaint = true; }
        hoveredRowMutePiece = -1;
        hoveredColMuteMic = -1;
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }
    else
    {
        if (hoveredPiece != -1 || hoveredMic != -1 || hoveredRowMutePiece != -1 || hoveredColMuteMic != -1)
        {
            hoveredPiece = -1;
            hoveredMic = -1;
            hoveredRowMutePiece = -1;
            hoveredColMuteMic = -1;
            needRepaint = true;
        }
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    if (needRepaint)
        repaint();
}

void MixerView::BleedMatrixPopup::mouseExit (const juce::MouseEvent& /*e*/)
{
    hoveredPiece = -1;
    hoveredMic = -1;
    hoveredRowMutePiece = -1;
    hoveredColMuteMic = -1;
    isEditingValue = false;
    setMouseCursor (juce::MouseCursor::NormalCursor);
    repaint();
}

void MixerView::BleedMatrixPopup::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int p = -1, m = -1;
    if (getCellAtPos (e.getPosition(), p, m))
    {
        auto& bm = processor.getSampler().getBleedMatrix();
        const float currentGain = bm.getCellGain (p, m);
        const float delta = (wheel.deltaY > 0.0f ? 0.06f : -0.06f);
        const float newGain = std::clamp (currentGain + delta, 0.0f, 2.0f);
        bm.setCellGain (p, m, newGain);
        if (bm.isCellMuted (p, m) && newGain > 0.01f)
            bm.setCellMute (p, m, false);

        hoveredPiece = p;
        hoveredMic = m;
        draggingPiece = p;
        draggingMic = m;
        isEditingValue = true;
        activeEditDb = (newGain <= 0.001f) ? -60.0f : linearToDb (newGain);
        activeEditPos = e.getPosition();
        repaint();
    }
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

    bleedMatrixBtn.setButtonText ("BLEED MATRIX");
    bleedMatrixBtn.activeColour = juce::Colour (0xffe67e22);
    bleedMatrixBtn.onClick = [this]() {
        toggleBleedMatrix();
    };
    addAndMakeVisible (bleedMatrixBtn);

    channelFXPopup   = std::make_unique<ChannelFXPopup> (*this, processor);
    masterFXPopup    = std::make_unique<MasterFXPopup> (*this, processor);
    bleedMatrixPopup = std::make_unique<BleedMatrixPopup> (*this, processor);
    addChildComponent (*channelFXPopup);
    addChildComponent (*masterFXPopup);
    addChildComponent (*bleedMatrixPopup);

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
        strip->setTheme (theme);
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

void MixerView::setBleedMatrixOpen (bool open)
{
    if (bleedMatrixOpen == open)
        return;

    bleedMatrixOpen = open;
    bleedMatrixBtn.setActive (bleedMatrixOpen);

    if (bleedMatrixPopup != nullptr)
    {
        bleedMatrixPopup->setVisible (bleedMatrixOpen);
        if (bleedMatrixOpen)
            bleedMatrixPopup->updateFromState();
    }

    resized();
    repaint();
}

void MixerView::toggleBleedMatrix()
{
    setBleedMatrixOpen (! bleedMatrixOpen);
}

void MixerView::openBleedMatrix (int highlightMic)
{
    bleedMatrixOpen = true;
    bleedMatrixBtn.setActive (true);
    if (bleedMatrixPopup != nullptr)
        bleedMatrixPopup->open (highlightMic);
    resized();
    repaint();
}

void MixerView::closePopups()
{
    if (channelFXPopup != nullptr && channelFXPopup->isOpen())
        channelFXPopup->close();

    if (masterFXPopup != nullptr && masterFXPopup->isOpen())
        masterFXPopup->close();
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

void MixerView::ChannelStripComponent::repaintMeter()
{
    const auto& sampler = processor.getSampler();
    const float peak = std::max (sampler.getMicPeakL (channelIndex_), sampler.getMicPeakR (channelIndex_));
    if (std::abs (peak - lastDisplayedPeak) > 0.002f || (peak == 0.0f && lastDisplayedPeak > 0.0f))
    {
        lastDisplayedPeak = peak;
        if (! meterBounds.isEmpty())
            repaint (meterBounds.expanded (2.0f).getSmallestIntegerContainer());
        else
            repaint();
    }
}

void MixerView::MasterStripComponent::repaintMeter()
{
    const auto& sampler = processor.getSampler();
    const float peakL = sampler.getMasterPeakL();
    const float peakR = sampler.getMasterPeakR();
    if (std::abs (peakL - lastDisplayedPeakL) > 0.002f || std::abs (peakR - lastDisplayedPeakR) > 0.002f
        || ((peakL == 0.0f && peakR == 0.0f) && (lastDisplayedPeakL > 0.0f || lastDisplayedPeakR > 0.0f)))
    {
        lastDisplayedPeakL = peakL;
        lastDisplayedPeakR = peakR;
        if (! meterLBounds.isEmpty() && ! meterRBounds.isEmpty())
            repaint (meterLBounds.getUnion (meterRBounds).expanded (2.0f).getSmallestIntegerContainer());
        else
            repaint();
    }
}

void MixerView::timerCallback()
{
    if (! isVisible())
        return;

    // Repaint only meter regions whose peak values changed
    for (auto& s : channelStrips)
        s->repaintMeter();

    if (masterStrip != nullptr)
        masterStrip->repaintMeter();
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

    savePresetBtn.setBounds  (386, btnY, 104, btnH);
    loadPresetBtn.setBounds  (494, btnY, 74,  btnH);
    bleedMatrixBtn.setBounds (574, btnY, 114, btnH);

    // Channel Area vs Master Strip
    const float contentY = 46.0f;
    const float totalAvailH = r.getHeight() - contentY - 8.0f;
    const float masterW = 100.0f;
    const float viewW = r.getWidth() - masterW - 20.0f;

    float contentH = totalAvailH;
    if (bleedMatrixOpen)
    {
        contentH = std::round (totalAvailH * 0.44f);
        contentH = std::clamp (contentH, 205.0f, 255.0f);
    }

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

    if (bleedMatrixPopup != nullptr)
    {
        if (bleedMatrixOpen)
        {
            const float matrixY = contentY + contentH + 6.0f;
            const float matrixH = r.getHeight() - matrixY - 6.0f;
            bleedMatrixPopup->setBounds (12, static_cast<int> (matrixY), static_cast<int> (r.getWidth() - 24.0f), static_cast<int> (matrixH));
            bleedMatrixPopup->setVisible (true);
        }
        else
        {
            bleedMatrixPopup->setVisible (false);
        }
    }
}

void MixerView::setTheme (const Theme& newTheme)
{
    theme = newTheme;

    presetCleanBtn.setTheme (theme);
    presetRockBtn.setTheme (theme);
    presetWarmBtn.setTheme (theme);
    presetPunchBtn.setTheme (theme);
    presetDryBtn.setTheme (theme);
    savePresetBtn.setTheme (theme);
    loadPresetBtn.setTheme (theme);
    bleedMatrixBtn.setTheme (theme);

    for (auto& strip : channelStrips)
        if (strip != nullptr)
            strip->setTheme (theme);

    if (masterStrip != nullptr)
        masterStrip->setTheme (theme);

    if (channelFXPopup != nullptr)
        channelFXPopup->setTheme (theme);

    if (masterFXPopup != nullptr)
        masterFXPopup->setTheme (theme);

    if (bleedMatrixPopup != nullptr)
        bleedMatrixPopup->setTheme (theme);

    repaint();
}

void MixerView::paint (juce::Graphics& g)
{
    // Background matching the plugin theme
    g.fillAll (theme.background);

    // Preset label
    g.setColour (theme.ink);
    g.setFont (displayFont (12.0f));
    g.drawText ("ACTIVE PRESET: " + activePresetName.toUpperCase(),
                getWidth() - 280, 14, 160, 20, juce::Justification::right, false);
}

void MixerView::mouseDown (const juce::MouseEvent& e)
{
    // Dismiss popups if clicking outside
    if (channelFXPopup && channelFXPopup->isOpen() && ! channelFXPopup->getBounds().contains (e.position.toInt()))
        channelFXPopup->close();

    if (masterFXPopup && masterFXPopup->isOpen() && ! masterFXPopup->getBounds().contains (e.position.toInt()))
        masterFXPopup->close();

    if (bleedMatrixPopup && bleedMatrixPopup->isOpen() && ! bleedMatrixPopup->getBounds().contains (e.position.toInt()))
        bleedMatrixPopup->close();
}

} // namespace glorp
