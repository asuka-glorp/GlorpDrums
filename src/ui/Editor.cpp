#include "Editor.h"

namespace glorp
{

//==============================================================================
void Editor::MenuButton::paintButton (juce::Graphics& g, bool isHover, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (2.0f);

    if (isHover || isDown)
    {
        g.setColour (juce::Colour (0xffea8c16).withAlpha (isDown ? 0.24f : 0.12f));
        g.fillRoundedRectangle (b, 4.0f);
        drawSketchRect (g, b, juce::Colour (0xffea8c16), 1.5f, 7);
    }

    g.setFont (displayFont (19.0f));
    g.setColour (isHover ? juce::Colour (0xffea8c16) : juce::Colour (0xff111215));
    g.drawText (getName(), b, juce::Justification::centred, false);
}

void Editor::InstrumentSelectorButton::paintButton (juce::Graphics& g, bool isHover, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);

    // Inner background
    g.setColour (juce::Colour (0xfffcfdfc));
    g.fillRoundedRectangle (b, 4.0f);

    // Orange sketched border
    const auto borderCol = juce::Colour (0xffea8c16);
    g.setColour (borderCol);
    drawSketchRect (g, b, borderCol, isHover ? 1.8f : 1.4f, 9);

    // Orange bullet dot
    const float dotRadius = 4.0f;
    const float dotX = b.getX() + 12.0f;
    const float dotY = b.getCentreY();
    g.setColour (borderCol);
    g.fillEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);

    // Down chevron on right
    juce::Path chevron;
    const float cx = b.getRight() - 14.0f;
    const float cy = b.getCentreY();
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.strokePath (chevron, juce::PathStrokeType (1.5f));

    // Instrument Name text
    auto textArea = b.withTrimmedLeft (22.0f).withTrimmedRight (20.0f);
    g.setFont (displayFont (16.0f));
    g.setColour (isHover ? borderCol : juce::Colour (0xff111215));
    g.drawText (currentName.toUpperCase(), textArea, juce::Justification::centred, true);
}

void Editor::KitSelectorButton::paintButton (juce::Graphics& g, bool isHover, bool /*isDown*/)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);

    // Inner paper background
    g.setColour (juce::Colour (0xfffcfdfc));
    g.fillRoundedRectangle (b, 4.0f);

    // Sketched border with orange hover accent
    const auto borderCol = isHover ? juce::Colour (0xffea8c16) : juce::Colour (0xff111215);
    drawSketchRect (g, b, borderCol, isHover ? 1.8f : 1.4f, 11);

    // Small comic "KIT:" label on left
    g.setFont (displayFont (12.0f));
    g.setColour (juce::Colour (0xff70747c));
    g.drawText ("KIT:", juce::Rectangle<float> (b.getX() + 10.0f, b.getY(), 32.0f, b.getHeight()),
                juce::Justification::centredLeft, false);

    // Down chevron on right
    juce::Path chevron;
    const float cx = b.getRight() - 14.0f;
    const float cy = b.getCentreY();
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);
    g.setColour (isHover ? juce::Colour (0xffea8c16) : juce::Colour (0xff111215));
    g.strokePath (chevron, juce::PathStrokeType (1.5f));

    // Kit title text in center
    auto textArea = b.withTrimmedLeft (44.0f).withTrimmedRight (24.0f);
    g.setFont (displayFont (15.0f));
    g.setColour (isHover ? juce::Colour (0xffea8c16) : juce::Colour (0xff111215));
    g.drawText (currentName.toUpperCase(), textArea, juce::Justification::centred, true);
}

void Editor::OutputBusButton::paintButton (juce::Graphics& g, bool isHover, bool /*isDown*/)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (juce::Colour (0xfffcfdfc));
    g.fillRoundedRectangle (b, 3.0f);

    const auto borderCol = isHover ? juce::Colour (0xffea8c16) : juce::Colour (0x66111215);
    drawSketchRect (g, b, borderCol, 1.0f, 4);

    const juce::String text = "OUT: " + juce::String (currentBus * 2 + 1) + "-" + juce::String (currentBus * 2 + 2)
                            + (currentBus == 0 ? " (MAIN)  ▼" : "  ▼");

    g.setFont (displayFont (12.5f));
    g.setColour (isHover ? juce::Colour (0xffea8c16) : juce::Colour (0xff111215));
    g.drawText (text, b, juce::Justification::centred, true);
}

Editor::ZoneButton::ZoneButton (const juce::String& name, bool active)
    : juce::Button (name), isActive (active)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Editor::ZoneButton::setActive (bool active)
{
    if (isActive != active)
    {
        isActive = active;
        repaint();
    }
}

void Editor::ZoneButton::paintButton (juce::Graphics& g, bool isHover, bool /*isDown*/)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);

    // Clean white background
    g.setColour (juce::Colour (0xfffcfdfc));
    g.fillRoundedRectangle (b, 3.0f);

    if (isActive)
    {
        const auto orangeCol = juce::Colour (0xffea8c16);
        drawSketchRect (g, b, orangeCol, isHover ? 1.8f : 1.4f, 5);
        g.setFont (displayFont (13.5f));
        g.setColour (orangeCol);
        g.drawText (getName(), b, juce::Justification::centred, true);
    }
    else
    {
        const auto borderCol = isHover ? juce::Colour (0xffea8c16) : juce::Colour (0x66111215);
        drawSketchRect (g, b, borderCol, 1.0f, 3);
        g.setFont (displayFont (13.0f));
        g.setColour (isHover ? juce::Colour (0xffea8c16) : juce::Colour (0xff111215));
        g.drawText (getName(), b, juce::Justification::centred, true);
    }
}

void Editor::LearnButton::setLearning (bool learning)
{
    if (isLearning_ != learning)
    {
        isLearning_ = learning;
        repaint();
    }
}

