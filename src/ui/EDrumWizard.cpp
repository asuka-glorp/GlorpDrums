/*
    EDrumWizard.cpp - Interactive step-by-step setup wizard for electronic drum kits.
*/

#include "EDrumWizard.h"
#include "PluginProcessor.h"

namespace glorp
{

EDrumWizard::WizardButton::WizardButton (const juce::String& name, bool primary, bool small)
    : juce::Button (name), isPrimary (primary), isSmall (small)
{
}

void EDrumWizard::WizardButton::paintButton (juce::Graphics& g, bool isHover, bool /*isDown*/)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const auto orangeCol = juce::Colour (0xffea8c16);
    const auto inkCol = juce::Colour (0xff111215);

    if (isPrimary)
        g.setColour (isHover ? juce::Colour (0xfff39c27) : orangeCol);
    else
        g.setColour (isHover ? juce::Colour (0xfff4f5f4) : juce::Colour (0xfffcfdfc));

    g.fillRoundedRectangle (b, 3.5f);

    const auto edgeCol = isPrimary ? (isHover ? inkCol : orangeCol) : (isHover ? orangeCol : inkCol);
    drawSketchRect (g, b, edgeCol, isHover ? 1.6f : 1.2f, isSmall ? 4 : 6);

    const float fontSize = isSmall ? 13.0f : 15.0f;
    g.setFont (displayFont (fontSize));

    if (isPrimary)
        g.setColour (juce::Colour (0xffffffff));
    else
        g.setColour (isHover ? orangeCol : inkCol);

    g.drawText (getButtonText(), b, juce::Justification::centred, false);
}

