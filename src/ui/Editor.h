/*
    Editor.h - GlorpDrums hand-drawn comic UI window.
*/

#pragma once

#include "DrumKitView.h"
#include "EDrumWizard.h"
#include "Knob.h"
#include "MixerView.h"
#include "PluginProcessor.h"
#include "RecordPracticeView.h"
#include "Scope.h"

#include <juce_opengl/juce_opengl.h>

#include <memory>
#include <vector>

namespace glorp
{

class Editor : public juce::Component,
               public juce::Timer,
               public juce::FileDragAndDropTarget,
               private PluginProcessor::Listener
{
public:
    explicit Editor (PluginProcessor&);
    ~Editor() override;

    enum class EditorView
    {
        kitView,
        mixerView,
        practiceView
    };

    void setViewMode (EditorView mode);
    EditorView getViewMode() const noexcept { return currentView; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void selectInstrument (int samplerIndex);
    void openEDrumWizard();

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
    DrumKitView& getKitView() noexcept { return kitView; }
    Scope& getScope() noexcept { return scope; }
    MixerView* getMixerView() noexcept { return mixerView.get(); }
    RecordPracticeView* getPracticeView() noexcept { return practiceView.get(); }
    void applyTheme (const Theme& newTheme);

    void setOpenGlEnabled (bool enabled);
    bool isOpenGlEnabled() const noexcept;
    void parentHierarchyChanged() override;

private:
    void kitChanged() override;
    void parametersChanged() override;

    void updateOpenGlAttachment();
    void rebuildLogoCache();

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

    PluginProcessor& processor;

    // Header buttons (hand-drawn text buttons)
    class MenuButton : public juce::Button
    {
    public:
        explicit MenuButton (const juce::String& name) : juce::Button (name) {}
        void setActive (bool a) { active = a; repaint(); }
        bool getActive() const noexcept { return active; }
        void setTheme (const Theme& t) { theme = t; repaint(); }
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        bool active = false;
        Theme theme = Theme::sketch();
    };

    class InstrumentSelectorButton : public juce::Button
    {
    public:
        InstrumentSelectorButton() : juce::Button ("instSelect") {}
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
        void setInstrumentName (const juce::String& name) { currentName = name; repaint(); }
        const juce::String& getInstrumentName() const noexcept { return currentName; }
        void setTheme (const Theme& t) { theme = t; repaint(); }
    private:
        juce::String currentName { "SNARE" };
        Theme theme = Theme::sketch();
    };

    class KitSelectorButton : public juce::Button
    {
    public:
        KitSelectorButton() : juce::Button ("kitSelect") {}
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
        void setKitName (const juce::String& name) { currentName = name; repaint(); }
        const juce::String& getKitName() const noexcept { return currentName; }
        void setTheme (const Theme& t) { theme = t; repaint(); }
    private:
        juce::String currentName { "NO KIT LOADED" };
        Theme theme = Theme::sketch();
    };

    class OutputBusButton : public juce::Button
    {
    public:
        OutputBusButton() : juce::Button ("outBus") {}
        void setBusIndex (int busIdx) { currentBus = busIdx; repaint(); }
        int getBusIndex() const noexcept { return currentBus; }
        void setTheme (const Theme& t) { theme = t; repaint(); }
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        int currentBus = 0;
        Theme theme = Theme::sketch();
    };

    class ZoneButton : public juce::Button
    {
    public:
        ZoneButton (const juce::String& name, bool active);
        void setActive (bool active);
        bool getActive() const noexcept { return isActive; }
        void setTheme (const Theme& t) { theme = t; repaint(); }
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        bool isActive = false;
        Theme theme = Theme::sketch();
    };

    class LearnButton : public juce::Button
    {
    public:
        LearnButton() : juce::Button ("LEARN") {}
        void setLearning (bool learning);
        void setTheme (const Theme& t) { theme = t; repaint(); }
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        bool isLearning_ = false;
        Theme theme = Theme::sketch();
    };

    class CurveButton : public juce::Button
    {
    public:
        CurveButton() : juce::Button ("CURVE") {}
        void setCurve (VelocityCurve c) { currentCurve = c; repaint(); }
        VelocityCurve getCurve() const noexcept { return currentCurve; }
        void setTheme (const Theme& t) { theme = t; repaint(); }
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        VelocityCurve currentCurve = VelocityCurve::linear;
        Theme theme = Theme::sketch();
    };

    class VelocityCurvePreview : public juce::Component
    {
    public:
        VelocityCurvePreview();
        void setCurve (VelocityCurve c) { currentCurve = c; repaint(); }
        VelocityCurve getCurve() const noexcept { return currentCurve; }
        void setTheme (const Theme& t) { theme = t; repaint(); }

        std::function<void()> onClick;

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;

    private:
        VelocityCurve currentCurve = VelocityCurve::linear;
        Theme theme = Theme::sketch();
    };

    MenuButton viewToggleButton { "MIXER VIEW" };
    MenuButton practiceToggleButton { "PRACTICE" };
    MenuButton fileButton { "FILE" };
    MenuButton presetsButton { "PRESETS" };
    MenuButton midiButton { "MIDI" };
    MenuButton settingsButton { "SETTINGS" };

    KitSelectorButton kitSelectorButton;
    DrumKitView kitView;
    Scope scope;

    std::unique_ptr<MixerView> mixerView;
    std::unique_ptr<RecordPracticeView> practiceView;
    EditorView currentView = EditorView::kitView;

    std::unique_ptr<juce::Drawable> logo;
    juce::Image cachedLogoImage;
    juce::OpenGLContext openGlContext;

    // Left Panel: "INSTRUMENT SETTINGS"
    InstrumentSelectorButton instSelectorButton;
    OutputBusButton outBusButton;

    std::vector<std::unique_ptr<ZoneButton>> zoneButtons;

    std::unique_ptr<Knob> instVolumeKnob;
    std::unique_ptr<Knob> instPanKnob;
    std::unique_ptr<Knob> instPitchKnob;
    std::unique_ptr<Knob> instDynamicsKnob;

    CurveButton instCurveButton;
    VelocityCurvePreview instCurvePreview;
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
