/*
    RecordPracticeView.h - Hand-drawn comic dashboard for GlorpDrums recording and practice.
    Includes backing track waveform deck, smart tempo controls, metronome count-in, and dual WAV/MIDI recorder.
*/

#pragma once

#include "BackingTrackPlayer.h"
#include "Knob.h"
#include "Metronome.h"
#include "MidiRecorder.h"
#include "AudioRecorder.h"
#include "Settings.h"
#include "TempoDetector.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>
#include <vector>

namespace glorp
{

class PluginProcessor;

class RecordPracticeView : public juce::Component,
                           public juce::Timer,
                           public juce::FileDragAndDropTarget
{
public:
    explicit RecordPracticeView (PluginProcessor&);
    ~RecordPracticeView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    // -- Drag and Drop Backing Track ------------------------------------------
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void applyTheme (const Theme& theme);


    bool keyPressed (const juce::KeyPress& key) override;
    void togglePlayPause();
    void stopTransportAndRecording();

    void setBeatsPerMeasure (int beats);
    void triggerBeatFlash (int beat, bool isDownbeat);
    void setAlsoRecordBackingTrack (bool alsoRecord);

private:
    PluginProcessor& processor;
    Theme currentTheme;

    // -- Sub-components & Controls --------------------------------------------
    class ComicButton : public juce::Button
    {
    public:
        explicit ComicButton (const juce::String& buttonText = {})
            : juce::Button (buttonText), label (buttonText)
        {
            setButtonText (buttonText);
        }

        void setLabel (const juce::String& newLabel)
        {
            label = newLabel;
            setButtonText (newLabel);
            repaint();
        }

        const juce::String& getLabel() const noexcept { return label; }

        void setBaseColour (juce::Colour c)
        {
            baseColour = c;
            repaint();
        }

        void setTheme (const Theme& t)
        {
            theme = t;
            repaint();
        }

        void setArmed (bool armed)
        {
            isArmed = armed;
            repaint();
        }
        bool getArmed() const noexcept { return isArmed; }

        void setDisabledStyle (bool disabled)
        {
            isDisabled = disabled;
            repaint();
        }
        bool getDisabledStyle() const noexcept { return isDisabled; }

        void setFontSize (float sz)
        {
            customFontSize = sz;
            repaint();
        }

        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.5f);
            juce::Colour bg = baseColour;
            if (bg.getAlpha() == 0)
                bg = theme.panel;

            if (isDisabled)
            {
                bg = theme.panel.withAlpha (0.45f);
                g.setColour (bg);
                g.fillRoundedRectangle (b, 4.0f);
                drawSketchRect (g, b, theme.panelEdge.withAlpha (0.35f), 1.2f, static_cast<unsigned> (label.hashCode()));
                g.setFont (displayFont (customFontSize > 0.0f ? customFontSize : 14.0f).boldened());
                g.setColour (theme.inkSoft.withAlpha (0.45f));
                g.drawText (label, b.reduced (10.0f, 0.0f),
                            label.startsWith ("Click Preset:") ? juce::Justification::centredLeft : juce::Justification::centred, false);
                return;
            }

            if (isArmed)
            {
                // Multi-layer radiant red pulsing halo glow
                const double nowSec = juce::Time::getMillisecondCounterHiRes() * 0.001;
                const float pulse = 0.5f + 0.5f * static_cast<float> (std::sin (nowSec * 7.0));
                const auto redCol = juce::Colour (0xffe74c3c);

                for (int r = 5; r >= 1; --r)
                {
                    const float alpha = (0.35f * pulse) / static_cast<float> (r);
                    g.setColour (redCol.withAlpha (alpha));
                    g.drawRoundedRectangle (b.expanded (static_cast<float> (r)), 4.0f + static_cast<float> (r), 1.8f);
                }
                bg = redCol.withAlpha (0.75f + 0.25f * pulse);
            }
            else if (isDown)
                bg = bg.interpolatedWith (theme.accent, 0.35f);
            else if (isHover)
                bg = bg.interpolatedWith (theme.panelEdge, 0.15f);

            g.setColour (bg);
            g.fillRoundedRectangle (b, 4.0f);

            // Sketched comic border
            drawSketchRect (g, b, isArmed ? juce::Colour (0xffe74c3c) : theme.panelEdge,
                            isHover ? 2.0f : 1.4f, static_cast<unsigned> (label.hashCode()));

            // Comic display font
            g.setFont (displayFont (customFontSize > 0.0f ? customFontSize : 14.0f).boldened());
            g.setColour (isArmed ? juce::Colours::white : theme.ink);
            g.drawText (label, b.reduced (10.0f, 0.0f),
                        label.startsWith ("Click Preset:") ? juce::Justification::centredLeft : juce::Justification::centred, false);
        }

