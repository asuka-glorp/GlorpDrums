/*
    RecordPracticeView.h - Hand-drawn comic dashboard for GlorpDrums recording, setlists, and practice.
    Includes backing track waveform deck, smart tempo controls, persistent song database,
    sequential setlist player with count-in transitions, and dual WAV/MIDI recorder.
*/

#pragma once

#include "AudioRecorder.h"
#include "BackingTrackPlayer.h"
#include "Knob.h"
#include "Metronome.h"
#include "MidiRecorder.h"
#include "SetlistPlayer.h"
#include "Settings.h"
#include "SongDatabase.h"
#include "TempoDetector.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>
#include <set>
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

    /// Loads an audio file into the deck, restoring saved SongDatabase settings if already configured,
    /// or triggering auto-BPM detection for brand-new tracks.
    void loadTrackAndSyncDatabase (const juce::File& file, bool addToActiveSetlist = false);

    /// Opens or closes the Persistent Song Database modal overlay.
    void setSongDatabaseModalOpen (bool open);
    bool isSongDatabaseModalVisible() const noexcept { return isSongDbModalOpen; }

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
            if (label == newLabel)
                return;
            label = newLabel;
            setButtonText (newLabel);
            repaint();
        }

        const juce::String& getLabel() const noexcept { return label; }

        void setBaseColour (juce::Colour c)
        {
            if (baseColour == c)
                return;
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
            if (isArmed == armed && ! isArmed)
                return;
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

        void setLeftAligned (bool leftAlign)
        {
            isLeftAligned = leftAlign;
            repaint();
        }

        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override
        {
            auto b = getLocalBounds().toFloat().reduced (1.5f);
            juce::Colour bg = baseColour;
            if (bg.getAlpha() == 0)
                bg = theme.panel;

            const bool alignLeft = isLeftAligned || label.startsWith ("Click Preset:") || label.startsWith ("Set:");

            if (isDisabled)
            {
                bg = theme.panel.withAlpha (0.45f);
                g.setColour (bg);
                g.fillRoundedRectangle (b, 4.0f);
                drawSketchRect (g, b, theme.panelEdge.withAlpha (0.35f), 1.2f, static_cast<unsigned> (label.hashCode()));
                g.setFont (displayFont (customFontSize > 0.0f ? customFontSize : 13.0f).boldened());
                g.setColour (theme.inkSoft.withAlpha (0.45f));
                g.drawText (label, b.reduced (8.0f, 0.0f),
                            alignLeft ? juce::Justification::centredLeft : juce::Justification::centred, false);
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
            g.setFont (displayFont (customFontSize > 0.0f ? customFontSize : 13.0f).boldened());
            if (isArmed)
                g.setColour (juce::Colours::white);
            else if (bg.getPerceivedBrightness() > 0.56f)
                g.setColour (juce::Colour (0xff101114));
            else
                g.setColour (theme.ink);
            g.drawText (label, b.reduced (alignLeft ? 8.0f : 3.0f, 0.0f),
                        alignLeft ? juce::Justification::centredLeft : juce::Justification::centred, false);
        }

    private:
        juce::String label;
        juce::Colour baseColour { juce::Colours::transparentBlack };
        Theme theme;
        bool isArmed = false;
        bool isDisabled = false;
        bool isLeftAligned = false;
        float customFontSize = 0.0f;
    };

    // -- Top Deck: Backing Track & Setlist Transport --------------------------
    ComicButton loadTrackButton { "Load Track..." };
    ComicButton prevSongButton  { "|< Prev" };
    ComicButton playButton      { "Play Setlist" };
    ComicButton pauseButton     { "Pause" };
    ComicButton nextSongButton  { "Next >|" };
    ComicButton rewindButton    { "Rewind" };
    ComicButton loopButton      { "Loop Song: OFF" };
    ComicButton muteTrackButton { "Mute" };
    juce::Rectangle<int> setlistStatusPillBounds;
    std::unique_ptr<Knob> trackVolumeKnob;

    // -- Top Deck: Tempo & Beat Grid Controls ---------------------------------
    ComicButton detectBpmButton    { "Detect BPM" };
    ComicButton tapBpmButton       { "Tap Tempo" };
    ComicButton alignBeatOneButton { "Align Beat 1" };
    ComicButton halfBpmButton      { "/ 2" };
    ComicButton doubleBpmButton    { "x 2" };
    ComicButton nudgeDownButton    { "-0.5" };
    ComicButton nudgeUpButton      { "+0.5" };
    juce::TextEditor bpmEditor;

    // -- Column 1: Setlist & Song Queue Controls ------------------------------
    ComicButton autoNextToggleButton   { "Auto-Next: Count-In ON" };
    ComicButton setlistSelectButton    { "Set: Practice Setlist 1 v" };
    ComicButton saveSetlistButton      { "Save" };
    ComicButton newSetlistButton       { "New" };
    ComicButton addFileToSetlistButton { "+ Add File" };
    ComicButton openSongDbButton       { "+ Song DB" };

    struct SetlistRowGeometry
    {
        int itemIndex = -1;
        juce::Rectangle<int> rowBounds;
        juce::Rectangle<int> moveUpBounds;
        juce::Rectangle<int> moveDownBounds;
        juce::Rectangle<int> removeBounds;
    };
    juce::Rectangle<int> setlistTableHeaderBounds;
    juce::Rectangle<int> setlistTableBodyBounds;
    std::vector<SetlistRowGeometry> setlistRowGeometries;
    int setlistScrollOffset = 0;

    // -- Column 2: Practice & Metronome Controls ------------------------------
    ComicButton metroToggleButton   { "Metronome: OFF" };
    ComicButton countInToggleButton { "Count In: ON" };
    ComicButton countIn1BarButton   { "1 bar" };
    ComicButton countIn2BarsButton  { "2 bars" };
    ComicButton clickPresetButton   { "Click Preset: Woodblock" };
    juce::TextEditor beatsEditor;
    std::unique_ptr<Knob> clickVolumeKnob;

    // -- Column 3: Dual Recording Console Controls ----------------------------
    ComicButton recordButton              { "REC" };
    ComicButton stopButton                { "STOP" };
    ComicButton alsoRecordTrackButton     { "Also record backing track" };
    ComicButton recordEntireSetlistButton { "Record Entire Setlist Take" };
    ComicButton exportWavButton           { "Export WAV" };
    ComicButton exportMidButton           { "Export MIDI" };

    // -- Persistent Song Database Modal Overlay -------------------------------
    class SongDbModalOverlay : public juce::Component
    {
    public:
        explicit SongDbModalOverlay (RecordPracticeView& o) : owner (o) {}
        void paint (juce::Graphics& g) override { owner.drawSongDatabaseModal (g); }
        void mouseDown (const juce::MouseEvent& e) override { owner.handleSongDbModalMouseDown (e); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { owner.handleSongDbModalMouseWheel (e, w); }
    private:
        RecordPracticeView& owner;
    };

    SongDbModalOverlay songDbModalOverlay { *this };
    bool isSongDbModalOpen = false;
    juce::Rectangle<int> songDbModalBounds;
    juce::TextEditor songDbSearchEditor;
    ComicButton songDbImportButton      { "+ Import New Audio Files..." };
    ComicButton songDbAddSelectedButton { "Add Selected to Setlist" };
    ComicButton songDbCloseButton       { "Close [X]" };

    void handleSongDbModalMouseDown (const juce::MouseEvent& e);
    void handleSongDbModalMouseWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel);

    struct SongDbRowGeometry
    {
        juce::String songId;
        juce::Rectangle<int> rowBounds;
        juce::Rectangle<int> checkboxBounds;
        juce::Rectangle<int> addBtnBounds;
    };
    std::vector<SongDbRowGeometry> songDbRowGeometries;
    std::set<juce::String> selectedSongDbIds;
    int songDbScrollOffset = 0;

    // -- Beat lights state ----------------------------------------------------
    int beatsPerMeasure = 4;
    int activeBeatFlash = 0;
    bool isDownbeatFlash = false;
    double lastBeatFlashTime = 0.0;
    std::vector<juce::Rectangle<int>> beatLightBounds;

    // -- Tap tempo calculation ------------------------------------------------
    std::vector<double> tapTimestamps;
    void handleTapTempo();

    // -- Waveform & Synchronized Beat Timeline interaction --------------------
    juce::Rectangle<int> topDeckBounds;
    juce::Rectangle<int> setlistConsoleBounds;
    juce::Rectangle<int> practiceConsoleBounds;
    juce::Rectangle<int> recordingConsoleBounds;
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
    void saveCurrentTrackToDatabase();
    void syncUiFromSongEntry (const SongEntry& entry, double effectiveBpm);
    void refreshSetlistSelectorLabel();
    void rebuildSetlistRowGeometries();

    void drawWaveformDeck (juce::Graphics&, juce::Rectangle<int> area);
    void drawBeatTimelineBar (juce::Graphics&, juce::Rectangle<int> area);
    void drawSetlistConsole (juce::Graphics&, juce::Rectangle<int> area);
    void drawPracticeConsole (juce::Graphics&, juce::Rectangle<int> area);
    void drawRecordingConsole (juce::Graphics&, juce::Rectangle<int> area);
    void drawBeatLights (juce::Graphics&);
    void drawSongDatabaseModal (juce::Graphics&);

    // UI state caches
    double detectedBpm = 120.0;
    float bpmConfidence = 0.0f;
    bool isAnalyzing = false;
    bool isRecArmed = false;
    bool alsoRecordBackingTrack = false;
    bool recordEntireSetlistTake = true;

    void updateButtonStyles();
    void updateCountInButtons();
};

} // namespace glorp
