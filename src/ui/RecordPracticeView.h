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

    /// Notify real-time note hit for the rolling rhythm ribbon
    void recordDrumHit (int instrumentIndex, float velocity);

    bool keyPressed (const juce::KeyPress& key) override;
    void togglePlayPause();
    void stopTransportAndRecording();

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

        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.5f);
            juce::Colour bg = baseColour;
            if (bg.getAlpha() == 0)
                bg = theme.panel;

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
            g.setFont (displayFont (14.0f));
            g.setColour (isArmed ? juce::Colours::white : theme.ink);
            g.drawText (label, b, juce::Justification::centred, false);
        }

    private:
        juce::String label;
        juce::Colour baseColour { juce::Colours::transparentBlack };
        Theme theme;
        bool isArmed = false;
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
    juce::ComboBox countInCombo;
    juce::ComboBox clickSoundCombo;
    std::unique_ptr<Knob> clickVolumeKnob;

    // Recording controls
    ComicButton recordButton { "REC" };
    ComicButton stopButton { "STOP" };
    juce::ComboBox recordSourceCombo; // Drums Only vs Full Mix
    ComicButton exportWavButton { "Export WAV" };
    ComicButton exportMidButton { "Export MIDI" };

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
    void drawRhythmRibbon (juce::Graphics&, juce::Rectangle<int> area);

    // Rhythm Ribbon state
    struct RibbonNote
    {
        int lane = 0;       // 0: Kick, 1: Snare, 2: HiHat, 3: Toms, 4: Cymbals, 5: Ride
        float position = 1.0f; // 1.0 (right) to 0.0 (left)
        float velocity = 1.0f;
    };
    std::deque<RibbonNote> activeRibbonNotes;

    // UI state caches
    double detectedBpm = 120.0;
    float bpmConfidence = 0.0f;
    bool isAnalyzing = false;
    bool isRecArmed = false;
    int currentBeatFlash = 1;
    bool isDownbeatFlash = false;
    double lastFlashTime = 0.0;

    void updateButtonStyles();
};

} // namespace glorp