void Editor::LearnButton::paintButton (juce::Graphics& g, bool isHover, bool /*isDown*/)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xfffcfdfc));
    g.fillRoundedRectangle (b, 3.0f);

    const auto orangeCol = juce::Colour (0xffea8c16);
    drawSketchRect (g, b, orangeCol, isHover ? 1.8f : 1.4f, 7);

    g.setFont (displayFont (14.5f));
    if (isLearning_)
    {
        g.setColour (orangeCol);
        g.drawText ("HIT PAD...", b, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (isHover ? orangeCol : juce::Colour (0xff111215));
        g.drawText ("LEARN", b, juce::Justification::centred, false);
    }
}

void Editor::CurveButton::paintButton (juce::Graphics& g, bool isHover, bool /*isDown*/)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xfffcfdfc));
    g.fillRoundedRectangle (b, 3.0f);

    const auto orangeCol = juce::Colour (0xffea8c16);
    drawSketchRect (g, b, isHover ? orangeCol : juce::Colour (0xff7a7d7a), isHover ? 1.6f : 1.2f, 6);

    juce::String label = "CURVE: LIN";
    switch (currentCurve)
    {
        case VelocityCurve::logarithmic: label = "CURVE: LOG"; break;
        case VelocityCurve::exponential: label = "CURVE: EXP"; break;
        case VelocityCurve::sCurve:     label = "CURVE: S-CURV"; break;
        case VelocityCurve::linear:
        default: break;
    }

    g.setFont (displayFont (13.5f));
    g.setColour (isHover ? orangeCol : juce::Colour (0xff222522));
    g.drawText (label, b, juce::Justification::centred, false);
}

//==============================================================================
Editor::Editor (PluginProcessor& p)
    : processor (p)
{
    theme = Theme::byIndex (processor.getSettings().themeIndex);

    addAndMakeVisible (kitView);
    addAndMakeVisible (scope);

    edrumWizard = std::make_unique<EDrumWizard> (processor);
    addChildComponent (*edrumWizard);
    edrumWizard->onClose = [this]
    {
        updateInstrumentDisplay();
        repaint();
    };

    buildHeaderMenu();
    buildControls();

    // Sound and animation when an instrument in the kit view is clicked
    kitView.onPieceHit = [this] (KitPiece piece, float velocity)
    {
        int targetLogicalIdx = -1;
        for (size_t i = 0; i < logicalInstruments.size(); ++i)
        {
            if (logicalInstruments[i].piece == piece)
            {
                targetLogicalIdx = static_cast<int> (i);
                break;
            }
        }

        if (targetLogicalIdx >= 0)
        {
            selectLogicalInstrument (targetLogicalIdx);
            const int targetZone = (targetLogicalIdx < static_cast<int> (logicalInstruments.size()))
                                       ? logicalInstruments[targetLogicalIdx].selectedZoneIndex : 0;
            selectZone (targetLogicalIdx, targetZone, false);

            const int samplerIdx = getActiveSamplerIndex();
            if (samplerIdx >= 0)
            {
                const auto note = processor.getMidiMap().noteForInstrument (samplerIdx);
                const int midiNote = (note >= 0) ? note : 36;

                const auto instName = processor.getSampler().instrumentName (samplerIdx);
                processor.getSampler().noteOn (samplerIdx, midiNote, velocity,
                                               processor.getSampler().currentSamplePosition());

                scope.hitReceived (instName, piece, midiNote, velocity);
            }

            kitView.triggerHitForPiece (piece, velocity);
        }
    };

    kitView.onPieceClicked = [this] (KitPiece piece)
    {
        for (size_t i = 0; i < logicalInstruments.size(); ++i)
        {
            if (logicalInstruments[i].piece == piece)
            {
                selectLogicalInstrument (static_cast<int> (i));
                const int targetZone = logicalInstruments[i].selectedZoneIndex;
                selectZone (static_cast<int> (i), targetZone, false);
                break;
            }
        }
    };

    processor.addListener (this);
    logo = logoDrawable();

    kitView.setMouseClickGrabsKeyboardFocus (false);
    scope.setInterceptsMouseClicks (false, false);
    setWantsKeyboardFocus (true);

    setSize (processor.getSettings().editorWidth, processor.getSettings().editorHeight);

    kitChanged();
    startTimerHz (30);
}

Editor::~Editor()
{
    stopTimer();
    processor.removeListener (this);
}

bool Editor::keyPressed (const juce::KeyPress& key)
{
    // Developer toggle shortcut: Ctrl + Shift + E or Cmd + Shift + E
    if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown()
        && (key.getKeyCode() == 'E' || key.getKeyCode() == 'e'))
    {
        kitView.setEditMode (! kitView.isEditMode());
        return true;
    }
    return false;
}

void Editor::mouseDown (const juce::MouseEvent& e)
{
    // Developer gesture: Alt-click or triple-click the top-left Glorp Logo/Title to toggle WYSIWYG edit mode
    const auto logoTarget = juce::Rectangle<float> (24.0f, 8.0f, 220.0f, 54.0f);
    if (logoTarget.contains (e.position) && (e.mods.isAltDown() || e.getNumberOfClicks() >= 3))
    {
        kitView.setEditMode (! kitView.isEditMode());
        return;
    }
}

bool Editor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".glorpkit" || ext == ".zip" || ext == ".xml")
            return true;
    }
    return false;
}

void Editor::filesDropped (const juce::StringArray& files, int /*x*/, int /*y*/)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".glorpkit" || ext == ".zip" || ext == ".xml")
        {
            processor.loadKit (f);
            break;
        }
    }
}

//==============================================================================
void Editor::buildHeaderMenu()
{
    addAndMakeVisible (fileButton);
    addAndMakeVisible (presetsButton);
    addAndMakeVisible (midiButton);
    addAndMakeVisible (settingsButton);

    auto showMenu = [this] (juce::PopupMenu& m, juce::Component* target)
    {
        juce::PopupMenu::Options opts;
        opts = opts.withTargetComponent (target)
                   .withParentComponent (this);
        m.showMenuAsync (opts);
    };

    fileButton.onClick = [this, showMenu]
    {
        auto menu = buildFileMenu();
        showMenu (menu, &fileButton);
    };

    presetsButton.onClick = [this, showMenu]
    {
        auto menu = buildPresetsMenu();
        showMenu (menu, &presetsButton);
    };

    midiButton.onClick = [this, showMenu]
    {
        auto menu = buildMidiMenu();
        showMenu (menu, &midiButton);
    };

    settingsButton.onClick = [this, showMenu]
    {
        auto menu = buildSettingsMenu();
        showMenu (menu, &settingsButton);
    };
}