//==============================================================================
EDrumWizard::EDrumWizard (PluginProcessor& p)
    : processor (p)
{
    setInterceptsMouseClicks (true, true);

    addAndMakeVisible (prevButton);
    addAndMakeVisible (skipButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (auditionBtn);
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (doneButton);

    addAndMakeVisible (rolandBtn);
    addAndMakeVisible (yamahaBtn);
    addAndMakeVisible (alesisBtn);
    addAndMakeVisible (milleniumBtn);
    addAndMakeVisible (efnoteBtn);
    addAndMakeVisible (gmBtn);

    prevButton.onClick = [this]
    {
        if (currentStepIndex > 0)
            goToStep (currentStepIndex - 1);
    };

    skipButton.onClick = [this]
    {
        if (currentStepIndex < static_cast<int> (steps.size()) - 1)
            goToStep (currentStepIndex + 1);
        else
            finishAndClose();
    };

    nextButton.onClick = [this]
    {
        if (currentStepIndex < static_cast<int> (steps.size()) - 1)
            goToStep (currentStepIndex + 1);
        else
            finishAndClose();
    };

    auditionBtn.onClick = [this]
    {
        if (currentStepIndex >= 0 && currentStepIndex < static_cast<int> (steps.size()))
        {
            const int instIdx = steps[static_cast<size_t> (currentStepIndex)].instrumentIndex;
            if (instIdx >= 0)
            {
                const int note = steps[static_cast<size_t> (currentStepIndex)].currentNote;
                const int midiNote = (note >= 0) ? note : 36;
                processor.getSampler().noteOn (instIdx, midiNote, 1.0f,
                                               processor.getSampler().currentSamplePosition());
            }
        }
    };

    cancelButton.onClick = [this] { cancelAndRevert(); };
    doneButton.onClick   = [this] { finishAndClose(); };

    rolandBtn.onClick    = [this] { applyModulePreset ("Roland V-Drums"); };
    yamahaBtn.onClick    = [this] { applyModulePreset ("Yamaha DTX"); };
    alesisBtn.onClick    = [this] { applyModulePreset ("Alesis (Strike / Nitro / Surge)"); };
    milleniumBtn.onClick = [this] { applyModulePreset ("Millenium / 2box"); };
    efnoteBtn.onClick    = [this] { applyModulePreset ("EFNote"); };
    gmBtn.onClick        = [this] { applyModulePreset ("General MIDI"); };
}

EDrumWizard::~EDrumWizard()
{
    stopTimer();
}

void EDrumWizard::open()
{
    theme = Theme::byIndex (processor.getSettings().themeIndex);

    // Backup current note assignments in case user cancels
    backupNotes = processor.getMidiMap().noteAssignments();

    buildSteps();
    currentStepIndex = 0;
    autoAdvanceCounter = 0;
    statusFeedback = "Waiting for pad strike...";

    setVisible (true);
    goToStep (0);
    startTimerHz (30);
}

void EDrumWizard::close()
{
    stopTimer();
    processor.getMidiMap().cancelLearn();
    setVisible (false);

    if (onClose != nullptr)
        onClose();
}

void EDrumWizard::cancelAndRevert()
{
    processor.getMidiMap().cancelLearn();

    // Revert to backup note map
    processor.getMidiMap().clearAssignments();
    for (const auto& pair : backupNotes)
        processor.getMidiMap().assignNote (pair.first, pair.second);

    close();
}

void EDrumWizard::finishAndClose()
{
    processor.getMidiMap().cancelLearn();
    close();
}

void EDrumWizard::applyModulePreset (const juce::String& presetName)
{
    processor.applyMapPreset (presetName);

    // Refresh current notes for all steps
    for (auto& s : steps)
    {
        if (s.instrumentIndex >= 0)
            s.currentNote = processor.getMidiMap().noteForInstrument (s.instrumentIndex);
    }

    statusFeedback = "Applied " + presetName + " map!";
    justLearned = true;
    repaint();
}

void EDrumWizard::buildSteps()
{
    steps.clear();
    const auto& sampler = processor.getSampler();
    const int instCount = sampler.numInstruments();

    if (instCount == 0)
        return;

    std::vector<bool> used (static_cast<size_t> (instCount), false);

    auto addStepForMatch = [&] (const juce::String& prompt, const juce::String& hint, const std::vector<const char*>& keywords)
    {
        for (int i = 0; i < instCount; ++i)
        {
            if (used[static_cast<size_t> (i)])
                continue;

            const auto name = sampler.instrumentName (i).toLowerCase();
            bool matched = false;
            for (const auto* kw : keywords)
            {
                if (name.contains (juce::String (kw).toLowerCase()))
                {
                    matched = true;
                    break;
                }
            }

            if (matched)
            {
                Step s;
                s.instrumentIndex = i;
                s.displayName = sampler.instrumentName (i);
                s.prompt = prompt;
                s.hint = hint;
                s.currentNote = processor.getMidiMap().noteForInstrument (i);
                steps.push_back (s);
                used[static_cast<size_t> (i)] = true;
                break;
            }
        }
    };

    // Logical drum kit setup order
    addStepForMatch ("KICK DRUM", "Strike your bass drum pedal.", { "kick", "kdrum" });
    addStepForMatch ("SNARE HEAD", "Strike the center of your snare mesh or pad.", { "snarehead", "snare_head", "snare" });
    addStepForMatch ("SNARE RIMSHOT", "Strike both the snare head and rim simultaneously.", { "rimshot", "snarerimshot", "rim_shot" });
    addStepForMatch ("SNARE CROSS-STICK", "Lay stick across rim for cross-stick (crosstick/side stick).", { "rimclick", "crossstick", "sidestick", "snarerimclick" });
    addStepForMatch ("HI-HAT (CLOSED)", "Hold hi-hat pedal down and strike hi-hat pad.", { "hihatclosed", "hhclosed", "hihat_closed" });
    addStepForMatch ("HI-HAT (SEMI-OPEN)", "Loosen pedal slightly and strike hi-hat pad.", { "hihatsemiopen", "hhsemiopen", "hihat_semiopen", "semiopen" });
    addStepForMatch ("HI-HAT (OPEN)", "Release hi-hat pedal and strike hi-hat pad.", { "hihatopen", "hhopen", "hihat_open" });
    addStepForMatch ("HI-HAT (PEDAL CHICK)", "Step down firmly on hi-hat pedal alone without striking pad.", { "hihatpedal", "hhpedal", "hihat_pedal" });
    addStepForMatch ("TOM 1 (HEAD)", "Strike rack tom 1 pad center.", { "tom1head", "tom1_head", "tom1", "hightom" });
    addStepForMatch ("TOM 1 (RIM)", "Strike rack tom 1 rim.", { "tom1rim", "tom1_rim" });
    addStepForMatch ("TOM 2 (HEAD)", "Strike rack tom 2 pad center.", { "tom2head", "tom2_head", "tom2", "midtom" });
    addStepForMatch ("TOM 2 (RIM)", "Strike rack tom 2 rim.", { "tom2rim", "tom2_rim" });
    addStepForMatch ("FLOOR TOM (HEAD)", "Strike floor tom pad center.", { "ftomhead", "floortomhead", "ftom", "floortom", "lowtom" });
    addStepForMatch ("FLOOR TOM (RIM)", "Strike floor tom rim.", { "ftomrim", "floortomrim" });
    addStepForMatch ("CRASH 1 (BOW / EDGE)", "Strike main crash cymbal bow or edge.", { "crash1", "crash_1", "crash" });
    addStepForMatch ("CRASH 2 (BOW / EDGE)", "Strike secondary crash cymbal.", { "crash2", "crash_2" });
    addStepForMatch ("RIDE (BOW)", "Strike ride cymbal body / bow.", { "ridebow", "ride_bow", "ride" });
    addStepForMatch ("RIDE (BELL)", "Strike ride cymbal bell with shoulder of stick.", { "ridebell", "ride_bell", "bell" });
    addStepForMatch ("RIDE (EDGE)", "Crash the edge of your ride cymbal.", { "rideedge", "ride_edge" });

    // Include any remaining kit instruments not captured above
    for (int i = 0; i < instCount; ++i)
    {
        if (! used[static_cast<size_t> (i)])
        {
            Step s;
            s.instrumentIndex = i;
            s.displayName = sampler.instrumentName (i);
            s.prompt = s.displayName.toUpperCase();
            s.hint = "Strike your pad to map " + s.displayName + ".";
            s.currentNote = processor.getMidiMap().noteForInstrument (i);
            steps.push_back (s);
            used[static_cast<size_t> (i)] = true;
        }
    }
}

void EDrumWizard::goToStep (int stepIndex)
{
    if (steps.empty())
        return;

    currentStepIndex = juce::jlimit (0, static_cast<int> (steps.size()) - 1, stepIndex);
    autoAdvanceCounter = 0;
    justLearned = false;

    const int instIdx = steps[static_cast<size_t> (currentStepIndex)].instrumentIndex;
    if (instIdx >= 0)
    {
        steps[static_cast<size_t> (currentStepIndex)].currentNote = processor.getMidiMap().noteForInstrument (instIdx);
        processor.getMidiMap().beginLearn ("note" + std::to_string (instIdx));
        isWaitingForHit = true;
        statusFeedback = "Waiting for pad strike...";
    }

    repaint();
}

void EDrumWizard::timerCallback()
{
    if (! isVisible() || steps.empty())
        return;

    if (isWaitingForHit)
    {
        // When audio thread receives and assigns a note, MidiMap isLearning() becomes false
        if (! processor.getMidiMap().isLearning())
        {
            isWaitingForHit = false;
            justLearned = true;

            const int instIdx = steps[static_cast<size_t> (currentStepIndex)].instrumentIndex;
            const int note = processor.getMidiMap().noteForInstrument (instIdx);
            steps[static_cast<size_t> (currentStepIndex)].currentNote = note;
            steps[static_cast<size_t> (currentStepIndex)].completed = true;

            const auto noteName = (note >= 0)
                ? (juce::MidiMessage::getMidiNoteName (note, true, true, 3) + " (" + juce::String (note) + ")")
                : "None";

            statusFeedback = "CAPTURED: " + noteName + " - ASSIGNED!";
            autoAdvanceCounter = 20; // Auto-advance after ~650ms
            repaint();
        }
    }
    else if (autoAdvanceCounter > 0)
    {
        --autoAdvanceCounter;
        if (autoAdvanceCounter == 0)
        {
            if (currentStepIndex < static_cast<int> (steps.size()) - 1)
                goToStep (currentStepIndex + 1);
            else
                statusFeedback = "All pieces configured! Click DONE to finish.";
        }
    }
}

void EDrumWizard::mouseDown (const juce::MouseEvent& e)
{
    // Absorb clicks so background editor is not clicked through scrim
    e.eventComponent->repaint();
}

void EDrumWizard::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    // Dark semi-transparent scrim
    g.fillAll (juce::Colour (0xcc111214));

    // Centered modal card
    const float cardW = std::min (620.0f, area.getWidth() - 32.0f);
    const float cardH = std::min (480.0f, area.getHeight() - 40.0f);
    const auto card = juce::Rectangle<float> ((area.getWidth() - cardW) * 0.5f,
                                             (area.getHeight() - cardH) * 0.5f,
                                             cardW, cardH);

    // Card background
    g.setColour (theme.panel);
    g.fillRoundedRectangle (card, 8.0f);
    drawSketchRect (g, card, theme.panelEdge, 2.0f, 4);

    // Header Title
    g.setFont (displayFont (22.0f));
    g.setColour (theme.ink);
    g.drawText ("E-DRUM SETUP WIZARD", juce::Rectangle<float> (card.getX(), card.getY() + 14.0f, card.getWidth(), 26.0f),
                juce::Justification::centred, false);

    g.setFont (uiFont (14.0f));
    g.setColour (theme.inkSoft);
    g.drawText ("Strike each pad on your electronic kit to map it automatically, or quick-load a preset:",
                juce::Rectangle<float> (card.getX(), card.getY() + 38.0f, card.getWidth(), 18.0f),
                juce::Justification::centred, false);

    if (steps.empty())
    {
        g.setFont (displayFont (18.0f));
        g.setColour (theme.ink);
        g.drawText ("No instruments found in current kit. Load a drum kit first!",
                    card.reduced (40.0f), juce::Justification::centred, true);
        return;
    }

    const auto& s = steps[static_cast<size_t> (currentStepIndex)];

    // Progress Bar
    const float progressY = card.getY() + 92.0f;
    const float barX = card.getX() + 24.0f;
    const float barW = card.getWidth() - 48.0f;
    const float barH = 8.0f;

    g.setColour (juce::Colour (0x33ea8c16));
    g.fillRoundedRectangle (juce::Rectangle<float> (barX, progressY, barW, barH), 4.0f);

    const float fillW = barW * (static_cast<float> (currentStepIndex + 1) / static_cast<float> (steps.size()));
    g.setColour (juce::Colour (0xffea8c16));
    g.fillRoundedRectangle (juce::Rectangle<float> (barX, progressY, fillW, barH), 4.0f);

    // Step Counter
    g.setFont (displayFont (14.0f));
    g.setColour (theme.inkSoft);
    const juce::String stepLabel = "Step " + juce::String (currentStepIndex + 1) + " of "
                                 + juce::String (steps.size()) + " (" + s.displayName + ")";
    g.drawText (stepLabel, juce::Rectangle<float> (barX, progressY - 18.0f, barW, 16.0f),
                juce::Justification::centredLeft, false);

    // Main Prompt Box
    const auto promptBox = juce::Rectangle<float> (barX, progressY + 18.0f, barW, 170.0f);
    g.setColour (theme.isDark ? juce::Colour (0xff1f1f23) : juce::Colour (0xfff3f5f3));
    g.fillRoundedRectangle (promptBox, 6.0f);
    drawSketchRect (g, promptBox, justLearned ? juce::Colour (0xffea8c16) : theme.panelEdge, justLearned ? 2.0f : 1.2f, 5);

    // Action Title
    g.setFont (displayFont (24.0f));
    g.setColour (justLearned ? juce::Colour (0xffea8c16) : theme.ink);
    g.drawText ("STRIKE: " + s.prompt,
                juce::Rectangle<float> (promptBox.getX(), promptBox.getY() + 16.0f, promptBox.getWidth(), 30.0f),
                juce::Justification::centred, false);

    // Hint / Guidance
    g.setFont (uiFont (15.0f));
    g.setColour (theme.inkSoft);
    g.drawText (s.hint,
                juce::Rectangle<float> (promptBox.getX() + 10.0f, promptBox.getY() + 48.0f, promptBox.getWidth() - 20.0f, 22.0f),
                juce::Justification::centred, false);

    // Note status badge
    const juce::String noteStr = (s.currentNote >= 0)
        ? (juce::MidiMessage::getMidiNoteName (s.currentNote, true, true, 3) + " (MIDI #" + juce::String (s.currentNote) + ")")
        : "Unassigned";

    g.setFont (displayFont (17.0f));
    g.setColour (theme.ink);
    g.drawText ("Current Mapping: " + noteStr,
                juce::Rectangle<float> (promptBox.getX(), promptBox.getY() + 78.0f, promptBox.getWidth(), 22.0f),
                juce::Justification::centred, false);

    // Feedback status (waiting vs captured)
    g.setFont (displayFont (18.0f));
    g.setColour (justLearned ? juce::Colour (0xff2e9b3a) : juce::Colour (0xffea8c16));
    g.drawText (statusFeedback,
                juce::Rectangle<float> (promptBox.getX(), promptBox.getY() + 118.0f, promptBox.getWidth(), 26.0f),
                juce::Justification::centred, false);
}

