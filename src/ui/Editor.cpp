#include "Editor.h"

namespace glorp
{

//==============================================================================
void Editor::MenuButton::paintButton (juce::Graphics& g, bool isHover, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);

    if (isHover || isDown)
    {
        g.setColour (juce::Colour (0xff101012).withAlpha (isDown ? 0.16f : 0.08f));
        g.fillRoundedRectangle (b, 4.0f);
        drawSketchRect (g, b, juce::Colour (0xff101012), 1.3f, 7);
    }

    g.setFont (uiFont (17.0f));
    g.setColour (juce::Colour (0xff101012));
    g.drawText (getName(), b, juce::Justification::centred, false);
}

//==============================================================================
Editor::Editor (PluginProcessor& p)
    : processor (p)
{
    theme = Theme::byIndex (processor.getSettings().themeIndex);

    addAndMakeVisible (kitArea);
    kitArea.addAndMakeVisible (kitView);
    addAndMakeVisible (scope);

    buildHeaderMenu();
    buildControls();

    // Sound and animation when an instrument in the kit view is clicked
    kitView.onPieceHit = [this] (KitPiece piece, float velocity)
    {
        const auto& sampler = processor.getSampler();
        const auto count = sampler.numInstruments();

        int targetIndex = -1;
        for (int i = 0; i < count; ++i)
        {
            if (DrumKitView::pieceForName (sampler.instrumentName (i)) == piece)
            {
                targetIndex = i;
                break;
            }
        }

        if (targetIndex >= 0)
        {
            const auto note = processor.getMidiMap().noteForInstrument (targetIndex);
            const int midiNote = (note >= 0) ? note : 36;

            processor.getSampler().noteOn (targetIndex, midiNote, velocity,
                                           processor.getSampler().currentSamplePosition());

            scope.noteReceived (midiNote, static_cast<int> (velocity * 127.0f));
            selectInstrument (targetIndex);
        }
    };

    processor.addListener (this);
    logo = logoDrawable();

    kitView.setMouseClickGrabsKeyboardFocus (false);
    scope.setInterceptsMouseClicks (false, false);

    setSize (processor.getSettings().editorWidth, processor.getSettings().editorHeight);

    kitChanged();
    startTimerHz (30);
}

Editor::~Editor()
{
    stopTimer();
    processor.removeListener (this);
}

//==============================================================================
void Editor::buildHeaderMenu()
{
    addAndMakeVisible (fileButton);
    addAndMakeVisible (presetsButton);
    addAndMakeVisible (midiButton);
    addAndMakeVisible (settingsButton);

    fileButton.onClick = [this]
    {
        auto menu = buildFileMenu();
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (&fileButton)
                                .withParentComponent (this));
    };

    presetsButton.onClick = [this]
    {
        auto menu = buildPresetsMenu();
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (&presetsButton)
                                .withParentComponent (this));
    };

    midiButton.onClick = [this]
    {
        auto menu = buildMidiMenu();
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (&midiButton)
                                .withParentComponent (this));
    };

    settingsButton.onClick = [this]
    {
        auto menu = buildSettingsMenu();
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (&settingsButton)
                                .withParentComponent (this));
    };
}

