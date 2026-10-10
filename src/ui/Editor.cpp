#include "Editor.h"

namespace glorp
{

namespace
{
    constexpr int numInstrumentControls = 3;   ///< level, pan, pitch
}

//==============================================================================
Editor::Editor (PluginProcessor& p)
    : processor (p)
{
    theme = Theme::byIndex (processor.getSettings().themeIndex);

    addAndMakeVisible (menuBar);
    addAndMakeVisible (kitArea);
    kitArea.addAndMakeVisible (kitView);
    addAndMakeVisible (scope);

    buildMenuBar();
    buildGlobalControls();

    kitView.onPieceClicked = [this] (KitPiece piece)
    {
        // Clicking a piece of the kit opens the sample chooser for whichever
        // loaded instrument is mapped to that piece.
        const auto& sampler = processor.getSampler();

        for (int i = 0; i < sampler.numInstruments(); ++i)
        {
            if (DrumKitView::pieceForName (sampler.instrumentName (i)) == piece)
            {
                showSampleMenu (i, juce::Point<int>());
                return;
            }
        }

        statusText = "No instrument is mapped to that part of the kit";
    };

    processor.addListener (this);

    logo = logoDrawable();

    kitView.setMouseClickGrabsKeyboardFocus (false);
    scope.setInterceptsMouseClicks (false, false);

    // Roughly the mockup's proportions.
    setSize (processor.getSettings().editorWidth, processor.getSettings().editorHeight);

    kitChanged();

    // Drains hit events, MIDI-learn results and queued CC values.
    startTimerHz (30);
}

Editor::~Editor()
{
    stopTimer();
    processor.removeListener (this);
}

//==============================================================================
//==============================================================================
void Editor::buildMenuBar()
{
    menuBar.setModel (this);
}

juce::StringArray Editor::getMenuBarNames()
{
    return { "FILE", "PRESETS", "MIDI", "SETTINGS" };
}

juce::PopupMenu Editor::getMenuForIndex (int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (topLevelMenuIndex == 0) // FILE
    {
        auto& settings = processor.getSettings();

        if (! settings.recentKits.isEmpty())
        {
            for (const auto& recent : settings.recentKits)
            {
                juce::String label = juce::File (recent).getFileNameWithoutExtension();

                if (recent == processor.getCurrentKitPath())
                    label += "  (loaded)";

                menu.addItem (label, [this, recent] { processor.loadKit (recent); });
            }

            menu.addSeparator();
        }

        menu.addItem ("Refresh kit list", [this]
        {
            processor.refreshKitList();
            statusText = juce::String (processor.getKitList().size()) + " kits found";
            repaint();
        });

        menu.addItem ("Choose kit folder...", [this]
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Add a folder to scan for drumkits",
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

        for (const auto& p : processor.getSettings().extraKitPaths)
            menu.addItem ("Remove: " + juce::File (p).getFileName(),
                          [this, p]
                          {
                              processor.getSettings().removeKitPath (p);
                              processor.getSettings().saveIfChanged();
                              processor.refreshKitList();
                          });

        menu.addSeparator();
        menu.addItem ("Panic (all notes off)",
                      [this] { processor.getSampler().allNotesOff(); });
    }
    else if (topLevelMenuIndex == 1) // PRESETS
    {
        for (const auto& preset : MidiMap::availablePresets())
            menu.addItem (preset.name, [this, preset] { processor.applyMapPreset (preset.name); });

        menu.addSeparator();
        menu.addItem ("Save current map as...", [this]
        {
            auto& map = processor.getMidiMap();
            const auto dir = MidiMap::userPresetDirectory();

            std::vector<juce::String> names;

            for (int i = 0; i < processor.getSampler().numInstruments(); ++i)
                names.push_back (processor.getSampler().instrumentName (i));

            auto fileName = "MyMap.xml";
            map.savePresetFile (dir.getChildFile (fileName), "My Map", names);

            statusText = "Saved to " + dir.getFullPathName();
            rebuildCallouts();
        });

        menu.addItem ("Rescan preset folder", [this]
        {
            rebuildCallouts();
            statusText = "Presets rescanned";
        });
    }
    else if (topLevelMenuIndex == 2) // MIDI
    {
        auto& map = processor.getMidiMap();

        menu.addItem ("Panic (all notes off)",
                      [this] { processor.getSampler().allNotesOff(); });

        menu.addSeparator();

        if (map.isLearning())
            menu.addItem ("Cancel MIDI learn",
                          [this] { processor.getMidiMap().cancelLearn(); learningInstrument = -1; });

        menu.addItem ("Clear note map", [this]
        {
            processor.getMidiMap().clearAssignments();
            updateCalloutLabels();
        });

        menu.addSeparator();
        menu.addItem ("Clear all MIDI bindings", [this]
        {
            processor.getMidiMap().clearBindings();
            statusText = "MIDI bindings cleared";
        });
    }
    else if (topLevelMenuIndex == 3) // SETTINGS
    {
        menu.addItem (themeName() == "Sketch" ? "Theme: dark" : "Theme: sketch", [this]
        {
            auto& settings = processor.getSettings();
            settings.themeIndex = settings.themeIndex == 0 ? 1 : 0;
            settings.saveIfChanged();
            theme = Theme::byIndex (settings.themeIndex);
            rebuildCallouts();
            repaint();
        });

        menu.addSeparator();
        menu.addItem ("Reset all instrument controls", [this]
        {
            processor.getSampler().resetInstrumentParams();
            updateCalloutLabels();
            repaint();
        });

        menu.addSeparator();
        menu.addItem ("About GlorpDrums", [this]
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon, "GlorpDrums",
                "GlorpDrums\n\nDrumGizmo kit player with per-instrument outputs,\n"
                "MIDI learn and transient-preserving tuning.");
        });
    }