void Editor::promptLoadKitFile()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Select DrumKit (.glorpkit, .xml, .zip)",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory),
        "*.glorpkit;*.xml;*.zip");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto chosen = fc.getResult();
            if (chosen.existsAsFile())
                processor.loadKit (chosen.getFullPathName());
        });
}

void Editor::openEDrumWizard()
{
    if (edrumWizard != nullptr)
    {
        edrumWizard->setBounds (getLocalBounds());
        edrumWizard->open();
        edrumWizard->toFront (true);
    }
}

juce::PopupMenu Editor::buildFileMenu()
{
    juce::PopupMenu menu;

    menu.addItem ("Load DrumKit (.glorpkit, .xml)...", [this]
    {
        promptLoadKitFile();
    });

    menu.addSeparator();

    // Discovered drumkits list
    const auto& kitList = processor.getKitList();
    if (! kitList.empty())
    {
        juce::PopupMenu kitsSubmenu;
        for (const auto& entry : kitList)
        {
            const juce::String path (entry.drumkitPath);
            const bool isCurrent = (processor.getSettings().lastKitPath == path);
            kitsSubmenu.addItem (entry.displayName, true, isCurrent, [this, path]
            {
                processor.loadKit (path);
            });
        }
        menu.addSubMenu ("Available Kits", kitsSubmenu);
    }

    // Recent kits list
    const auto& recents = processor.getSettings().recentKits;
    if (! recents.isEmpty())
    {
        juce::PopupMenu recentsSubmenu;
        for (int i = 0; i < recents.size(); ++i)
        {
            const auto path = recents[i];
            const auto name = juce::File (path).getFileName();
            recentsSubmenu.addItem (name, [this, path] { processor.loadKit (path); });
        }
        menu.addSubMenu ("Recent Kits", recentsSubmenu);
    }

    menu.addSeparator();

    menu.addItem ("Add Kit Folder...", [this]
    {
        auto chooser = std::make_shared<juce::FileChooser> ("Select Folder Containing DrumKits",
            juce::File::getSpecialLocation (juce::File::userHomeDirectory));

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
            [this, chooser] (const juce::FileChooser& fc)
            {
                const auto chosen = fc.getResult();
                if (chosen.isDirectory())
                {
                    processor.getSettings().addKitPath (chosen.getFullPathName());
                    processor.getSettings().saveIfChanged();
                    processor.refreshKitList();
                    kitChanged();
                }
            });
    });

    menu.addSeparator();

    const bool multiOut = processor.getSampler().isMultiOutMode();
    menu.addItem ("Multi-Output Mode (16 Stems)", true, multiOut, [this, multiOut]
    {
        processor.getSampler().setMultiOutMode (! multiOut);
        statusText = (! multiOut) ? "Multi-out mode active (16 stereo buses)" : "Stereo mix mode active (all to 1-2)";
        repaint();
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
        repaint();
    });

    menu.addItem ("Rescan Presets", [this]
    {
        statusText = "Presets rescanned";
        repaint();
    });

    return menu;
}