juce::PopupMenu Editor::buildFileMenu()
{
    juce::PopupMenu menu;

    // Discovered drumkits list
    const auto& kitList = processor.getKitList();
    if (! kitList.empty())
    {
        juce::PopupMenu kitsSubmenu;
        int id = 1;

        for (const auto& entry : kitList)
        {
            if (! entry.valid)
                continue;

            const auto isLoaded = (entry.drumkitPath == processor.getCurrentKitPath().toStdString());
            const auto path = juce::String (entry.drumkitPath);

            kitsSubmenu.addItem (entry.displayName, true, isLoaded, [this, path]
            {
                processor.loadKit (path);
            });
            ++id;
        }

        menu.addSubMenu ("Drum Kits", kitsSubmenu);
        menu.addSeparator();
    }

    // Recent kits
    const auto& recentKits = processor.getSettings().recentKits;
    if (! recentKits.isEmpty())
    {
        juce::PopupMenu recentMenu;
        for (const auto& recent : recentKits)
        {
            juce::String label = juce::File (recent).getFileNameWithoutExtension();
            if (recent == processor.getCurrentKitPath())
                label += "  (loaded)";

            recentMenu.addItem (label, [this, recent] { processor.loadKit (recent); });
        }
        menu.addSubMenu ("Recent Kits", recentMenu);
        menu.addSeparator();
    }

    menu.addItem ("Refresh Kit List", [this]
    {
        processor.refreshKitList();
        statusText = juce::String (processor.getKitList().size()) + " kits found";
        repaint();
    });

    menu.addItem ("Choose Kit Folder...", [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Select DrumKit Folder",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory),
            "*");

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser] (const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if (chosen.isDirectory())
                {
                    processor.getSettings().addKitPath (chosen.getFullPathName());
                    processor.getSettings().saveIfChanged();
                    processor.refreshKitList();
                    rebuildCallouts();
                }
            });
    });

    menu.addSeparator();
    menu.addItem ("Panic (All Notes Off)", [this] { processor.getSampler().allNotesOff(); });

    return menu;
}

juce::PopupMenu Editor::buildPresetsMenu()
{
    juce::PopupMenu menu;

    for (const auto& preset : MidiMap::availablePresets())
        menu.addItem (preset.name, [this, preset] { processor.applyMapPreset (preset.name); });

    menu.addSeparator();

    menu.addItem ("Save Current Map As...", [this]
    {
        auto& map = processor.getMidiMap();
        const auto dir = MidiMap::userPresetDirectory();

        std::vector<juce::String> names;
        for (int i = 0; i < processor.getSampler().numInstruments(); ++i)
            names.push_back (processor.getSampler().instrumentName (i));

        map.savePresetFile (dir.getChildFile ("MyMap.xml"), "My Map", names);
        statusText = "Saved to " + dir.getFullPathName();
        rebuildCallouts();
    });

    menu.addItem ("Rescan Presets", [this]
    {
        rebuildCallouts();
        statusText = "Presets rescanned";
    });

    return menu;
}

juce::PopupMenu Editor::buildMidiMenu()
{
    juce::PopupMenu menu;
    auto& map = processor.getMidiMap();

    menu.addItem ("Panic (All Notes Off)", [this] { processor.getSampler().allNotesOff(); });
    menu.addSeparator();

    if (map.isLearning())
        menu.addItem ("Cancel MIDI Learn", [this]
        {
            processor.getMidiMap().cancelLearn();
            learningInstrument = -1;
            updateCalloutLabels();
        });

    menu.addItem ("Clear Note Map", [this]
    {
        processor.getMidiMap().clearAssignments();
        updateCalloutLabels();
    });

    menu.addItem ("Clear All MIDI Bindings", [this]
    {
        processor.getMidiMap().clearBindings();
        statusText = "MIDI bindings cleared";
    });

    return menu;
}

juce::PopupMenu Editor::buildSettingsMenu()
{
    juce::PopupMenu menu;

    menu.addItem (theme.isDark ? "Theme: Sketch" : "Theme: Dark", [this]
    {
        auto& settings = processor.getSettings();
        settings.themeIndex = settings.themeIndex == 0 ? 1 : 0;
        settings.saveIfChanged();
        theme = Theme::byIndex (settings.themeIndex);
        rebuildCallouts();
        repaint();
    });

    menu.addSeparator();
    menu.addItem ("Reset All Instrument Controls", [this]
    {
        processor.getSampler().resetInstrumentParams();
        updateCalloutLabels();
        repaint();
    });

    menu.addSeparator();
    menu.addItem ("About GlorpDrums", [this]
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "GlorpDrums",
            "GlorpDrums VSTi / LV2 Drum Plugin\n\nPlays DrumGizmo kits with natural WSOLA tuning,\n"
            "interactive MIDI learn, and individual output buses.");
    });

    return menu;
}