void EDrumWizard::resized()
{
    const auto area = getLocalBounds();
    const int cardW = std::min (620, area.getWidth() - 32);
    const int cardH = std::min (480, area.getHeight() - 40);
    const int cardX = (area.getWidth() - cardW) / 2;
    const int cardY = (area.getHeight() - cardH) / 2;

    // Quick preset buttons row
    const int presetY = cardY + 58;
    const int numPresets = 6;
    const int pPad = 4;
    const int pW = (cardW - 48 - (numPresets - 1) * pPad) / numPresets;
    const int pH = 22;

    rolandBtn.setBounds    (cardX + 24 + 0 * (pW + pPad), presetY, pW, pH);
    yamahaBtn.setBounds    (cardX + 24 + 1 * (pW + pPad), presetY, pW, pH);
    alesisBtn.setBounds    (cardX + 24 + 2 * (pW + pPad), presetY, pW, pH);
    milleniumBtn.setBounds (cardX + 24 + 3 * (pW + pPad), presetY, pW, pH);
    efnoteBtn.setBounds    (cardX + 24 + 4 * (pW + pPad), presetY, pW, pH);
    gmBtn.setBounds        (cardX + 24 + 5 * (pW + pPad), presetY, pW, pH);

    // Audition button centered below prompt card
    const int audW = 150;
    const int audH = 26;
    auditionBtn.setBounds (cardX + (cardW - audW) / 2, cardY + 312, audW, audH);

    // Bottom Navigation Bar
    const int btnY = cardY + cardH - 46;
    const int btnH = 30;
    const int navBtnW = 92;

    cancelButton.setBounds (cardX + 24, btnY, 80, btnH);
    prevButton.setBounds   (cardX + 118, btnY, navBtnW, btnH);
    skipButton.setBounds   (cardX + 218, btnY, 76, btnH);
    nextButton.setBounds   (cardX + 302, btnY, navBtnW, btnH);
    doneButton.setBounds   (cardX + cardW - 24 - 96, btnY, 96, btnH);
}

} // namespace glorp
