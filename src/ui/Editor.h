/*
    Editor.h - the GlorpDrums window.

    Layout follows the mockup: menu bar across the top, the drum kit in the middle
    with callout boxes tethered to it, global knobs on the left, per-instrument
    controls down the right, and the MIDI activity scope along the bottom.
*/

#pragma once

#include "CalloutBox.h"
#include "DrumKitView.h"
#include "Knob.h"
#include "PluginProcessor.h"
#include "Scope.h"

#include <memory>
#include <vector>

namespace glorp
{

class Editor : public juce::Component,
               public juce::MenuBarModel,
               public juce::Timer,
               private PluginProcessor::Listener
{
public:
    explicit Editor (PluginProcessor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    // -- juce::MenuBarModel ------------------------------------------------
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override {}

private:
    // -- PluginProcessor::Listener -----------------------------------------
    void kitChanged() override;
    void parametersChanged() override {}

    // -- construction ------------------------------------------------------
    void buildMenuBar();
    void buildGlobalControls();
    void buildInstrumentControls();
    void rebuildCallouts();

    void updateCalloutLabels();
    juce::String noteLabelFor (int instrument) const;
    juce::String sampleLabelFor (int instrument) const;

    void showKitMenu();
    void showMapPresetMenu();
    void showSampleMenu (int instrument, const juce::Point<int>& anchor);
    void armLearn (int instrument);
    void toggleAuto (int instrument);
    void layoutCallouts (int rightColumnWidth);
    juce::String themeName() const;

    PluginProcessor& processor;

    juce::MenuBarComponent menuBar;
    juce::Component kitArea;
    DrumKitView kitView;
    Scope scope;

    std::unique_ptr<juce::Drawable> logo;
    juce::Image cachedBackground;

    std::vector<std::unique_ptr<Knob>> globalKnobs;
    std::vector<std::unique_ptr<Knob>> instrumentKnobs;   ///< 3 per instrument
    std::vector<std::unique_ptr<CalloutBox>> callouts;

    juce::StringArray menuNames { "FILE", "PRESETS", "MIDI", "SETTINGS" };
    juce::String statusText;
    juce::String kitErrorText;

    int learningInstrument = -1;

    Theme theme = Theme::sketch();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace glorp