//==============================================================================
void Editor::buildControls()
{
    // Bottom-Left 4 Knobs (2x2 Grid)
    masterVolumeKnob = std::make_unique<Knob> ("VOLUME");
    if (auto* p = processor.findParameter ("masterLevel"))
    {
        masterVolumeKnob->setDoubleClickReturnValue (true, 0.8);
        masterVolumeKnob->attachToParameter (p);
    }
    addAndMakeVisible (*masterVolumeKnob);

    instPanKnob = std::make_unique<Knob> ("PAN");
    instPanKnob->setDoubleClickReturnValue (true, 0.5);
    addAndMakeVisible (*instPanKnob);

    instPitchKnob = std::make_unique<Knob> ("PITCH");
    instPitchKnob->setDoubleClickReturnValue (true, 0.5);
    addAndMakeVisible (*instPitchKnob);

    instLevelKnob = std::make_unique<Knob> ("LEVEL");
    instLevelKnob->setDoubleClickReturnValue (true, 0.5);
    addAndMakeVisible (*instLevelKnob);

    // Bottom-Right Controls
    attackKnob = std::make_unique<Knob> ("ATTACK");
    if (auto* p = processor.findParameter ("attack"))
    {
        attackKnob->setDoubleClickReturnValue (true, 0.0);
        attackKnob->attachToParameter (p);
    }
    addAndMakeVisible (*attackKnob);

    decayKnob = std::make_unique<Knob> ("DECAY");
    if (auto* p = processor.findParameter ("decay"))
    {
        decayKnob->setDoubleClickReturnValue (true, 0.5);
        decayKnob->attachToParameter (p);
    }
    addAndMakeVisible (*decayKnob);

    // Sliders / Faders in bottom right
    fader1 = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::NoTextBox);
    fader1->setRange (0.0, 1.0, 0.01);
    fader1->setValue (0.5);
    addAndMakeVisible (*fader1);

    fader2 = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::NoTextBox);
    fader2->setRange (0.0, 1.0, 0.01);
    fader2->setValue (0.7);
    addAndMakeVisible (*fader2);

    bindInstrumentKnobs();
}

void Editor::bindInstrumentKnobs()
{
    const auto prefix = "inst" + juce::String (selectedInstrumentIndex) + ".";

    if (auto* p = processor.findParameter (prefix + "pan"))
        instPanKnob->attachToParameter (p);

    if (auto* p = processor.findParameter (prefix + "pitch"))
        instPitchKnob->attachToParameter (p);

    if (auto* p = processor.findParameter (prefix + "level"))
        instLevelKnob->attachToParameter (p);
}

void Editor::selectInstrument (int index)
{
    const auto count = processor.getSampler().numInstruments();
    if (count > 0)
        selectedInstrumentIndex = juce::jlimit (0, count - 1, index);
    else
        selectedInstrumentIndex = 0;

    bindInstrumentKnobs();

    for (size_t i = 0; i < callouts.size(); ++i)
        callouts[i]->setSelected (static_cast<int> (i) == selectedInstrumentIndex);

    repaint();
}