    return menu;
}

juce::String Editor::themeName() const
{
    return theme.isDark ? "Dark" : "Sketch";
}

//==============================================================================
void Editor::buildGlobalControls()
{
    struct GlobalSpec
    {
        const char* label;
        const char* parameterId;
        float defaultValue;
    };

    static const GlobalSpec specs[] =
    {
        { "VOLUME", "masterLevel", 0.8f },
        { "PAN",    "masterPan",   0.5f },
        { "PITCH",  "inst0.pitch", 0.5f },
        { "LEVEL",  "inst0.level", 1.0f },
    };

    for (const auto& spec : specs)
    {
        auto knob = std::make_unique<Knob> (spec.label);

        if (auto* parameter = processor.findParameter (spec.parameterId))
        {
            knob->setDoubleClickReturnValue (true, spec.defaultValue);
            knob->attachToParameter (parameter);
        }

        addAndMakeVisible (*knob);
        globalKnobs.push_back (std::move (knob));
    }
}

void Editor::buildInstrumentControls()
{
    for (auto& knob : instrumentKnobs)
        removeChildComponent (knob.get());

    instrumentKnobs.clear();

    const auto count = processor.getSampler().numInstruments();
    instrumentKnobs.reserve (static_cast<size_t> (count * numInstrumentControls));

    for (int i = 0; i < count; ++i)
    {
        struct Spec { const char* suffix; const char* label; float defaultValue; };
        static const Spec specs[] =
        {
            { "level", "LEVEL", 1.0f },
            { "pan",   "PAN",   0.5f },
            { "pitch", "PITCH", 0.5f },
        };

        for (const auto& spec : specs)
        {
            auto knob = std::make_unique<Knob> (spec.label);
            const auto id = PluginProcessor::controlIdForInstrument (spec.suffix, i);

            if (auto* parameter = processor.findParameter (id))
            {
                knob->setDoubleClickReturnValue (true, spec.defaultValue);
                knob->attachToParameter (parameter);
            }

            addAndMakeVisible (*knob);
            instrumentKnobs.push_back (std::move (knob));
        }
    }
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
    callouts.reserve (static_cast<size_t> (count));

    for (int i = 0; i < count; ++i)
    {
        auto box = std::make_unique<CalloutBox>();
        box->setInstrumentName (processor.getSampler().instrumentName (i));

        kitArea.addAndMakeVisible (*box);

        const auto instrumentIndex = i;

        box->onSelectClicked = [this, instrumentIndex]
        {
            showSampleMenu (instrumentIndex, juce::Point<int>());
        };

        box->onAutoClicked = [this, instrumentIndex] { toggleAuto (instrumentIndex); };
        box->onLearnClicked = [this, instrumentIndex] { armLearn (instrumentIndex); };
        box->onMidiLabelClicked = [this, instrumentIndex] { armLearn (instrumentIndex); };

        callouts.push_back (std::move (box));
    }

    updateCalloutLabels();
    resized();
}

