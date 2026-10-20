/*
    MixerView.h - Full-featured per-mic drum mixer console and DSP rack for GlorpDrums.
*/

#pragma once

#include "ChannelFX.h"
#include "Knob.h"
#include "PluginProcessor.h"
#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace glorp
{

class MixerView : public juce::Component,
                  private juce::Timer
{
public:
    explicit MixerView (PluginProcessor& processor);
    ~MixerView() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

    void refreshChannels();
    void openChannelFX (int channelIndex);
    void openMasterFX();
    void openBleedMatrix (int highlightMic = -1);
    void toggleBleedMatrix();
    void setBleedMatrixOpen (bool open);
    bool isBleedMatrixOpen() const noexcept { return bleedMatrixOpen; }
    void closePopups();
    void setTheme (const Theme& newTheme);

private:
    void timerCallback() override;

    PluginProcessor& processor;
    Theme theme = Theme::sketch();
    bool bleedMatrixOpen = false;

    //==========================================================================
    // UI Helpers & Custom Buttons
    //==========================================================================
    class HandButton : public juce::Button
    {
    public:
        HandButton (const juce::String& name, bool isToggle = false);
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
        void setActive (bool a) { active = a; repaint(); }
        bool getActive() const noexcept { return active; }
        void setTheme (const Theme& t) { theme = t; repaint(); }
        juce::Colour activeColour = juce::Colour (0xfff1c40f);
    private:
        bool isToggle_ = false;
        bool active = false;
        Theme theme = Theme::sketch();
    };

    //==========================================================================
    // Channel FX Drawer Popup (EQ, Comp, Saturation, Transient)
    //==========================================================================
    class ChannelFXPopup : public juce::Component
    {
    public:
        ChannelFXPopup (MixerView& owner, PluginProcessor& processor);
        void openForChannel (int channelIndex);
        void close();
        bool isOpen() const noexcept { return isVisible(); }
        void setTheme (const Theme& t);

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;

    private:
        MixerView& owner;
        PluginProcessor& processor;
        int currentChannel = -1;
        Theme theme = Theme::sketch();

        HandButton closeBtn { "CLOSE" };
        HandButton eqToggle { "EQ ON", true };
        HandButton compToggle { "COMP ON", true };

        // EQ Knobs
        std::unique_ptr<Knob> eqLowGainKnob;
        std::unique_ptr<Knob> eqMidGainKnob;
        std::unique_ptr<Knob> eqMidFreqKnob;
        std::unique_ptr<Knob> eqHighGainKnob;

        // Compressor Knobs
        std::unique_ptr<Knob> compThreshKnob;
        std::unique_ptr<Knob> compRatioKnob;
        std::unique_ptr<Knob> compAttackKnob;
        std::unique_ptr<Knob> compReleaseKnob;
        std::unique_ptr<Knob> compMakeupKnob;

        // Saturation & Transient Shaper Knobs
        std::unique_ptr<Knob> satDriveKnob;
        std::unique_ptr<Knob> transAttackKnob;
        std::unique_ptr<Knob> transSustainKnob;
    };

    //==========================================================================
    // Master FX Drawer Popup (Reverb Return, Size, Damping, Limiter)
    //==========================================================================
    class MasterFXPopup : public juce::Component
    {
    public:
        MasterFXPopup (MixerView& owner, PluginProcessor& processor);
        void open();
        void close();
        bool isOpen() const noexcept { return isVisible(); }
        void setTheme (const Theme& t);

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;

    private:
        MixerView& owner;
        PluginProcessor& processor;
        Theme theme = Theme::sketch();

        HandButton closeBtn { "CLOSE" };
        HandButton limiterToggle { "LIMITER", true };

        std::unique_ptr<Knob> revReturnKnob;
        std::unique_ptr<Knob> revSizeKnob;
        std::unique_ptr<Knob> revDampKnob;
        std::unique_ptr<Knob> revWidthKnob;
    };

    //==========================================================================
    // Bleed Matrix Popup / Panel (Multi-mic Leakage & Acoustic Crosstalk Matrix)
    //==========================================================================
    class BleedMatrixPopup : public juce::Component
    {
    public:
        BleedMatrixPopup (MixerView& owner, PluginProcessor& processor);
        void open (int highlightMic = -1);
        void close();
        bool isOpen() const noexcept { return isVisible(); }
        void updateFromState();
        void setTheme (const Theme& t);

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    private:
        MixerView& owner;
        PluginProcessor& processor;
        Theme theme = Theme::sketch();
        int highlightedMicIndex = -1;

        HandButton closeBtn { "CLOSE" };

        // Presets
        HandButton naturalBtn { "NATURAL" };
        HandButton cleanBtn   { "CLEAN (DRY)" };
        HandButton liveBtn    { "LIVE ROOM" };
        HandButton ohOnlyBtn  { "OH ONLY" };

        // Modes
        HandButton hybridModeBtn   { "HYBRID ACOUSTIC", true };
        HandButton recordedModeBtn { "RECORDED ONLY",   true };
        HandButton disabledModeBtn { "MUTE BLEED",      true };

        std::unique_ptr<Knob> masterBleedKnob;
        std::unique_ptr<Knob> sympResonanceKnob;

        // Matrix Grid Interaction
        int draggingPiece = -1;
        int draggingMic = -1;
        float dragStartGain = 1.0f;
        juce::Point<int> dragStartPos;

        // Hover & Macro Mutes
        int hoveredPiece = -1;
        int hoveredMic = -1;
        int hoveredRowMutePiece = -1;
        int hoveredColMuteMic = -1;

        // Tooltip bubble
        bool isEditingValue = false;
        float activeEditDb = 0.0f;
        juce::Point<int> activeEditPos;

        // Cached layout
        juce::Rectangle<int> gridBounds;
        int rowHeight = 22;
        int colWidth = 64;
        int headerColWidth = 100;

        bool getCellAtPos (juce::Point<int> pos, int& outPiece, int& outMic) const;
        bool getRowMuteAtPos (juce::Point<int> pos, int& outPiece) const;
        bool getColMuteAtPos (juce::Point<int> pos, int& outMic) const;
    };

    //==========================================================================
    // Individual Channel Strip Component
    //==========================================================================
    class ChannelStripComponent : public juce::Component
    {
    public:
        ChannelStripComponent (MixerView& owner, PluginProcessor& processor, int channelIndex);

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;

        int getChannelIndex() const noexcept { return channelIndex_; }
        void updateFromState();
        void setTheme (const Theme& t);

    private:
        MixerView& owner;
        PluginProcessor& processor;
        int channelIndex_ = 0;
        Theme theme = Theme::sketch();

        HandButton phaseBtn { "0", true };
        HandButton fxBtn    { "FX", true };
        HandButton bleedBtn { "BLD" };
        HandButton soloBtn  { "S", true };
        HandButton muteBtn  { "M", true };
        HandButton busBtn   { "M 1/2" };

        // Fader & Meter bounds
        juce::Rectangle<float> faderTrackBounds;
        juce::Rectangle<float> faderThumbBounds;
        juce::Rectangle<float> meterBounds;

        bool draggingFader = false;
        bool draggingPan = false;
        juce::Rectangle<float> panBarBounds;
    };

    //==========================================================================
    // Master Strip Component
    //==========================================================================
    class MasterStripComponent : public juce::Component
    {
    public:
        MasterStripComponent (MixerView& owner, PluginProcessor& processor);

        void paint (juce::Graphics& g) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;

        void updateFromState();
        void setTheme (const Theme& t);

    private:
        MixerView& owner;
        PluginProcessor& processor;
        Theme theme = Theme::sketch();

        HandButton fxBtn { "REVERB FX" };
        HandButton limiterBtn { "LIMITER", true };

        juce::Rectangle<float> faderTrackBounds;
        juce::Rectangle<float> faderThumbBounds;
        juce::Rectangle<float> meterLBounds;
        juce::Rectangle<float> meterRBounds;
        juce::Rectangle<float> panBarBounds;

        bool draggingFader = false;
        bool draggingPan = false;
    };

    //==========================================================================
    // Subcomponents & Containers
    //==========================================================================
    juce::Viewport channelViewport;
    std::unique_ptr<juce::Component> channelContainer;
    std::vector<std::unique_ptr<ChannelStripComponent>> channelStrips;

    std::unique_ptr<MasterStripComponent> masterStrip;

    // Preset buttons
    HandButton presetCleanBtn  { "CLEAN", false };
    HandButton presetRockBtn   { "ROCK", false };
    HandButton presetWarmBtn   { "WARM", false };
    HandButton presetPunchBtn  { "PUNCH", false };
    HandButton presetDryBtn    { "DRY", false };
    HandButton savePresetBtn   { "SAVE .glorpmix" };
    HandButton loadPresetBtn   { "LOAD..." };

    // Bleed Matrix Button
    HandButton bleedMatrixBtn  { "BLEED MATRIX" };

    // FX Drawers & Popups
    std::unique_ptr<ChannelFXPopup> channelFXPopup;
    std::unique_ptr<MasterFXPopup> masterFXPopup;
    std::unique_ptr<BleedMatrixPopup> bleedMatrixPopup;

    void applyPreset (int presetIndex);
    void promptSavePreset();
    void promptLoadPreset();

    juce::String activePresetName { "Default Clean" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};

} // namespace glorp