//==============================================================================
void Editor::rebuildCallouts()
{
    for (auto& box : callouts)
    {
        kitArea.removeChildComponent (box.get());
        box.reset();
    }
    callouts.clear();

    const auto count = processor.getSampler().numInstruments();
    const auto numBoxes = (count > 0) ? count : 7; // Show 7 callouts matching mockup
    callouts.reserve (static_cast<size_t> (numBoxes));

    for (int i = 0; i < numBoxes; ++i)
    {
        auto box = std::make_unique<CalloutBox>();

        if (i < count)
            box->setInstrumentName (processor.getSampler().instrumentName (i));
        else
        {
            // Default placeholder names matching the mockup layout
            const char* defaultNames[] = { "Fat Snare", "Heavy Kick", "High Tom", "Crash", "Tight Hi-Hat", "Floor Tom", "Kick" };
            box->setInstrumentName (defaultNames[i % 7]);
        }

        kitArea.addAndMakeVisible (*box);

        const int instIdx = i;
        box->onBoxClicked = [this, instIdx]
        {
            selectInstrument (instIdx);
            // Play a test hit when clicked
            if (instIdx < processor.getSampler().numInstruments())
            {
                const auto note = processor.getMidiMap().noteForInstrument (instIdx);
                const int midiNote = (note >= 0) ? note : 36;
                processor.getSampler().noteOn (instIdx, midiNote, 0.85f, processor.getSampler().currentSamplePosition());
                kitView.triggerHit (processor.getSampler().instrumentName (instIdx), 0.85f);
                scope.noteReceived (midiNote, 108);
            }
        };

        box->onSelectClicked = [this, instIdx]
        {
            showSampleMenu (instIdx, juce::Point<int>());
        };

        box->onAutoClicked = [this, instIdx] { toggleAuto (instIdx); };
        box->onLearnClicked = [this, instIdx] { armLearn (instIdx); };
        box->onMidiLabelClicked = [this, instIdx] { armLearn (instIdx); };

        callouts.push_back (std::move (box));
    }

    updateCalloutLabels();
    selectInstrument (selectedInstrumentIndex);
    resized();
}

void Editor::armLearn (int instrument)
{
    auto& map = processor.getMidiMap();
    learningInstrument = instrument;
    map.beginLearn ("note" + std::to_string (instrument));

    statusText = "MIDI learn armed - hit your drum pad";
    updateCalloutLabels();
}

void Editor::toggleAuto (int instrument)
{
    if (instrument >= processor.getSampler().numInstruments())
        return;

    auto params = processor.getSampler().getInstrumentParams (instrument);
    params.autoRoundRobin = ! params.autoRoundRobin;
    if (! params.autoRoundRobin)
        params.selectedSample = 0;

    processor.getSampler().setInstrumentParams (instrument, params);
    updateCalloutLabels();
}

void Editor::showSampleMenu (int instrument, const juce::Point<int>&)
{
    if (instrument >= processor.getSampler().numInstruments())
        return;

    juce::PopupMenu menu;
    const auto currentSample = processor.getSampler().getInstrumentParams (instrument).selectedSample;
    const auto count = processor.getSampler().numSamplesForInstrument (instrument);

    for (int s = 0; s < count; ++s)
    {
        const auto name = processor.getSampler().sampleName (instrument, s);
        menu.addItem (name, true, s == currentSample, [this, instrument, s]
        {
            auto params = processor.getSampler().getInstrumentParams (instrument);
            params.autoRoundRobin = false;
            params.selectedSample = s;
            processor.getSampler().setInstrumentParams (instrument, params);
            updateCalloutLabels();
        });
    }

    if (instrument < static_cast<int> (callouts.size()))
        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (callouts[static_cast<size_t> (instrument)].get())
                                .withParentComponent (this));
}

void Editor::updateCalloutLabels()
{
    for (size_t i = 0; i < callouts.size(); ++i)
    {
        auto& box = callouts[i];
        const int idx = static_cast<int> (i);

        box->setNoteLabel (noteLabelFor (idx));
        box->setSampleName (sampleLabelFor (idx));

        if (idx < processor.getSampler().numInstruments())
            box->setAutoEnabled (processor.getSampler().getInstrumentParams (idx).autoRoundRobin);

        box->setLearnArmed (learningInstrument == idx);
        box->setSelected (idx == selectedInstrumentIndex);
    }

    repaint();
}

juce::String Editor::noteLabelFor (int instrument) const
{
    const auto note = processor.getMidiMap().noteForInstrument (instrument);
    if (note < 0)
        return "--";

    return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
}