void Editor::armLearn (int instrument)
{
    auto& map = processor.getMidiMap();

    learningInstrument = instrument;
    map.beginLearn ("note" + std::to_string (instrument));

    statusText = "MIDI learn armed - play a note";
    updateCalloutLabels();
}

void Editor::toggleAuto (int instrument)
{
    auto params = processor.getSampler().getInstrumentParams (instrument);
    params.autoRoundRobin = ! params.autoRoundRobin;

    if (! params.autoRoundRobin)
        params.selectedSample = 0;

    processor.getSampler().setInstrumentParams (instrument, params);
    updateCalloutLabels();
}

void Editor::showSampleMenu (int instrument, const juce::Point<int>&)
{
    juce::PopupMenu menu;
    const auto sampler = &processor.getSampler();

    for (int s = 0; s < sampler->numSamplesForInstrument (instrument); ++s)
        menu.addItem (sampler->sampleName (instrument, s), [this, instrument, s]
        {
            auto params = processor.getSampler().getInstrumentParams (instrument);
            params.selectedSample = s;
            params.autoRoundRobin = false;
            processor.getSampler().setInstrumentParams (instrument, params);
            updateCalloutLabels();
        });

    if (menu.getNumItems() == 0)
    {
        statusText = "This instrument has no samples";
        return;
    }

    menu.addSeparator();
    menu.addItem ("AUTO (round robin)", [this, instrument] { toggleAuto (instrument); });

    if (instrument < static_cast<int> (callouts.size()))
    {
        auto* target = callouts[static_cast<size_t> (instrument)].get();
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
    }
}

juce::String Editor::sampleLabelFor (int instrument) const
{
    const auto params = processor.getSampler().getInstrumentParams (instrument);

    if (params.autoRoundRobin)
        return "AUTO (" + juce::String (processor.getSampler().numSamplesForInstrument (instrument)) + ")";

    return processor.getSampler().sampleName (instrument, params.selectedSample);
}

juce::String Editor::noteLabelFor (int instrument) const
{
    const auto note = processor.getMidiMap().noteForInstrument (instrument);

    if (note < 0)
        return "-";

    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
}

void Editor::updateCalloutLabels()
{
    const auto mapIsLearning = processor.getMidiMap().isLearning();

    for (int i = 0; i < static_cast<int> (callouts.size()); ++i)
    {
        auto& box = callouts[static_cast<size_t> (i)];

        box->setInstrumentName (processor.getSampler().instrumentName (i));
        box->setSampleName (sampleLabelFor (i));
        box->setNoteLabel (noteLabelFor (i));
        box->setAutoEnabled (processor.getSampler().getInstrumentParams (i).autoRoundRobin);
        box->setLearnArmed (mapIsLearning && learningInstrument == i);
    }
}

//==============================================================================
void Editor::kitChanged()
{
    kitView.setInstrumentNames ([&]
    {
        std::vector<juce::String> names;

        for (int i = 0; i < processor.getSampler().numInstruments(); ++i)
            names.push_back (processor.getSampler().instrumentName (i));

        return names;
    }());

    buildInstrumentControls();
    rebuildCallouts();

    kitErrorText = processor.getLastKitError();

    statusText = processor.getSampler().isKitLoaded()
        ? juce::String (processor.getSampler().numInstruments()) + " instruments, "
            + juce::String (processor.getSampler().usedBusCount()) + " output buses"
        : "No kit loaded";

    repaint();
}

