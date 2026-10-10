/*
    Editor.h - GlorpDrums hand-drawn comic UI window.
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
               public juce::Timer,
               private PluginProcessor::Listener
{
public:
    explicit Editor (PluginProcessor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    void selectInstrument (int index);

private:
    void kitChanged() override;
    void parametersChanged() override;

    void buildHeaderMenu();
    void buildControls();
    void rebuildCallouts();
    void updateCalloutLabels();
    void bindInstrumentKnobs();

    juce::String noteLabelFor (int instrument) const;
    juce::String sampleLabelFor (int instrument) const;

    juce::PopupMenu buildFileMenu();
    juce::PopupMenu buildPresetsMenu();
    juce::PopupMenu buildMidiMenu();
    juce::PopupMenu buildSettingsMenu();

    void showSampleMenu (int instrument, const juce::Point<int>& anchor);
    void armLearn (int instrument);
    void toggleAuto (int instrument);

    void drawLeaderArrows (juce::Graphics& g);

    PluginProcessor& processor;

    // Header buttons (hand-drawn text buttons)
    class MenuButton : public juce::Button
    {
    public:
        explicit MenuButton (const juce::String& name) : juce::Button (name) {}
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    };

    MenuButton fileButton { "FILE" };
    MenuButton presetsButton { "PRESETS ▼" };
    MenuButton midiButton { "MIDI" };
    MenuButton settingsButton { "SETTINGS" };

    juce::Component kitArea;
    DrumKitView kitView;
    Scope scope;

    std::unique_ptr<juce::Drawable> logo;

    // Bottom-Left 2x2 Knobs (Master + Selected Instrument)
    std::unique_ptr<Knob> masterVolumeKnob;
    std::unique_ptr<Knob> instPanKnob;
    std::unique_ptr<Knob> instPitchKnob;
    std::unique_ptr<Knob> instLevelKnob;

    // Bottom-Right Controls
    std::unique_ptr<Knob> attackKnob;
    std::unique_ptr<Knob> decayKnob;
    std::unique_ptr<juce::Slider> fader1;
    std::unique_ptr<juce::Slider> fader2;

    std::vector<std::unique_ptr<CalloutBox>> callouts;

    int selectedInstrumentIndex = 0;
    int learningInstrument = -1;

    juce::String statusText;
    juce::String kitErrorText;

    Theme theme = Theme::sketch();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace glorp