juce::String Editor::sampleLabelFor (int instrument) const
{
    if (instrument >= processor.getSampler().numInstruments())
        return {};

    const auto& s = processor.getSampler();
    const auto params = s.getInstrumentParams (instrument);

    if (params.autoRoundRobin)
        return "AUTO";

    return s.sampleName (instrument, params.selectedSample);
}

void Editor::kitChanged()
{
    kitErrorText = processor.getLastKitError();

    std::vector<juce::String> names;
    for (int i = 0; i < processor.getSampler().numInstruments(); ++i)
        names.push_back (processor.getSampler().instrumentName (i));

    kitView.setInstrumentNames (names);
    rebuildCallouts();

    statusText = processor.getSampler().isKitLoaded()
        ? juce::String (processor.getSampler().numInstruments()) + " instruments, "
            + juce::String (processor.getSampler().usedBusCount()) + " output buses"
        : "Glorp Demo Kit ready";

    repaint();
}

void Editor::parametersChanged()
{
    updateCalloutLabels();
    bindInstrumentKnobs();
    repaint();
}

//==============================================================================
void Editor::timerCallback()
{
    auto& sampler = processor.getSampler();
    HitEvent event;
    bool anyHit = false;

    while (sampler.popHit (event))
    {
        kitView.triggerHit (processor.getSampler().instrumentName (event.instrumentIndex),
                            event.velocity);

        if (event.note >= 0)
            scope.noteReceived (event.note, static_cast<int> (event.velocity * 127.0f));

        anyHit = true;
    }

    if (anyHit)
        repaint();

    processor.drainControlQueue();

    static bool wasLearning = false;
    const auto isLearning = processor.getMidiMap().isLearning();

    if (wasLearning && ! isLearning)
    {
        learningInstrument = -1;
        statusText = "MIDI learned";
        updateCalloutLabels();
    }

    wasLearning = isLearning;
}

//==============================================================================
void Editor::resized()
{
    const auto b = getLocalBounds();
    const int w = b.getWidth();
    const int h = b.getHeight();

    // 1. Header menu buttons in top right
    const int menuY = 10;
    const int menuH = 28;
    const int menuBtnW = 84;
    int curX = w - 40 - menuBtnW;

    settingsButton.setBounds (curX, menuY, menuBtnW, menuH);
    curX -= (menuBtnW + 6);
    midiButton.setBounds (curX, menuY, menuBtnW, menuH);
    curX -= (menuBtnW + 18);
    presetsButton.setBounds (curX, menuY, menuBtnW + 12, menuH);
    curX -= (menuBtnW + 6);
    fileButton.setBounds (curX, menuY, menuBtnW, menuH);

    // 2. Central drum kit & callouts area
    const int kitAreaY = 56;
    const int bottomH = 148;
    const int kitAreaH = h - kitAreaY - bottomH;

    kitArea.setBounds (12, kitAreaY, w - 24, kitAreaH);
    kitView.setBounds (kitArea.getLocalBounds());

    // Position the 7 Callout Boxes around the kit (matching Image 2 mockup)
    const float kw = static_cast<float> (kitArea.getWidth());
    const float kh = static_cast<float> (kitArea.getHeight());
    const float boxW = juce::jlimit (120.0f, 150.0f, kw * 0.16f);
    const float boxH = juce::jlimit (70.0f, 85.0f, kh * 0.20f);

    struct BoxPos { float xRatio; float yRatio; KitPiece piece; };
    const BoxPos positions[] = {
        { 0.04f, 0.22f, KitPiece::snare },      // 0: Snare (Top Left)
        { 0.06f, 0.50f, KitPiece::kick },       // 1: Kick Alt (Mid Left)
        { 0.44f, 0.05f, KitPiece::tom1 },       // 2: High Tom (Top Mid)
        { 0.78f, 0.08f, KitPiece::crash },      // 3: Crash/Ride (Top Right)
        { 0.80f, 0.35f, KitPiece::hihat },      // 4: Hi-Hat (Mid Right)
        { 0.78f, 0.62f, KitPiece::floorTom },   // 5: Floor Tom (Lower Right)
        { 0.44f, 0.76f, KitPiece::kick }        // 6: Main Kick (Bottom Mid)
    };

    for (size_t i = 0; i < callouts.size(); ++i)
    {
        const auto& pos = positions[i % 7];
        const auto bx = kw * pos.xRatio;
        const auto by = kh * pos.yRatio;

        callouts[i]->setBounds (static_cast<int> (bx), static_cast<int> (by),
                                static_cast<int> (boxW), static_cast<int> (boxH));

        // Connect anchor to the target drum piece
        callouts[i]->setAnchor (kitView.anchorFor (pos.piece));
    }

    // 3. Bottom controls
    const int bottomY = h - bottomH + 8;

    // Bottom-Left 4 Knobs (2x2 grid)
    const int kSize = 64;
    const int kLeft = 28;
    const int kTop = bottomY + 18;

    masterVolumeKnob->setBounds (kLeft, kTop, kSize, kSize);
    instPanKnob->setBounds (kLeft + kSize + 14, kTop, kSize, kSize);
    instPitchKnob->setBounds (kLeft, kTop + kSize + 2, kSize, kSize);
    instLevelKnob->setBounds (kLeft + kSize + 14, kTop + kSize + 2, kSize, kSize);

    // Bottom-Center MIDI Activity Panel
    const int scopeX = kLeft + (kSize * 2) + 38;
    const int scopeW = w - scopeX - 250;
    scope.setBounds (scopeX, bottomY + 8, scopeW, bottomH - 24);

    // Bottom-Right Controls
    const int rightX = scopeX + scopeW + 18;
    fader1->setBounds (rightX, bottomY + 10, 22, 60);
    fader2->setBounds (rightX + 28, bottomY + 10, 22, 60);

    attackKnob->setBounds (rightX + 70, bottomY + 68, kSize, kSize);
    decayKnob->setBounds (rightX + 70 + kSize + 10, bottomY + 68, kSize, kSize);
}