    private:
        juce::String label;
        juce::Colour baseColour { juce::Colours::transparentBlack };
        Theme theme;
        bool isArmed = false;
        bool isDisabled = false;
        float customFontSize = 0.0f;
    };

    // Backing track transport
    ComicButton loadTrackButton { "Load Track..." };
    ComicButton clearTrackButton { "Clear Track" };
    ComicButton playButton { "Play" };
    ComicButton pauseButton { "Pause" };
    ComicButton rewindButton { "Rewind" };
    ComicButton loopButton { "Loop: OFF" };
    ComicButton muteTrackButton { "Mute" };
    std::unique_ptr<Knob> trackVolumeKnob;

    // Tempo controls
    ComicButton detectBpmButton { "Detect BPM" };
    ComicButton tapBpmButton { "Tap Tempo" };
    ComicButton alignBeatOneButton { "Align Beat 1" };
    ComicButton halfBpmButton { "/ 2" };
    ComicButton doubleBpmButton { "x 2" };
    ComicButton nudgeDownButton { "-0.5" };
    ComicButton nudgeUpButton { "+0.5" };
    juce::TextEditor bpmEditor;

    // Metronome controls
    ComicButton metroToggleButton { "Metronome: OFF" };
    ComicButton countInToggleButton { "Count In: ON" };
    ComicButton countIn1BarButton { "1 bar" };
    ComicButton countIn2BarsButton { "2 bars" };
    ComicButton clickPresetButton { "Click Preset: Woodblock" };
    juce::TextEditor beatsEditor;
    std::unique_ptr<Knob> clickVolumeKnob;

    // Recording controls
    ComicButton recordButton { "REC" };
    ComicButton stopButton { "STOP" };
    ComicButton alsoRecordTrackButton { "Also record backing track" };
    ComicButton exportWavButton { "Export WAV" };
    ComicButton exportMidButton { "Export MIDI" };

    // Beat lights state
    int beatsPerMeasure = 4;
    int activeBeatFlash = 0;
    bool isDownbeatFlash = false;
    double lastBeatFlashTime = 0.0;
    std::vector<juce::Rectangle<int>> beatLightBounds;

    // Tap tempo calculation
    std::vector<double> tapTimestamps;
    void handleTapTempo();

    // Waveform & Synchronized Beat Timeline interaction
    juce::Rectangle<int> waveformBounds;
    juce::Rectangle<int> timelineBounds;
    float zoomFactor = 1.0f;
    double viewStartSec = 0.0;
    double metronomeOffsetSec = 0.0;

    bool isDraggingPlayhead = false;
    bool isDraggingTimeline = false;
    bool isOffsettingGrid = false;
    bool isPanningView = false;
    int dragStartX = 0;
    double dragStartOffsetSec = 0.0;
    double dragStartViewSec = 0.0;
    int panStartX = 0;
    double panStartViewSec = 0.0;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void startTransportWithCountIn();
    void pauseTransport();

    void drawWaveformDeck (juce::Graphics&, juce::Rectangle<int> area);
    void drawBeatTimelineBar (juce::Graphics&, juce::Rectangle<int> area);
    void drawPracticeConsole (juce::Graphics&, juce::Rectangle<int> area);
    void drawRecordingConsole (juce::Graphics&, juce::Rectangle<int> area);
    void drawBeatLights (juce::Graphics&);

    // UI state caches
    double detectedBpm = 120.0;
    float bpmConfidence = 0.0f;
    bool isAnalyzing = false;
    bool isRecArmed = false;
    bool alsoRecordBackingTrack = false;

    void updateButtonStyles();
    void updateCountInButtons();
};

} // namespace glorp