juce::PopupMenu Editor::buildMidiMenu()
{
    juce::PopupMenu menu;
    auto& map = processor.getMidiMap();

    menu.addItem ("E-Drum Setup Wizard...", [this] { openEDrumWizard(); });
    menu.addSeparator();

    menu.addItem ("Panic (All Notes Off)", [this] { processor.getSampler().allNotesOff(); });
    menu.addSeparator();

    if (map.isLearning())
        menu.addItem ("Cancel MIDI Learn", [this]
        {
            processor.getMidiMap().cancelLearn();
            learningInstrument = -1;
            updateInstrumentDisplay();
        });

    menu.addItem ("Clear Note Map", [this]
    {
        processor.getMidiMap().clearAssignments();
        updateInstrumentDisplay();
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
        repaint();
    });

    menu.addSeparator();
    menu.addItem ("Reset All Instrument Controls", [this]
    {
        processor.getSampler().resetInstrumentParams();
        updateInstrumentDisplay();
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
    // Left Panel: Instrument Selector Button
    addAndMakeVisible (instSelectorButton);
    instSelectorButton.onClick = [this]
    {
        juce::PopupMenu menu;
        for (size_t i = 0; i < logicalInstruments.size(); ++i)
        {
            const auto& inst = logicalInstruments[i];
            menu.addItem (inst.name, true, static_cast<int> (i) == selectedLogicalIndex, [this, i]
            {
                selectLogicalInstrument (static_cast<int> (i));
                selectZone (static_cast<int> (i), 0, false);
            });
        }

        juce::PopupMenu::Options opts;
        opts = opts.withTargetComponent (&instSelectorButton)
                   .withParentComponent (this);
        menu.showMenuAsync (opts);
    };

    // Kit Selector Dropdown Button (centered above drum kit graphic)
    addAndMakeVisible (kitSelectorButton);
    kitSelectorButton.onClick = [this]
    {
        juce::PopupMenu menu;
        const auto& kitList = processor.getKitList();
        const auto currentPath = processor.getCurrentKitPath();

        menu.addSectionHeader ("AVAILABLE KITS");
        bool hasKits = false;
        for (const auto& entry : kitList)
        {
            if (! entry.valid)
                continue;

            hasKits = true;
            const juce::String path (entry.drumkitPath);
            const bool isCurrent = (path == currentPath);
            menu.addItem (entry.displayName, true, isCurrent, [this, path]
            {
                processor.loadKit (path);
            });
        }

        if (! hasKits)
            menu.addItem ("(No kits found in scan folders)", false, false, nullptr);

        menu.addSeparator();
        menu.addItem ("Browse for DrumKit (.glorpkit, .xml)...", [this]
        {
            promptLoadKitFile();
        });

        menu.addItem ("Rescan Kit Folders", [this]
        {
            processor.refreshKitList();
            kitChanged();
        });

        juce::PopupMenu::Options opts;
        opts = opts.withTargetComponent (&kitSelectorButton)
                   .withParentComponent (this);
        menu.showMenuAsync (opts);
    };

    // Left Panel: Output Bus Selector Button
    addAndMakeVisible (outBusButton);
    outBusButton.onClick = [this]
    {
        if (logicalInstruments.empty() || selectedLogicalIndex < 0 || selectedLogicalIndex >= static_cast<int> (logicalInstruments.size()))
            return;

        auto& inst = logicalInstruments[selectedLogicalIndex];
        juce::PopupMenu menu;

        for (int b = 0; b < 16; ++b)
        {
            const juce::String busLabel = "Out " + juce::String (b * 2 + 1) + "/" + juce::String (b * 2 + 2)
                                        + (b == 0 ? " (Main / Stereo Mix)" : "");
            menu.addItem (busLabel, true, b == inst.outputBus, [this, b]
            {
                if (selectedLogicalIndex >= 0 && selectedLogicalIndex < static_cast<int> (logicalInstruments.size()))
                {
                    auto& currentInst = logicalInstruments[selectedLogicalIndex];
                    currentInst.outputBus = b;
                    outBusButton.setBusIndex (b);

                    for (const auto& z : currentInst.zones)
                    {
                        processor.getSampler().setInstrumentBus (z.samplerIndex, b);
                        if (auto* p = processor.findParameter ("inst" + juce::String (z.samplerIndex) + ".bus"))
                            p->setValueNotifyingHost (static_cast<float> (b) / 15.0f);
                    }
                    statusText = currentInst.name + " routed to Out " + juce::String (b * 2 + 1) + "/" + juce::String (b * 2 + 2);
                    repaint();
                }
            });
        }

        juce::PopupMenu::Options opts;
        opts = opts.withTargetComponent (&outBusButton)
                   .withParentComponent (this);
        menu.showMenuAsync (opts);
    };

    // Left Panel: 4 Knobs (2x2 Grid)
    instVolumeKnob = std::make_unique<Knob> ("VOLUME");
    instVolumeKnob->setDoubleClickReturnValue (true, 0.5);
    addAndMakeVisible (*instVolumeKnob);

    instPanKnob = std::make_unique<Knob> ("PAN");
    instPanKnob->setDoubleClickReturnValue (true, 0.5);
    addAndMakeVisible (*instPanKnob);

    instPitchKnob = std::make_unique<Knob> ("PITCH");
    instPitchKnob->setDoubleClickReturnValue (true, 0.5);
    addAndMakeVisible (*instPitchKnob);

    instDynamicsKnob = std::make_unique<Knob> ("DYNAMICS");
    instDynamicsKnob->setDoubleClickReturnValue (true, 0.5);
    addAndMakeVisible (*instDynamicsKnob);

    // Left Panel: Velocity Curve Button
    addAndMakeVisible (instCurveButton);
    instCurveButton.onClick = [this]
    {
        const int samplerIdx = getActiveSamplerIndex();
        if (samplerIdx < 0)
            return;

        const auto currentParams = processor.getSampler().getInstrumentParams (samplerIdx);
        juce::PopupMenu menu;
        menu.addSectionHeader ("VELOCITY RESPONSE");
        menu.addItem ("Linear (1:1 Natural)", true, currentParams.velocityCurve == VelocityCurve::linear, [this, samplerIdx]
        {
            auto p = processor.getSampler().getInstrumentParams (samplerIdx);
            p.velocityCurve = VelocityCurve::linear;
            processor.getSampler().setInstrumentParams (samplerIdx, p);
            updateInstrumentDisplay();
        });
        menu.addItem ("Logarithmic (Boost Soft Hits)", true, currentParams.velocityCurve == VelocityCurve::logarithmic, [this, samplerIdx]
        {
            auto p = processor.getSampler().getInstrumentParams (samplerIdx);
            p.velocityCurve = VelocityCurve::logarithmic;
            processor.getSampler().setInstrumentParams (samplerIdx, p);
            updateInstrumentDisplay();
        });
        menu.addItem ("Exponential (Tight / Hard Hits)", true, currentParams.velocityCurve == VelocityCurve::exponential, [this, samplerIdx]
        {
            auto p = processor.getSampler().getInstrumentParams (samplerIdx);
            p.velocityCurve = VelocityCurve::exponential;
            processor.getSampler().setInstrumentParams (samplerIdx, p);
            updateInstrumentDisplay();
        });
        menu.addItem ("S-Curve (Enhanced Mid-Dynamics)", true, currentParams.velocityCurve == VelocityCurve::sCurve, [this, samplerIdx]
        {
            auto p = processor.getSampler().getInstrumentParams (samplerIdx);
            p.velocityCurve = VelocityCurve::sCurve;
            processor.getSampler().setInstrumentParams (samplerIdx, p);
            updateInstrumentDisplay();
        });

        juce::PopupMenu::Options opts;
        opts = opts.withTargetComponent (&instCurveButton)
                   .withParentComponent (this);
        menu.showMenuAsync (opts);
    };

    // Left Panel: Learn Button
    addAndMakeVisible (instLearnButton);
    instLearnButton.onClick = [this]
    {
        const int samplerIdx = getActiveSamplerIndex();
        if (samplerIdx >= 0)
            armLearn (samplerIdx);
    };

    // Bottom-Right: Master Volume Knob
    masterVolumeKnob = std::make_unique<Knob> ("VOLUME");
    if (auto* p = processor.findParameter ("masterLevel"))
    {
        masterVolumeKnob->setDoubleClickReturnValue (true, 0.8);
        masterVolumeKnob->attachToParameter (p);
    }
    addAndMakeVisible (*masterVolumeKnob);

    bindInstrumentKnobs();
    updateInstrumentDisplay();
}

void Editor::buildLogicalInstruments()
{
    logicalInstruments.clear();
    const auto& sampler = processor.getSampler();
    const int count = sampler.numInstruments();

    if (count == 0)
    {
        LogicalInstrument dummy;
        dummy.name = "SNARE";
        dummy.piece = KitPiece::snare;
        dummy.zones.push_back ({ 0, "Snare" });
        logicalInstruments.push_back (dummy);
        rebuildZoneButtons();
        return;
    }

    const auto parseParent = [] (const juce::String& rawName) -> std::pair<juce::String, juce::String>
    {
        const auto trimmed = rawName.trim();
        if (trimmed.isEmpty())
            return { "Drum", "Drum" };

        // 1. Specific multi-zone snare pieces
        if (trimmed.equalsIgnoreCase ("RimClick") || trimmed.equalsIgnoreCase ("CrossStick")
            || trimmed.equalsIgnoreCase ("Rimshot") || trimmed.equalsIgnoreCase ("SnareRimShot")
            || trimmed.equalsIgnoreCase ("SnareRim") || trimmed.equalsIgnoreCase ("SnareRest")
            || trimmed.startsWithIgnoreCase ("Snare"))
        {
            juce::String zone = trimmed;
            if (zone.startsWithIgnoreCase ("Snare") && zone.length() > 5)
                zone = zone.substring (5);
            return { "Snare", zone };
        }

        // 2. Toms with numbers and zones (Tom1Head, Tom1Rim, Tom2Head, Tom2Rim, FTomHead, FTomRim)
        if (trimmed.startsWithIgnoreCase ("Tom1"))
        {
            juce::String zone = trimmed.length() > 4 ? trimmed.substring (4) : "Tom1";
            return { "Tom 1", zone };
        }
        if (trimmed.startsWithIgnoreCase ("Tom2"))
        {
            juce::String zone = trimmed.length() > 4 ? trimmed.substring (4) : "Tom2";
            return { "Tom 2", zone };
        }
        if (trimmed.startsWithIgnoreCase ("FTom") || trimmed.startsWithIgnoreCase ("FloorTom"))
        {
            juce::String zone = trimmed;
            if (zone.startsWithIgnoreCase ("FloorTom") && zone.length() > 8)
                zone = zone.substring (8);
            else if (zone.startsWithIgnoreCase ("FTom") && zone.length() > 4)
                zone = zone.substring (4);
            return { "Floor Tom", zone };
        }

        // 3. Crash 1 and Crash 2 with zones (Crash1Edge, Crash1Bow, Crash1Choke, Crash2Edge, etc.)
        if (trimmed.startsWithIgnoreCase ("Crash1"))
        {
            juce::String zone = trimmed.length() > 6 ? trimmed.substring (6) : "Crash1";
            return { "Crash 1", zone };
        }
        if (trimmed.startsWithIgnoreCase ("Crash2"))
        {
            juce::String zone = trimmed.length() > 6 ? trimmed.substring (6) : "Crash2";
            return { "Crash 2", zone };
        }

        // 4. Ride with zones (RideBow, RideBell, RideEdge, RideChoke, RideRBell, RideR)
        if (trimmed.startsWithIgnoreCase ("RideR") && trimmed.length() > 5)
            return { "RideR", trimmed.substring (5) };
        if (trimmed.startsWithIgnoreCase ("Ride") && trimmed.length() > 4 && ! trimmed.equalsIgnoreCase ("RideR"))
            return { "Ride", trimmed.substring (4) };

        // 5. Hihat with zones (HihatClosed, HihatSemiOpen, HihatOpen, HihatPedal)
        if (trimmed.startsWithIgnoreCase ("Hihat") && trimmed.length() > 5)
            return { "Hihat", trimmed.substring (5) };

        // 6. If name ends with digit(s) (e.g. Tom1, Tom2, FTom1, FTom2) -> individual instrument
        const juce::juce_wchar lastChar = trimmed[trimmed.length() - 1];
        if (juce::CharacterFunctions::isDigit (lastChar))
            return { trimmed, trimmed };

        // 7. If starts with known L/R prefix (e.g. ChinaL, ChinaR, CrashL, CrashR, KDrumL, KDrumR, RideR, SplashL, SplashR)
        const char* knownLR[] = {
            "ChinaL", "ChinaR", "CrashL", "CrashR", "KDrumL", "KDrumR",
            "RideL", "RideR", "SplashL", "SplashR", "KickL", "KickR", nullptr
        };
        for (int i = 0; knownLR[i] != nullptr; ++i)
        {
            if (trimmed.startsWithIgnoreCase (knownLR[i]))
            {
                juce::String parent = knownLR[i];
                juce::String zone = trimmed;
                if (zone.length() > parent.length())
                    zone = zone.substring (parent.length());
                return { parent, zone };
            }
        }

        // 8. Known base roots with multi-zones
        const char* knownRoots[] = {
            "Snare", "Hihat", "HiHat", "Ride", "Kick", "KDrum",
            "Crash", "China", "Splash", "Cowbell", "Tambourine", nullptr
        };
        for (int i = 0; knownRoots[i] != nullptr; ++i)
        {
            if (trimmed.startsWithIgnoreCase (knownRoots[i]))
            {
                juce::String parent = knownRoots[i];
                if (parent.equalsIgnoreCase ("hihat"))
                    parent = "Hihat";
                juce::String zone = trimmed;
                if (zone.length() > parent.length())
                    zone = zone.substring (parent.length());
                return { parent, zone };
            }
        }

        // 9. Default: parent is trimmed
        return { trimmed, trimmed };
    };

    for (int i = 0; i < count; ++i)
    {
        const auto rawName = sampler.instrumentName (i);
        auto [parentName, zoneName] = parseParent (rawName);

        int matchIdx = -1;
        for (size_t k = 0; k < logicalInstruments.size(); ++k)
        {
            if (logicalInstruments[k].name.equalsIgnoreCase (parentName))
            {
                matchIdx = static_cast<int> (k);
                break;
            }
        }

        if (matchIdx < 0)
        {
            LogicalInstrument newInst;
            newInst.name = parentName;
            newInst.piece = DrumKitView::pieceForName (rawName);
            if (newInst.piece == KitPiece::count)
                newInst.piece = DrumKitView::pieceForName (parentName);
            newInst.outputBus = sampler.getInstrumentBus (i);
            newInst.zones.push_back ({ i, zoneName });
            logicalInstruments.push_back (newInst);
        }
        else
        {
            logicalInstruments[matchIdx].zones.push_back ({ i, zoneName });
            if (logicalInstruments[matchIdx].piece == KitPiece::count)
            {
                auto p = DrumKitView::pieceForName (rawName);
                if (p != KitPiece::count)
                    logicalInstruments[matchIdx].piece = p;
            }
        }
    }

    selectedLogicalIndex = juce::jlimit (0, static_cast<int> (logicalInstruments.size()) - 1, selectedLogicalIndex);
    rebuildZoneButtons();
}

void Editor::rebuildZoneButtons()
{
    for (auto& btn : zoneButtons)
        removeChildComponent (btn.get());

    zoneButtons.clear();

    if (logicalInstruments.empty() || selectedLogicalIndex < 0 || selectedLogicalIndex >= static_cast<int> (logicalInstruments.size()))
        return;

    const auto& inst = logicalInstruments[selectedLogicalIndex];
    for (size_t i = 0; i < inst.zones.size(); ++i)
    {
        const bool active = (static_cast<int> (i) == inst.selectedZoneIndex);
        auto btn = std::make_unique<ZoneButton> (inst.zones[i].zoneName, active);
        const int zoneIdx = static_cast<int> (i);

        btn->onClick = [this, zoneIdx]
        {
            selectZone (selectedLogicalIndex, zoneIdx, true);
        };

        addAndMakeVisible (*btn);
        zoneButtons.push_back (std::move (btn));
    }

    resized();
}

int Editor::getActiveSamplerIndex() const
{
    if (logicalInstruments.empty() || selectedLogicalIndex < 0 || selectedLogicalIndex >= static_cast<int> (logicalInstruments.size()))
        return -1;

    const auto& inst = logicalInstruments[selectedLogicalIndex];
    if (inst.zones.empty() || inst.selectedZoneIndex < 0 || inst.selectedZoneIndex >= static_cast<int> (inst.zones.size()))
        return -1;

    return inst.zones[inst.selectedZoneIndex].samplerIndex;
}

void Editor::selectLogicalInstrument (int index)
{
    if (logicalInstruments.empty())
        return;

    selectedLogicalIndex = juce::jlimit (0, static_cast<int> (logicalInstruments.size()) - 1, index);
    instSelectorButton.setInstrumentName (logicalInstruments[selectedLogicalIndex].name);
    outBusButton.setBusIndex (logicalInstruments[selectedLogicalIndex].outputBus);

    rebuildZoneButtons();
    selectZone (selectedLogicalIndex, logicalInstruments[selectedLogicalIndex].selectedZoneIndex, false);
}

void Editor::selectZone (int logicalIndex, int zoneIndex, bool triggerPreview)
{
    if (logicalInstruments.empty())
        return;

    selectedLogicalIndex = juce::jlimit (0, static_cast<int> (logicalInstruments.size()) - 1, logicalIndex);
    auto& inst = logicalInstruments[selectedLogicalIndex];

    if (! inst.zones.empty())
        inst.selectedZoneIndex = juce::jlimit (0, static_cast<int> (inst.zones.size()) - 1, zoneIndex);
    else
        inst.selectedZoneIndex = 0;

    for (size_t i = 0; i < zoneButtons.size(); ++i)
        zoneButtons[i]->setActive (static_cast<int> (i) == inst.selectedZoneIndex);

    bindInstrumentKnobs();
    updateInstrumentDisplay();

    const int samplerIdx = getActiveSamplerIndex();
    if (triggerPreview && samplerIdx >= 0)
    {
        const auto note = processor.getMidiMap().noteForInstrument (samplerIdx);
        const int midiNote = (note >= 0) ? note : 36;
        processor.getSampler().noteOn (samplerIdx, midiNote, 1.0f,
                                       processor.getSampler().currentSamplePosition());
        scope.hitReceived (inst.name, inst.piece, midiNote, 1.0f);

        if (inst.piece != KitPiece::count)
            kitView.triggerHitForPiece (inst.piece, 1.0f);
    }

    repaint();
}

void Editor::selectInstrument (int samplerIndex)
{
    for (size_t l = 0; l < logicalInstruments.size(); ++l)
    {
        for (size_t z = 0; z < logicalInstruments[l].zones.size(); ++z)
        {
            if (logicalInstruments[l].zones[z].samplerIndex == samplerIndex)
            {
                selectLogicalInstrument (static_cast<int> (l));
                selectZone (static_cast<int> (l), static_cast<int> (z), false);
                return;
            }
        }
    }
}

void Editor::bindInstrumentKnobs()
{
    const int samplerIdx = getActiveSamplerIndex();
    if (samplerIdx < 0)
        return;

    const auto prefix = "inst" + juce::String (samplerIdx) + ".";

    if (auto* p = processor.findParameter (prefix + "level"))
        instVolumeKnob->attachToParameter (p);

    if (auto* p = processor.findParameter (prefix + "pan"))
        instPanKnob->attachToParameter (p);

    if (auto* p = processor.findParameter (prefix + "pitch"))
        instPitchKnob->attachToParameter (p);

    if (auto* p = processor.findParameter (prefix + "dynamics"))
        instDynamicsKnob->attachToParameter (p);
}

void Editor::updateInstrumentDisplay()
{
    if (logicalInstruments.empty() || selectedLogicalIndex < 0 || selectedLogicalIndex >= static_cast<int> (logicalInstruments.size()))
        return;

    const auto& inst = logicalInstruments[selectedLogicalIndex];
    instSelectorButton.setInstrumentName (inst.name);
    outBusButton.setBusIndex (inst.outputBus);

    const int samplerIdx = getActiveSamplerIndex();
    if (samplerIdx >= 0)
    {
        const auto params = processor.getSampler().getInstrumentParams (samplerIdx);
        instCurveButton.setCurve (params.velocityCurve);
    }

    const bool isLearningThis = (learningInstrument >= 0 && learningInstrument == samplerIdx);
    instLearnButton.setLearning (isLearningThis);
}

void Editor::armLearn (int samplerIndex)
{
    if (samplerIndex < 0)
        return;

    auto& map = processor.getMidiMap();
    if (learningInstrument == samplerIndex)
    {
        map.cancelLearn();
        learningInstrument = -1;
        statusText = "MIDI learn cancelled";
    }
    else
    {
        learningInstrument = samplerIndex;
        map.beginLearn ("note" + std::to_string (samplerIndex));
        statusText = "MIDI learn armed - hit your drum pad";
    }

    updateInstrumentDisplay();
    repaint();
}

juce::String Editor::noteLabelFor (int samplerIndex) const
{
    if (samplerIndex < 0)
        return "--";

    const auto note = processor.getMidiMap().noteForInstrument (samplerIndex);
    if (note < 0)
        return "--";

    return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
}

void Editor::kitChanged()
{
    kitErrorText = processor.getLastKitError();

    juce::String title = processor.getSampler().kitTitleString();
    if (title.isEmpty())
    {
        const auto p = processor.getCurrentKitPath();
        if (p.isNotEmpty())
            title = juce::File (p).getParentDirectory().getFileName();
        else
            title = "GLORP DEMO KIT";
    }
    kitSelectorButton.setKitName (title);

    buildLogicalInstruments();

    std::vector<juce::String> names;
    for (int i = 0; i < processor.getSampler().numInstruments(); ++i)
        names.push_back (processor.getSampler().instrumentName (i));

    kitView.setInstrumentNames (names);

    if (! logicalInstruments.empty())
    {
        selectedLogicalIndex = juce::jlimit (0, static_cast<int> (logicalInstruments.size()) - 1, selectedLogicalIndex);
        selectZone (selectedLogicalIndex, 0, false);
    }

    statusText = processor.getSampler().isKitLoaded()
        ? juce::String (processor.getSampler().numInstruments()) + " instruments, "
            + juce::String (processor.getSampler().usedBusCount()) + " output buses"
        : "Glorp Demo Kit ready";

    repaint();
}

void Editor::parametersChanged()
{
    updateInstrumentDisplay();
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
        const auto instName = processor.getSampler().instrumentName (event.instrumentIndex);
        kitView.triggerHit (instName, event.velocity);

        const auto piece = DrumKitView::pieceForName (instName);
        scope.hitReceived (instName, piece, event.note, event.velocity);

        anyHit = true;
    }

    if (anyHit)
        repaint();

    const float cc4 = processor.getHiHatCC4();
    static float lastReportedCC4 = -1.0f;
    if (std::abs (cc4 - lastReportedCC4) > 0.005f)
    {
        lastReportedCC4 = cc4;
        scope.controllerReceived (4, static_cast<int> (cc4 * 127.0f));
    }

    processor.drainControlQueue();

    static bool wasLearning = false;
    const auto isLearning = processor.getMidiMap().isLearning();

    if (wasLearning && ! isLearning)
    {
        learningInstrument = -1;
        statusText = "MIDI learned";
        updateInstrumentDisplay();
        repaint();
    }

    wasLearning = isLearning;
}

//==============================================================================
void Editor::resized()
{
    const auto b = getLocalBounds();
    const int w = b.getWidth();
    const int h = b.getHeight();

    // 1. Header menu buttons placed in top right section
    const int menuY = 16;
    const int menuH = 30;
    const int fileW = 60;
    const int presetsW = 92;
    const int midiW = 60;
    const int settingsW = 96;
    const int gap = 20;

    const int totalMenuW = fileW + gap + presetsW + gap + midiW + gap + settingsW;
    int curX = w - 36 - totalMenuW;
    fileButton.setBounds (curX, menuY, fileW, menuH);
    curX += fileW + gap;
    presetsButton.setBounds (curX, menuY, presetsW, menuH);
    curX += presetsW + gap;
    midiButton.setBounds (curX, menuY, midiW, menuH);
    curX += midiW + gap;
    settingsButton.setBounds (curX, menuY, settingsW, menuH);

    // 2. Bottom strip
    const int bottomH = 135;
    const int bottomY = h - bottomH - 12;

    // Master Volume in bottom right
    const int masterW = 110;
    const int masterX = w - masterW - 24;
    masterVolumeKnob->setBounds (masterX, bottomY + 16, masterW, bottomH - 20);

    // MIDI Activity scope spanning bottom
    const int scopeX = 24;
    const int scopeW = masterX - scopeX - 18;
    scope.setBounds (scopeX, bottomY, scopeW, bottomH);

    // 3. Left Panel ("INSTRUMENT SETTINGS")
    const int panelX = 24;
    const int panelY = 74;
    const int panelW = 196;

    // Inside Left Panel:
    // Selector button
    instSelectorButton.setBounds (panelX + 14, panelY + 52, panelW - 28, 28);

    // Output bus button
    outBusButton.setBounds (panelX + 14, panelY + 84, panelW - 28, 24);

    // Zone buttons layout (2 columns grid)
    const int zoneY = panelY + 114;
    const int zoneW = (panelW - 28 - 6) / 2;
    const int zoneH = 24;
    const int zoneGap = 4;

    int currentZoneBottom = zoneY;

    if (! zoneButtons.empty())
    {
        if (zoneButtons.size() == 1)
        {
            zoneButtons[0]->setBounds (panelX + 14, zoneY, panelW - 28, zoneH);
            currentZoneBottom = zoneY + zoneH;
        }
        else
        {
            for (size_t i = 0; i < zoneButtons.size(); ++i)
            {
                const int row = static_cast<int> (i / 2);
                const int col = static_cast<int> (i % 2);
                const int bx = panelX + 14 + col * (zoneW + 6);
                const int by = zoneY + row * (zoneH + zoneGap);
                zoneButtons[i]->setBounds (bx, by, zoneW, zoneH);
                currentZoneBottom = std::max (currentZoneBottom, by + zoneH);
            }
        }
    }
    else
    {
        currentZoneBottom = zoneY + zoneH;
    }

    // 4 Knobs (2x2 Grid)
    const int kSize = 68;
    const int kPadX = 16;
    const int kCol1 = panelX + kPadX;
    const int kCol2 = panelX + panelW - kPadX - kSize;
    const int kRow1 = currentZoneBottom + 10;
    const int kRow2 = kRow1 + kSize + 14;

    instVolumeKnob->setBounds (kCol1, kRow1, kSize, kSize + 14);
    instPanKnob->setBounds (kCol2, kRow1, kSize, kSize + 14);
    instPitchKnob->setBounds (kCol1, kRow2, kSize, kSize + 14);
    instDynamicsKnob->setBounds (kCol2, kRow2, kSize, kSize + 14);

    // Velocity Curve row
    const int curveY = kRow2 + kSize + 14;
    const int curveH = 24;
    instCurveButton.setBounds (panelX + 14, curveY, panelW - 28, curveH);

    // Bottom row: NOTE label on left, LEARN button on right
    const int noteRowY = curveY + curveH + 8;
    const int learnW = 68;
    const int learnH = 26;
    instLearnButton.setBounds (panelX + panelW - 14 - learnW, noteRowY, learnW, learnH);

    leftPanelHeight = static_cast<float> ((noteRowY + learnH + 12) - panelY);

    // 4. Central drum kit view with kit dropdown placed above it
    const int kitX = panelX + panelW + 16;
    const int kitW = w - kitX - 20;

    const int kitDropW = std::min (340, kitW - 20);
    const int kitDropX = kitX + (kitW - kitDropW) / 2;
    kitSelectorButton.setBounds (kitDropX, 52, kitDropW, 28);

    const int kitY = 84;
    const int kitH = bottomY - kitY - 8;
    kitView.setBounds (kitX, kitY, kitW, kitH);

    if (edrumWizard != nullptr)
        edrumWizard->setBounds (getLocalBounds());
}

//==============================================================================
void Editor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // 1. Paper background
    g.fillAll (theme.background);

    // 2. Outer window sketchy border
    drawSketchRect (g, b.reduced (3.0f), theme.panelEdge, 2.0f, 1);

    // 3. Top-left: Glorp Drummer Logo & Stacked "GLORP DRUMS" Title
    if (logo != nullptr)
    {
        auto logoTarget = juce::Rectangle<float> (24.0f, 10.0f, 62.0f, 48.0f);
        logo->drawWithin (g, logoTarget, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);
    }

    g.setFont (displayFont (24.0f));
    g.setColour (theme.ink);
    g.drawText ("GLORP", juce::Rectangle<float> (92.0f, 8.0f, 180.0f, 24.0f),
                juce::Justification::centredLeft, false);
    g.drawText ("DRUMS", juce::Rectangle<float> (92.0f, 30.0f, 180.0f, 24.0f),
                juce::Justification::centredLeft, false);

    // 4. Left Panel Card ("INSTRUMENT SETTINGS")
    const auto panelRect = juce::Rectangle<float> (24.0f, 74.0f, 196.0f, leftPanelHeight);
    g.setColour (theme.panel);
    g.fillRoundedRectangle (panelRect, 6.0f);
    drawSketchRect (g, panelRect, theme.panelEdge, 1.8f, 3);

    // Title inside card
    g.setFont (displayFont (18.0f));
    g.setColour (theme.ink);
    g.drawText ("INSTRUMENT", juce::Rectangle<float> (panelRect.getX(), panelRect.getY() + 10.0f, panelRect.getWidth(), 18.0f),
                juce::Justification::centred, false);
    g.drawText ("SETTINGS", juce::Rectangle<float> (panelRect.getX(), panelRect.getY() + 28.0f, panelRect.getWidth(), 18.0f),
                juce::Justification::centred, false);

    // Bottom note label in Left Panel
    if (instLearnButton.isVisible())
    {
        const float noteRowX = 24.0f + 14.0f;
        const float noteRowY = static_cast<float> (instLearnButton.getY());
        const float noteRowH = static_cast<float> (instLearnButton.getHeight());
        const float noteMaxW = static_cast<float> (instLearnButton.getX()) - noteRowX - 4.0f;

        g.setFont (displayFont (15.0f));
        g.setColour (juce::Colour (0xff111215));
        const juce::String notePrefix = "NOTE: ";
        const float pfxW = displayFont (15.0f).getStringWidthFloat (notePrefix);
        g.drawText (notePrefix, juce::Rectangle<float> (noteRowX, noteRowY, pfxW, noteRowH),
                    juce::Justification::centredLeft, false);

        const int samplerIdx = getActiveSamplerIndex();
        const juce::String noteStr = (learningInstrument == samplerIdx && samplerIdx >= 0)
            ? "???"
            : noteLabelFor (samplerIdx);

        g.setFont (displayFont (17.5f));
        g.setColour (juce::Colour (0xffea8c16));
        g.drawText (noteStr, juce::Rectangle<float> (noteRowX + pfxW, noteRowY, noteMaxW - pfxW, noteRowH),
                    juce::Justification::centredLeft, false);
    }

    // 5. "MASTER" title above Master Volume Knob
    if (masterVolumeKnob != nullptr)
    {
        const auto mx = static_cast<float> (masterVolumeKnob->getX());
        const auto my = static_cast<float> (masterVolumeKnob->getY());
        const auto mw = static_cast<float> (masterVolumeKnob->getWidth());

        g.setFont (displayFont (18.0f));
        g.setColour (theme.ink);
        g.drawText ("MASTER", juce::Rectangle<float> (mx, my - 20.0f, mw, 18.0f),
                    juce::Justification::centred, false);
    }
}

} // namespace glorp