//==============================================================================
void Editor::drawLeaderArrows (juce::Graphics& g)
{
    g.setColour (theme.ink.withAlpha (0.75f));

    for (const auto& box : callouts)
    {
        const auto start = box->getLeaderStartPoint (&kitArea);
        const auto target = box->getAnchor();

        if (target.x <= 0.0f || target.y <= 0.0f)
            continue;

        const auto delta = target - start;
        const auto dist = delta.getDistanceFromOrigin();
        if (dist < 10.0f)
            continue;

        const auto dir = delta / dist;

        // Dashed line
        constexpr float dash = 5.0f;
        constexpr float gap = 4.0f;

        for (float t = 0.0f; t < dist - 12.0f; t += dash + gap)
        {
            const auto p1 = start + dir * t;
            const auto p2 = start + dir * juce::jmin (dist - 12.0f, t + dash);
            g.drawLine (juce::Line<float> (p1, p2), 1.5f);
        }

        // Arrowhead pointing at target
        const auto tip = target - dir * 2.0f;
        const auto normal = juce::Point<float> (-dir.y, dir.x);
        const auto arrowBase = tip - dir * 9.0f;

        juce::Path arrow;
        arrow.startNewSubPath (tip);
        arrow.lineTo (arrowBase + normal * 4.5f);
        arrow.lineTo (arrowBase - normal * 4.5f);
        arrow.closeSubPath();

        g.fillPath (arrow);
    }
}

