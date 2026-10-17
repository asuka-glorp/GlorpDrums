/*
    Editor.h - GlorpDrums hand-drawn comic UI window.
*/

#pragma once

#include "DrumKitView.h"
#include "EDrumWizard.h"
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
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void selectInstrument (int samplerIndex);

    struct ZoneInfo
    {
        int samplerIndex = 0;
        juce::String zoneName;
    };

    struct LogicalInstrument
    {
        juce::String name;
        KitPiece piece { KitPiece::count };
        std::vector<ZoneInfo> zones;
        int selectedZoneIndex = 0;
        int outputBus = 0;
    };

    const std::vector<LogicalInstrument>& getLogicalInstruments() const noexcept { return logicalInstruments; }
    void selectLogicalInstrument (int index);
    void selectZone (int logicalIndex, int zoneIndex, bool triggerPreview = false);
    int getActiveSamplerIndex() const;

private:
    void kitChanged() override;
    void parametersChanged() override;

    void buildHeaderMenu();
    void buildControls();
    void buildLogicalInstruments();
    void rebuildZoneButtons();

    void bindInstrumentKnobs();
    void updateInstrumentDisplay();

    juce::String noteLabelFor (int instrument) const;

    juce::PopupMenu buildFileMenu();
    juce::PopupMenu buildPresetsMenu();
    juce::PopupMenu buildMidiMenu();
    juce::PopupMenu buildSettingsMenu();

    void armLearn (int samplerIndex);
    void promptLoadKitFile();
    void openEDrumWizard();

    PluginProcessor& processor;

    // Header buttons (hand-drawn text buttons)
    class MenuButton : public juce::Button
    {
    public:
        explicit MenuButton (const juce::String& name) : juce::Button (name) {}
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    };

    class InstrumentSelectorButton : public juce::Button
    {
    public:
        InstrumentSelectorButton() : juce::Button ("instSelect") {}
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
        void setInstrumentName (const juce::String& name) { currentName = name; repaint(); }
        const juce::String& getInstrumentName() const noexcept { return currentName; }
    private:
        juce::String currentName { "SNARE" };
    };

    class KitSelectorButton : public juce::Button
    {
    public:
        KitSelectorButton() : juce::Button ("kitSelect") {}
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
        void setKitName (const juce::String& name) { currentName = name; repaint(); }
        const juce::String& getKitName() const noexcept { return currentName; }
    private:
        juce::String currentName { "NO KIT LOADED" };
    };

    class OutputBusButton : public juce::Button
    {
    public:
        OutputBusButton() : juce::Button ("outBus") {}
        void setBusIndex (int busIdx) { currentBus = busIdx; repaint(); }
        int getBusIndex() const noexcept { return currentBus; }
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        int currentBus = 0;
    };

    class ZoneButton : public juce::Button
    {
    public:
        ZoneButton (const juce::String& name, bool active);
        void setActive (bool active);
        bool getActive() const noexcept { return isActive; }
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        bool isActive = false;
    };

    class LearnButton : public juce::Button
    {
    public:
        LearnButton() : juce::Button ("LEARN") {}
        void setLearning (bool learning);
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        bool isLearning_ = false;
    };

    class CurveButton : public juce::Button
    {
    public:
        CurveButton() : juce::Button ("CURVE") {}
        void setCurve (VelocityCurve c) { currentCurve = c; repaint(); }
        VelocityCurve getCurve() const noexcept { return currentCurve; }
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        VelocityCurve currentCurve = VelocityCurve::linear;
    };

    MenuButton fileButton { "FILE" };
    MenuButton presetsButton { "PRESETS" };
    MenuButton midiButton { "MIDI" };
    MenuButton settingsButton { "SETTINGS" };

    KitSelectorButton kitSelectorButton;
    DrumKitView kitView;
    Scope scope;

    std::unique_ptr<juce::Drawable> logo;

    // Left Panel: "INSTRUMENT SETTINGS"
    InstrumentSelectorButton instSelectorButton;
    OutputBusButton outBusButton;

    std::vector<std::unique_ptr<ZoneButton>> zoneButtons;

    std::unique_ptr<Knob> instVolumeKnob;
    std::unique_ptr<Knob> instPanKnob;
    std::unique_ptr<Knob> instPitchKnob;
    std::unique_ptr<Knob> instDynamicsKnob;

    CurveButton instCurveButton;
    LearnButton instLearnButton;

    // E-Drum Setup Wizard Overlay
    std::unique_ptr<EDrumWizard> edrumWizard;

    // Bottom-Right: Master Volume Knob
    std::unique_ptr<Knob> masterVolumeKnob;

    std::vector<LogicalInstrument> logicalInstruments;
    int selectedLogicalIndex = 0;
    int learningInstrument = -1;

    float leftPanelHeight = 440.0f;

    juce::String statusText;
    juce::String kitErrorText;

    Theme theme = Theme::sketch();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};

} // namespace glorp