//==============================================================================
void Editor::resized()
{
    auto b = getLocalBounds();

    menuBar.setBounds (b.removeFromTop (26));

    auto bottom = b.removeFromBottom (juce::jlimit (48, 90, b.getHeight() / 5));
    scope.setBounds (bottom);

    auto left = b.removeFromLeft (juce::jlimit (76, 108, b.getWidth() / 8));
    auto right = b.removeFromRight (juce::jlimit (96, 150, b.getWidth() / 7));

    kitArea.setBounds (b);
    kitView.setBounds (kitArea.getLocalBounds());

    // Global knobs in a 2x2 block on the left, as in the mockup.
    {
        const auto knobSize = juce::jmin (78, left.getWidth());
        int index = 0;

        for (auto& knob : globalKnobs)
        {
            const auto col = index % 2;
            const auto row = index / 2;
            const auto cellHeight = left.getHeight() / 2;

            knob->setBounds (col * (left.getWidth() / 2),
                             row * cellHeight,
                             left.getWidth() / 2,
                             cellHeight);

            juce::ignoreUnused (knobSize);
            ++index;
        }
    }

    // Per-instrument knobs in a scrolling-free column on the right.
    {
        auto& sampler = processor.getSampler();
        const auto count = sampler.numInstruments();
        const auto rows = juce::jmax (1, count);
        const auto rowHeight = juce::jmax (26, right.getHeight() / rows);

        for (int i = 0; i < count; ++i)
        {
            for (int c = 0; c < numInstrumentControls; ++c)
            {
                const auto index = i * numInstrumentControls + c;

                if (index < static_cast<int> (instrumentKnobs.size()))
                    instrumentKnobs[static_cast<size_t> (index)]->setBounds (
                        c * (right.getWidth() / numInstrumentControls),
                        i * rowHeight,
                        right.getWidth() / numInstrumentControls,
                        rowHeight);
            }
        }
    }

    // Callouts: two columns flanking the kit, tethered to the nearest piece.
    layoutCallouts (right.getWidth());
}

void Editor::layoutCallouts (int rightWidth)
{
    juce::ignoreUnused (rightWidth);

    const auto area = kitArea.getLocalBounds().toFloat();
    const auto count = static_cast<int> (callouts.size());

    if (count <= 0)
        return;

    const auto boxWidth = juce::jlimit (130.0f, 176.0f, area.getWidth() * 0.19f);
    const auto boxHeight = juce::jlimit (78.0f, 100.0f, boxWidth * 0.62f);

    // Alternate left and right so the leader lines do not cross into a tangle.
    for (int i = 0; i < count; ++i)
    {
        auto& box = callouts[static_cast<size_t> (i)];

        const auto onLeft = (i % 2) == 0;
        const auto row = i / 2;

        const auto perColumn = (count + 1) / 2;
        const auto x = onLeft ? area.getX() + 4.0f
                              : area.getRight() - boxWidth - 4.0f;

        const auto step = juce::jmax (boxHeight + 10.0f,
                                      (area.getHeight() - 20.0f) / juce::jmax (1, perColumn));
        const auto y = area.getY() + 10.0f + row * step;

        box->setBounds (juce::Rectangle<float> (x, y, boxWidth, boxHeight).toNearestInt());

        // Tether to whichever drawn piece this instrument corresponds to.
        const auto piece = DrumKitView::pieceForName (processor.getSampler().instrumentName (i));
        const auto anchor = kitView.anchorFor (piece);

        box->setAnchor (anchor);
    }
}

//==============================================================================
void Editor::timerCallback()
{
    // Drain hit events written by the audio thread.
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

    // Apply CC values learned from MIDI on the message thread.
    processor.drainControlQueue();

    // MIDI learn finishes when the map stops listening; pick that up and
    // refresh the callouts so the new note shows up immediately.
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
void Editor::paint (juce::Graphics& g)
{
    g.fillAll (theme.background);

    // Draw the kit's own line art behind everything else.
    kitArea.setOpaque (false);

    // Wordmark.
    if (logo != nullptr)
    {
        auto target = getLocalBounds().toFloat().withTrimmedLeft (24.0f)
                                                    .withTrimmedTop (36.0f)
                                                    .withHeight (64.0f)
                                                    .withWidth (200.0f);

        g.setOpacity (0.95f);
        logo->drawWithin (g, target, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);
        g.setOpacity (1.0f);
    }

    g.setFont (displayFont (44.0f));
    g.setColour (theme.ink);
    g.drawText ("GlorpDrums", getLocalBounds().toFloat().withTrimmedLeft (86.0f).withTrimmedTop (38.0f)
                                   .withHeight (56.0f).withWidth (360.0f),
                juce::Justification::centredLeft, false);

    // Status line.
    g.setFont (uiFont (15.0f));
    g.setColour (kitErrorText.isNotEmpty() ? theme.warning : theme.inkSoft);
    g.drawText (kitErrorText.isNotEmpty() ? kitErrorText : statusText,
                getLocalBounds().toFloat().removeFromTop (30.0f).withTrimmedLeft (24.0f),
                juce::Justification::centredLeft, true);

    // Outer frame, like the mockup's window border.
    drawSketchRect (g, getLocalBounds().toFloat().reduced (2.0f), theme.ink.withAlpha (0.55f), 2.0f, 1);
}

} // namespace glorp