//==============================================================================
void Editor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // 1. Paper background
    g.fillAll (theme.background);

    // 2. Hand-drawn outer window border
    drawSketchRect (g, b.reduced (4.0f), theme.panelEdge, 2.2f, 1);
    drawSketchRect (g, b.reduced (6.5f), theme.panelEdge.withAlpha (0.4f), 1.0f, 3);

    // Window controls in top right (_ □ ✕)
    const float winBtnY = 12.0f;
    const float winBtnX = b.getRight() - 65.0f;
    g.setColour (theme.ink);
    drawSketchRect (g, juce::Rectangle<float> (winBtnX, winBtnY, 14.0f, 12.0f), theme.ink, 1.2f, 4);
    g.drawLine (winBtnX + 2.0f, winBtnY + 8.0f, winBtnX + 12.0f, winBtnY + 8.0f, 1.4f); // _

    drawSketchRect (g, juce::Rectangle<float> (winBtnX + 18.0f, winBtnY, 14.0f, 12.0f), theme.ink, 1.2f, 5); // □
    drawSketchRect (g, juce::Rectangle<float> (winBtnX + 36.0f, winBtnY, 14.0f, 12.0f), theme.ink, 1.2f, 6);
    g.drawLine (winBtnX + 39.0f, winBtnY + 2.0f, winBtnX + 47.0f, winBtnY + 10.0f, 1.4f); // ✕
    g.drawLine (winBtnX + 47.0f, winBtnY + 2.0f, winBtnX + 39.0f, winBtnY + 10.0f, 1.4f);

    // Mini Glorp cat in top left window frame corner
    g.fillEllipse (12.0f, 10.0f, 10.0f, 8.0f);
    g.drawLine (13.0f, 10.0f, 11.0f, 5.0f, 1.4f);
    g.drawLine (19.0f, 10.0f, 21.0f, 5.0f, 1.4f);

    // 3. Glorp Logo & Wordmark
    if (logo != nullptr)
    {
        auto logoTarget = juce::Rectangle<float> (28.0f, 14.0f, 44.0f, 36.0f);
        logo->drawWithin (g, logoTarget, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);
    }

    g.setFont (displayFont (36.0f));
    g.setColour (theme.ink);
    g.drawText ("GlorpDrums", juce::Rectangle<float> (76.0f, 12.0f, 240.0f, 38.0f),
                juce::Justification::centredLeft, false);

    // 4. Dashed Leader Arrows connecting callouts to drums
    drawLeaderArrows (g);

    // 5. Selected Instrument Badge above the bottom-left knobs
    const auto kLeft = masterVolumeKnob->getX();
    const auto kTop = masterVolumeKnob->getY();
    const auto instName = (selectedInstrumentIndex < processor.getSampler().numInstruments())
        ? processor.getSampler().instrumentName (selectedInstrumentIndex).toUpperCase()
        : "DRUM INSTRUMENT";

    auto badgeArea = juce::Rectangle<float> (static_cast<float> (kLeft),
                                             static_cast<float> (kTop - 20),
                                             static_cast<float> (instPanKnob->getRight() - kLeft),
                                             18.0f);

    g.setColour (theme.panel);
    g.fillRoundedRectangle (badgeArea, 3.0f);
    drawSketchRect (g, badgeArea, theme.accent, 1.3f, 8);

    g.setFont (uiFont (12.0f));
    g.setColour (theme.accent);
    g.fillEllipse (badgeArea.getX() + 6.0f, badgeArea.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.drawText (instName, badgeArea.withTrimmedLeft (16.0f), juce::Justification::centredLeft, true);

    // 6. Level meters in bottom right
    const float meterX = static_cast<float> (fader2->getRight() + 10);
    const float meterY = static_cast<float> (fader2->getY());
    const float meterW = 12.0f;
    const float meterH = static_cast<float> (fader2->getHeight());

    for (int ch = 0; ch < 2; ++ch)
    {
        auto mr = juce::Rectangle<float> (meterX + ch * 16.0f, meterY, meterW, meterH);
        g.setColour (theme.panel);
        g.fillRect (mr);
        drawSketchRect (g, mr, theme.ink, 1.2f, 15 + ch);

        // Crosshatch meter fill
        for (float y = mr.getBottom() - 4.0f; y > mr.getY() + 10.0f; y -= 4.0f)
            g.drawLine (mr.getX() + 2.0f, y, mr.getRight() - 2.0f, y - 2.0f, 1.2f);
    }
}

} // namespace glorp
