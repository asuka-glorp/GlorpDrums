/*
    RecordPracticeView.cpp - Hand-drawn comic dashboard for GlorpDrums recording and practice.
*/

#include "RecordPracticeView.h"
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

namespace glorp
{

//==============================================================================
RecordPracticeView::RecordPracticeView (PluginProcessor& p)
    : processor (p)
{
    setWantsKeyboardFocus (true);

    // Backing track transport buttons
    addAndMakeVisible (loadTrackButton);
    loadTrackButton.onClick = [this]()
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Select Backing Track Audio",
            juce::File::getSpecialLocation (juce::File::userHomeDirectory),
            "*.wav;*.mp3;*.flac;*.aiff;*.ogg");

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto file = fc.getResult();
                if (file.existsAsFile())
                {
                    processor.getBackingTrackPlayer().loadFile (file);
                    // Automatically trigger tempo detection
                    isAnalyzing = true;
                    processor.getTempoWorker().startDetection (
                        processor.getBackingTrackPlayer().getDecodedBuffer(),
                        processor.getSampleRate(),
                        [this](const TempoDetector::Result& res)
                        {
                            isAnalyzing = false;
                            if (res.isValid)
                            {
                                detectedBpm = res.bpm;
                                bpmConfidence = res.confidence;
                                processor.getMetronome().setBpm (detectedBpm);
                                bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
                                metronomeOffsetSec = std::max (0.0, res.firstDownbeatSeconds);
                                processor.getMetronome().setGridOffset (metronomeOffsetSec);
                            }
                            repaint();
                        });
                    repaint();
                }
            });
    };

    addAndMakeVisible (clearTrackButton);
    clearTrackButton.onClick = [this]()
    {
        stopTransportAndRecording();
        processor.getBackingTrackPlayer().clear();
        detectedBpm = 120.0;
        bpmConfidence = 0.0f;
        processor.getMetronome().setBpm (120.0);
        bpmEditor.setText ("120.0", false);
        metronomeOffsetSec = 0.0;
        zoomFactor = 1.0f;
        viewStartSec = 0.0;
        processor.getMetronome().setGridOffset (0.0);
        repaint();
    };

    addAndMakeVisible (playButton);
    playButton.onClick = [this]() { togglePlayPause(); };

    addAndMakeVisible (pauseButton);
    pauseButton.onClick = [this]() { pauseTransport(); };

    addAndMakeVisible (rewindButton);
    rewindButton.onClick = [this]()
    {
        processor.getBackingTrackPlayer().stop();
        if (! processor.getBackingTrackPlayer().isPlaying())
            processor.getMetronome().stop();
        repaint();
    };

    addAndMakeVisible (loopButton);
    loopButton.onClick = [this]()
    {
        bool nextState = ! processor.getBackingTrackPlayer().isLooping();
        processor.getBackingTrackPlayer().setLooping (nextState);
        loopButton.setLabel (nextState ? "Loop: ON" : "Loop: OFF");
        loopButton.setBaseColour (nextState ? currentTheme.accent.withAlpha (0.35f) : currentTheme.panel);
    };

    addAndMakeVisible (muteTrackButton);
    muteTrackButton.onClick = [this]()
    {
        bool nextMute = ! processor.getBackingTrackPlayer().getMuted();
        processor.getBackingTrackPlayer().setMuted (nextMute);
        muteTrackButton.setLabel (nextMute ? "Unmute" : "Mute");
        muteTrackButton.setBaseColour (nextMute ? currentTheme.warning.withAlpha (0.4f) : currentTheme.panel);
    };

    trackVolumeKnob = std::make_unique<Knob> ("Track Vol", 0.0f, 1.5f, 0.8f);
    addAndMakeVisible (*trackVolumeKnob);
    trackVolumeKnob->onValueChanged = [this](float val)
    {
        processor.getBackingTrackPlayer().setLevel (val);
    };

    // Tempo controls
    addAndMakeVisible (detectBpmButton);
    detectBpmButton.onClick = [this]()
    {
        if (processor.getBackingTrackPlayer().hasTrack())
        {
            const double totalDur = processor.getBackingTrackPlayer().getDurationSeconds();
            const double visibleDur = totalDur / std::max (1.0f, zoomFactor);

            isAnalyzing = true;
            auto completionHandler = [this](const TempoDetector::Result& res)
            {
                isAnalyzing = false;
                if (res.isValid)
                {
                    detectedBpm = res.bpm;
                    bpmConfidence = res.confidence;
                    processor.getMetronome().setBpm (detectedBpm);
                    bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
                    metronomeOffsetSec = std::max (0.0, res.firstDownbeatSeconds);
                    processor.getMetronome().setGridOffset (metronomeOffsetSec);
                }
                repaint();
            };

            if (zoomFactor > 1.25f && visibleDur >= 3.0)
            {
                processor.getTempoWorker().startDetectionRange (
                    processor.getBackingTrackPlayer().getDecodedBuffer(),
                    processor.getSampleRate(),
                    viewStartSec,
                    visibleDur,
                    completionHandler);
            }
            else
            {
                processor.getTempoWorker().startDetection (
                    processor.getBackingTrackPlayer().getDecodedBuffer(),
                    processor.getSampleRate(),
                    completionHandler);
            }
            repaint();
        }
    };

    addAndMakeVisible (tapBpmButton);
    tapBpmButton.onClick = [this]() { handleTapTempo(); };

    addAndMakeVisible (alignBeatOneButton);
    alignBeatOneButton.onClick = [this]()
    {
        if (processor.getBackingTrackPlayer().hasTrack())
        {
            const double playheadSec = processor.getBackingTrackPlayer().getPlayheadSeconds();
            metronomeOffsetSec = playheadSec;
            processor.getMetronome().setGridOffset (metronomeOffsetSec);
            repaint();
        }
    };

    addAndMakeVisible (halfBpmButton);
    halfBpmButton.onClick = [this]()
    {
        detectedBpm = std::clamp (detectedBpm * 0.5, 30.0, 300.0);
        processor.getMetronome().setBpm (detectedBpm);
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        repaint();
    };

    addAndMakeVisible (doubleBpmButton);
    doubleBpmButton.onClick = [this]()
    {
        detectedBpm = std::clamp (detectedBpm * 2.0, 30.0, 300.0);
        processor.getMetronome().setBpm (detectedBpm);
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        repaint();
    };

    addAndMakeVisible (nudgeDownButton);
    nudgeDownButton.onClick = [this]()
    {
        detectedBpm = std::clamp (detectedBpm - 0.5, 30.0, 300.0);
        processor.getMetronome().setBpm (detectedBpm);
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        repaint();
    };

    addAndMakeVisible (nudgeUpButton);
    nudgeUpButton.onClick = [this]()
    {
        detectedBpm = std::clamp (detectedBpm + 0.5, 30.0, 300.0);
        processor.getMetronome().setBpm (detectedBpm);
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        repaint();
    };

    addAndMakeVisible (bpmEditor);
    bpmEditor.setText ("120.0", false);
    bpmEditor.setJustification (juce::Justification::centred);
    bpmEditor.setInputRestrictions (6, "0123456789.");
    bpmEditor.onReturnKey = [this]()
    {
        const double val = bpmEditor.getText().getDoubleValue();
        if (val >= 30.0 && val <= 300.0)
        {
            detectedBpm = std::clamp (std::round (val * 10.0) / 10.0, 30.0, 300.0);
            processor.getMetronome().setBpm (detectedBpm);
            bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
            repaint();
        }
    };
    bpmEditor.onFocusLost = [this]()
    {
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
    };

    // Metronome controls
    addAndMakeVisible (metroToggleButton);
    metroToggleButton.onClick = [this]()
    {
        bool next = ! processor.getMetronome().isEnabled();
        processor.getMetronome().setEnabled (next);
        metroToggleButton.setLabel (next ? "Metronome: ON" : "Metronome: OFF");
        metroToggleButton.setBaseColour (next ? currentTheme.accent.withAlpha (0.35f) : currentTheme.panel);
    };

    addAndMakeVisible (countInCombo);
    countInCombo.addItem ("Count-In: OFF", 1);
    countInCombo.addItem ("Count-In: 1 Bar (4 Beats)", 2);
    countInCombo.addItem ("Count-In: 2 Bars (8 Beats)", 3);
    countInCombo.setSelectedId (2); // Default: 1 Bar
    countInCombo.onChange = [this]()
    {
        int id = countInCombo.getSelectedId();
        auto mode = (id == 1) ? Metronome::CountInMode::off
                              : (id == 2 ? Metronome::CountInMode::oneBar : Metronome::CountInMode::twoBars);
        processor.getMetronome().setCountInMode (mode);
    };

    addAndMakeVisible (clickSoundCombo);
    clickSoundCombo.addItem ("Click: Woodblock", 1);
    clickSoundCombo.addItem ("Click: Studio Beep", 2);
    clickSoundCombo.addItem ("Click: SideStick", 3);
    clickSoundCombo.addItem ("Click: Cowbell", 4);
    clickSoundCombo.addItem ("Click: Hi-Hat Pedal", 5);
    clickSoundCombo.addItem ("Click: Digital Beep", 6);
    clickSoundCombo.addItem ("Click: Clave", 7);
    clickSoundCombo.addItem ("Click: Triangle Bell", 8);
    clickSoundCombo.setSelectedId (1);
    clickSoundCombo.onChange = [this]()
    {
        processor.getMetronome().setClickSound (
            static_cast<Metronome::ClickSound> (clickSoundCombo.getSelectedId() - 1));
    };

    clickVolumeKnob = std::make_unique<Knob> ("Click Vol", 0.0f, 2.5f, 1.0f);
    addAndMakeVisible (*clickVolumeKnob);
    clickVolumeKnob->onValueChanged = [this](float val)
    {
        processor.getMetronome().setLevel (val);
    };

    // Recording controls
    addAndMakeVisible (recordButton);
    recordButton.onClick = [this]()
    {
        if (processor.getAudioRecorder().isRecording())
        {
            stopTransportAndRecording();
            return;
        }

        // Toggle armed recording state
        isRecArmed = ! isRecArmed;
        recordButton.setArmed (isRecArmed);
        recordButton.setLabel (isRecArmed ? "REC ARMED" : "REC");
        repaint();
    };

    addAndMakeVisible (stopButton);
    stopButton.onClick = [this]()
    {
        stopTransportAndRecording();
    };

    addAndMakeVisible (recordSourceCombo);
    recordSourceCombo.addItem ("Source: Drums Only (Default)", 1);
    recordSourceCombo.addItem ("Source: Full Mix (Drums + Track)", 2);
    recordSourceCombo.setSelectedId (1);
    recordSourceCombo.onChange = [this]()
    {
        auto src = (recordSourceCombo.getSelectedId() == 1) ? AudioRecorder::RecordingSource::drumsOnly
                                                            : AudioRecorder::RecordingSource::fullMix;
        processor.getAudioRecorder().setSource (src);
    };

    addAndMakeVisible (exportWavButton);
    exportWavButton.onClick = [this]()
    {
        auto recorded = processor.getAudioRecorder().getRecordedFile();
        if (recorded.existsAsFile())
        {
            auto chooser = std::make_shared<juce::FileChooser> (
                "Export WAV Drum Take",
                recorded.getParentDirectory().getChildFile (recorded.getFileName()),
                "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                [recorded, chooser](const juce::FileChooser& fc)
                {
                    auto dest = fc.getResult();
                    if (dest != juce::File())
                        recorded.copyFileTo (dest);
                });
        }
    };

    addAndMakeVisible (exportMidButton);
    exportMidButton.onClick = [this]()
    {
        if (processor.getMidiRecorder().getEventCount() > 0)
        {
            const auto timestamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
            const auto defaultFile = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                                         .getChildFile ("GlorpTake_" + timestamp + ".mid");

            auto chooser = std::make_shared<juce::FileChooser> (
                "Export Standard MIDI Take (.mid)",
                defaultFile,
                "*.mid");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                [this, chooser](const juce::FileChooser& fc)
                {
                    auto dest = fc.getResult();
                    if (dest != juce::File())
                        processor.getMidiRecorder().exportToFile (dest);
                });
        }
    };

    // Metronome beat tick notification
    processor.getMetronome().onBeat = [this](int beat, bool isDownbeat)
    {
        currentBeatFlash = beat;
        isDownbeatFlash = isDownbeat;
        lastFlashTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    };

    applyTheme (Theme::sketch());
    startTimerHz (30);
}

RecordPracticeView::~RecordPracticeView()
{
    stopTimer();
    processor.getMetronome().onBeat = nullptr;
    processor.getMetronome().onCountInComplete = nullptr;
}

//==============================================================================
void RecordPracticeView::togglePlayPause()
{
    if (processor.getBackingTrackPlayer().isPlaying() || processor.getMetronome().isCountingIn())
    {
        pauseTransport();
    }
    else
    {
        startTransportWithCountIn();
    }
}

void RecordPracticeView::startTransportWithCountIn()
{
    // If REC is armed, start recording
    if (isRecArmed && ! processor.getAudioRecorder().isRecording())
    {
        const auto homeDir = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
        const auto timestamp = juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S");
        const auto wavFile = homeDir.getChildFile ("GlorpTake_" + timestamp + ".wav");

        processor.getAudioRecorder().startRecording (wavFile);
        processor.getMidiRecorder().startRecording (
            processor.getSampleRate(),
            processor.getMetronome().getBpm());
    }

    if (processor.getMetronome().getCountInMode() != Metronome::CountInMode::off)
    {
        processor.getMetronome().setTransportPlaying (false);
        processor.getMetronome().onCountInComplete = [this]()
        {
            processor.getBackingTrackPlayer().play();
            processor.getMetronome().setTransportPlaying (true);
            repaint();
        };
        const double startPosSec = processor.getBackingTrackPlayer().getPlayheadSeconds();
        processor.getMetronome().startCountIn (startPosSec);
    }
    else
    {
        processor.getBackingTrackPlayer().play();
        processor.getMetronome().setTransportPlaying (true);
    }

    repaint();
}

void RecordPracticeView::pauseTransport()
{
    processor.getBackingTrackPlayer().pause();
    processor.getMetronome().setTransportPlaying (false);
    processor.getMetronome().stop();
    repaint();
}

void RecordPracticeView::stopTransportAndRecording()
{
    processor.getMetronome().stop();
    processor.getBackingTrackPlayer().stop();
    if (processor.getAudioRecorder().isRecording())
        processor.getAudioRecorder().stopRecording();
    if (processor.getMidiRecorder().isRecording())
        processor.getMidiRecorder().stopRecording();

    isRecArmed = false;
    recordButton.setArmed (false);
    recordButton.setLabel ("REC");
    repaint();
}

bool RecordPracticeView::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::spaceKey))
    {
        togglePlayPause();
        return true;
    }
    if (key.isKeyCode (juce::KeyPress::homeKey))
    {
        processor.getBackingTrackPlayer().stop();
        if (! processor.getBackingTrackPlayer().isPlaying())
            processor.getMetronome().stop();
        repaint();
        return true;
    }
    if (key.getTextCharacter() == 'a' || key.getTextCharacter() == 'A')
    {
        if (processor.getBackingTrackPlayer().hasTrack())
        {
            const double playheadSec = processor.getBackingTrackPlayer().getPlayheadSeconds();
            metronomeOffsetSec = playheadSec;
            processor.getMetronome().setGridOffset (metronomeOffsetSec);
            repaint();
            return true;
        }
    }
    return false;
}

//==============================================================================
void RecordPracticeView::applyTheme (const Theme& theme)
{
    currentTheme = theme;
    if (trackVolumeKnob != nullptr)
        trackVolumeKnob->setTheme (theme);
    if (clickVolumeKnob != nullptr)
        clickVolumeKnob->setTheme (theme);

    updateButtonStyles();
    repaint();
}

void RecordPracticeView::updateButtonStyles()
{
    auto styleBtn = [&](ComicButton& btn, juce::Colour col)
    {
        btn.setTheme (currentTheme);
        btn.setBaseColour (col);
    };

    const auto greenCol = juce::Colour (0xff2ecc71);
    const auto blueCol = juce::Colour (0xff3498db);
    const auto redCol = currentTheme.warning;
    const auto orangeCol = currentTheme.accent;

    styleBtn (loadTrackButton, currentTheme.panel);
    styleBtn (clearTrackButton, currentTheme.panel);
    styleBtn (playButton, greenCol.withAlpha (0.4f));
    styleBtn (pauseButton, currentTheme.panel);
    styleBtn (rewindButton, currentTheme.panel);
    styleBtn (loopButton, currentTheme.panel);
    styleBtn (muteTrackButton, currentTheme.panel);

    styleBtn (detectBpmButton, blueCol.withAlpha (0.35f));
    styleBtn (tapBpmButton, currentTheme.panel);
    styleBtn (alignBeatOneButton, currentTheme.panel);
    styleBtn (halfBpmButton, currentTheme.panel);
    styleBtn (doubleBpmButton, currentTheme.panel);
    styleBtn (nudgeDownButton, currentTheme.panel);
    styleBtn (nudgeUpButton, currentTheme.panel);

    styleBtn (metroToggleButton, currentTheme.panel);
    styleBtn (recordButton, redCol.withAlpha (0.6f));
    styleBtn (stopButton, currentTheme.panel);
    styleBtn (exportWavButton, greenCol.withAlpha (0.35f));
    styleBtn (exportMidButton, orangeCol.withAlpha (0.35f));

    auto styleCombo = [&](juce::ComboBox& combo)
    {
        combo.setColour (juce::ComboBox::backgroundColourId, currentTheme.panel);
        combo.setColour (juce::ComboBox::textColourId, currentTheme.ink);
        combo.setColour (juce::ComboBox::outlineColourId, currentTheme.panelEdge);
        combo.setColour (juce::ComboBox::arrowColourId, currentTheme.panelEdge);
    };

    styleCombo (countInCombo);
    styleCombo (clickSoundCombo);
    styleCombo (recordSourceCombo);

    bpmEditor.setColour (juce::TextEditor::backgroundColourId, currentTheme.panel);
    bpmEditor.setColour (juce::TextEditor::textColourId, currentTheme.ink);
    bpmEditor.setColour (juce::TextEditor::highlightedTextColourId, currentTheme.panel);
    bpmEditor.setColour (juce::TextEditor::highlightColourId, currentTheme.accent);
    bpmEditor.setColour (juce::TextEditor::outlineColourId, currentTheme.panelEdge);
    bpmEditor.setColour (juce::TextEditor::focusedOutlineColourId, currentTheme.accent);
    bpmEditor.setFont (uiFont (14.0f).boldened());
    bpmEditor.applyColourToAllText (currentTheme.ink);
    bpmEditor.applyFontToAllText (uiFont (14.0f).boldened());
}

void RecordPracticeView::recordDrumHit (int instIndex, float vel)
{
    // Map instrument to lane:
    // 0: Kick, 1: Snare, 2: HiHat, 3: Toms, 4: Cymbals, 5: Ride
    int lane = 1; // Default snare
    if (instIndex == 0) // Kick
        lane = 0;
    else if (instIndex >= 1 && instIndex <= 3) // Snare
        lane = 1;
    else if (instIndex >= 4 && instIndex <= 9) // Toms
        lane = 3;
    else if (instIndex >= 10 && instIndex <= 13) // HiHat
        lane = 2;
    else if (instIndex >= 14 && instIndex <= 19) // Crashes
        lane = 4;
    else if (instIndex >= 20) // Ride
        lane = 5;

    activeRibbonNotes.push_back ({ lane, 1.0f, std::clamp (vel, 0.2f, 1.0f) });
    if (activeRibbonNotes.size() > 100)
        activeRibbonNotes.pop_front();
}

void RecordPracticeView::handleTapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (! tapTimestamps.empty() && (now - tapTimestamps.back()) > 2.5)
        tapTimestamps.clear();

    tapTimestamps.push_back (now);
    if (tapTimestamps.size() > 5)
        tapTimestamps.erase (tapTimestamps.begin());

    if (tapTimestamps.size() >= 2)
    {
        double totalInterval = 0.0;
        for (size_t i = 1; i < tapTimestamps.size(); ++i)
            totalInterval += (tapTimestamps[i] - tapTimestamps[i - 1]);

        const double avgInterval = totalInterval / static_cast<double> (tapTimestamps.size() - 1);
        if (avgInterval > 0.1)
        {
            detectedBpm = std::clamp (std::round (60.0 / avgInterval * 10.0) / 10.0, 40.0, 240.0);
            processor.getMetronome().setBpm (detectedBpm);
            bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        }
    }
    repaint();
}

//==============================================================================
bool RecordPracticeView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff" || ext == ".ogg")
            return true;
    }
    return false;
}

void RecordPracticeView::filesDropped (const juce::StringArray& files, int /*x*/, int /*y*/)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff" || ext == ".ogg")
        {
            processor.getBackingTrackPlayer().loadFile (juce::File (f));
            isAnalyzing = true;
            processor.getTempoWorker().startDetection (
                processor.getBackingTrackPlayer().getDecodedBuffer(),
                processor.getSampleRate(),
                [this](const TempoDetector::Result& res)
                {
                    isAnalyzing = false;
                    if (res.isValid)
                    {
                        detectedBpm = res.bpm;
                        bpmConfidence = res.confidence;
                        processor.getMetronome().setBpm (detectedBpm);
                        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
                        metronomeOffsetSec = std::max (0.0, res.firstDownbeatSeconds);
                        processor.getMetronome().setGridOffset (metronomeOffsetSec);
                    }
                    repaint();
                });
            repaint();
            break;
        }
    }
}

//==============================================================================
void RecordPracticeView::timerCallback()
{
    // Auto-follow playhead when zoomed in and playing
    if (processor.getBackingTrackPlayer().isPlaying() && zoomFactor > 1.05f
        && ! isPanningView && ! isDraggingPlayhead && ! isOffsettingGrid)
    {
        const double curSec = processor.getBackingTrackPlayer().getPlayheadSeconds();
        const double totalDur = std::max (0.01, processor.getBackingTrackPlayer().getDurationSeconds());
        const double visibleDur = totalDur / static_cast<double> (zoomFactor);

        // If playhead moved past 75% of visible window, or is behind viewStartSec
        if (curSec < viewStartSec || curSec > (viewStartSec + visibleDur * 0.75))
        {
            const double targetViewStart = std::clamp (curSec - visibleDur * 0.25, 0.0, std::max (0.0, totalDur - visibleDur));
            viewStartSec = targetViewStart;
        }
    }

    // Scroll active rhythm ribbon notes
    for (auto& note : activeRibbonNotes)
        note.position -= 0.025f;

    while (! activeRibbonNotes.empty() && activeRibbonNotes.front().position < -0.05f)
        activeRibbonNotes.pop_front();

    const bool isRec = processor.getAudioRecorder().isRecording();
    if (isRecArmed || isRec)
    {
        recordButton.setArmed (true);
        if (isRec)
            recordButton.setLabel ("RECORDING");
        else
            recordButton.setLabel ("REC ARMED");
    }
    else
    {
        recordButton.setArmed (false);
        recordButton.setLabel ("REC");
    }

    repaint();
}

void RecordPracticeView::mouseDown (const juce::MouseEvent& e)
{
    const bool inWave = waveformBounds.contains (e.getPosition());
    const bool inTime = timelineBounds.contains (e.getPosition());

    if (! inWave && ! inTime)
        return;

    // Middle click: initiate view panning
    if (e.mods.isMiddleButtonDown())
    {
        isPanningView = true;
        panStartX = e.x;
        panStartViewSec = viewStartSec;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        return;
    }

    dragStartX = e.x;
    dragStartOffsetSec = metronomeOffsetSec;
    dragStartViewSec = viewStartSec;

    const double totalDur = std::max (0.01, processor.getBackingTrackPlayer().getDurationSeconds());
    const double visibleDur = totalDur / std::max (1.0f, zoomFactor);

    // If holding Shift OR clicking inside timeline bar: slide metronome grid offset
    if (e.mods.isShiftDown() || inTime)
    {
        isOffsettingGrid = true;
        isDraggingPlayhead = false;
        isDraggingTimeline = false;
    }
    else if (inWave && processor.getBackingTrackPlayer().hasTrack())
    {
        isDraggingPlayhead = true;
        isOffsettingGrid = false;
        isDraggingTimeline = false;

        const float norm = std::clamp ((e.x - waveformBounds.getX()) / static_cast<float> (waveformBounds.getWidth()), 0.0f, 1.0f);
        const double targetSec = std::clamp (viewStartSec + static_cast<double> (norm) * visibleDur, 0.0, totalDur);
        processor.getBackingTrackPlayer().scrubTo (targetSec);
    }
    repaint();
}

void RecordPracticeView::mouseDrag (const juce::MouseEvent& e)
{
    const double totalDur = std::max (0.01, processor.getBackingTrackPlayer().getDurationSeconds());
    const double visibleDur = totalDur / std::max (1.0f, zoomFactor);

    if (isPanningView)
    {
        const int deltaX = e.x - panStartX;
        const double deltaSec = (static_cast<double> (deltaX) / static_cast<double> (std::max (1, waveformBounds.getWidth()))) * visibleDur;
        viewStartSec = std::clamp (panStartViewSec - deltaSec, 0.0, std::max (0.0, totalDur - visibleDur));
        repaint();
        return;
    }

    const int deltaX = e.x - dragStartX;

    if (isOffsettingGrid)
    {
        const double deltaSec = (static_cast<double> (deltaX) / static_cast<double> (std::max (1, waveformBounds.getWidth()))) * visibleDur;
        metronomeOffsetSec = dragStartOffsetSec + deltaSec;
        processor.getMetronome().setGridOffset (metronomeOffsetSec);
        repaint();
    }
    else if (isDraggingPlayhead && processor.getBackingTrackPlayer().hasTrack())
    {
        const float norm = std::clamp ((e.x - waveformBounds.getX()) / static_cast<float> (waveformBounds.getWidth()), 0.0f, 1.0f);
        const double targetSec = std::clamp (viewStartSec + static_cast<double> (norm) * visibleDur, 0.0, totalDur);
        processor.getBackingTrackPlayer().scrubTo (targetSec);
        repaint();
    }
}

void RecordPracticeView::mouseUp (const juce::MouseEvent& /*e*/)
{
    if (isPanningView)
    {
        isPanningView = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }
    isDraggingPlayhead = false;
    isOffsettingGrid = false;
    isDraggingTimeline = false;
}

void RecordPracticeView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (timelineBounds.contains (e.getPosition()))
    {
        metronomeOffsetSec = 0.0;
        processor.getMetronome().setGridOffset (0.0);
        repaint();
    }
}

void RecordPracticeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (waveformBounds.contains (e.getPosition()) || timelineBounds.contains (e.getPosition()))
    {
        const double totalDur = std::max (0.01, processor.getBackingTrackPlayer().getDurationSeconds());
        const double visibleDur = totalDur / std::max (1.0f, zoomFactor);
        const float mouseNorm = std::clamp ((e.x - waveformBounds.getX()) / static_cast<float> (waveformBounds.getWidth()), 0.0f, 1.0f);
        const double mouseTime = viewStartSec + static_cast<double> (mouseNorm) * visibleDur;

        const float oldZoom = zoomFactor;
        if (wheel.deltaY > 0.0f)
            zoomFactor = std::min (32.0f, zoomFactor * 1.25f);
        else if (wheel.deltaY < 0.0f)
            zoomFactor = std::max (1.0f, zoomFactor / 1.25f);

        if (std::abs (zoomFactor - oldZoom) > 1e-4f)
        {
            const double newVisibleDur = totalDur / static_cast<double> (zoomFactor);
            viewStartSec = std::clamp (mouseTime - static_cast<double> (mouseNorm) * newVisibleDur, 0.0, std::max (0.0, totalDur - newVisibleDur));
            repaint();
        }
    }
}

//==============================================================================
void RecordPracticeView::resized()
{
    auto area = getLocalBounds().reduced (12);

    // Top: Backing Track Deck (42% height)
    auto topArea = area.removeFromTop (static_cast<int> (area.getHeight() * 0.44));
    area.removeFromTop (10);

    // Bottom: 6-Lane Drum Rhythm Ribbon (20% height)
    auto bottomArea = area.removeFromBottom (static_cast<int> (area.getHeight() * 0.36));
    area.removeFromBottom (10);

    // Middle: Split into Practice/Metronome (left 50%) and Recording Console (right 50%)
    auto leftConsole = area.removeFromLeft (area.getWidth() / 2 - 6);
    area.removeFromLeft (12);
    auto rightConsole = area;

    // --- Layout Top Area (Backing Track Deck) ---
    auto deckInner = topArea.reduced (10);
    auto topRow = deckInner.removeFromTop (32);
    loadTrackButton.setBounds (topRow.removeFromLeft (100));
    topRow.removeFromLeft (6);
    clearTrackButton.setBounds (topRow.removeFromLeft (90));
    topRow.removeFromLeft (8);
    playButton.setBounds (topRow.removeFromLeft (65));
    topRow.removeFromLeft (6);
    pauseButton.setBounds (topRow.removeFromLeft (65));
    topRow.removeFromLeft (6);
    rewindButton.setBounds (topRow.removeFromLeft (70));
    topRow.removeFromLeft (6);
    loopButton.setBounds (topRow.removeFromLeft (85));
    topRow.removeFromLeft (6);
    muteTrackButton.setBounds (topRow.removeFromLeft (55));

    // Right side of top row: Volume knob
    if (trackVolumeKnob != nullptr)
        trackVolumeKnob->setBounds (topRow.removeFromRight (55));

    deckInner.removeFromTop (8);
    auto bpmRow = deckInner.removeFromBottom (28);
    detectBpmButton.setBounds (bpmRow.removeFromLeft (105));
    bpmRow.removeFromLeft (6);
    tapBpmButton.setBounds (bpmRow.removeFromLeft (85));
    bpmRow.removeFromLeft (6);
    alignBeatOneButton.setBounds (bpmRow.removeFromLeft (95));
    bpmRow.removeFromLeft (6);
    halfBpmButton.setBounds (bpmRow.removeFromLeft (34));
    bpmRow.removeFromLeft (4);
    doubleBpmButton.setBounds (bpmRow.removeFromLeft (34));
    bpmRow.removeFromLeft (6);
    nudgeDownButton.setBounds (bpmRow.removeFromLeft (40));
    bpmRow.removeFromLeft (4);
    nudgeUpButton.setBounds (bpmRow.removeFromLeft (40));

    // Right side of bpmRow: editable BPM field
    bpmEditor.setBounds (bpmRow.removeFromRight (68).reduced (0, 1));

    deckInner.removeFromBottom (4);
    timelineBounds = deckInner.removeFromBottom (24);
    deckInner.removeFromBottom (2);
    waveformBounds = deckInner;

    // --- Layout Left Console (Practice & Metronome) ---
    auto leftInner = leftConsole.reduced (12);
    leftInner.removeFromTop (26); // Title space
    auto mRow1 = leftInner.removeFromTop (30);
    metroToggleButton.setBounds (mRow1.removeFromLeft (130));
    mRow1.removeFromLeft (8);
    countInCombo.setBounds (mRow1);

    leftInner.removeFromTop (8);
    auto mRow2 = leftInner.removeFromTop (30);
    clickSoundCombo.setBounds (mRow2.removeFromLeft (180));
    if (clickVolumeKnob != nullptr)
        clickVolumeKnob->setBounds (mRow2.removeFromRight (55));

    // --- Layout Right Console (Dual Recording) ---
    auto rightInner = rightConsole.reduced (12);
    rightInner.removeFromTop (26); // Title space
    auto rRow1 = rightInner.removeFromTop (36);
    recordButton.setBounds (rRow1.removeFromLeft (120));
    rRow1.removeFromLeft (8);
    stopButton.setBounds (rRow1.removeFromLeft (75));
    rRow1.removeFromLeft (8);
    recordSourceCombo.setBounds (rRow1);

    rightInner.removeFromTop (10);
    auto rRow2 = rightInner.removeFromTop (30);
    exportWavButton.setBounds (rRow2.removeFromLeft (130));
    rRow2.removeFromLeft (8);
    exportMidButton.setBounds (rRow2.removeFromLeft (130));
}

//==============================================================================
void RecordPracticeView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme.background);

    auto area = getLocalBounds().reduced (12);
    auto topArea = area.removeFromTop (static_cast<int> (area.getHeight() * 0.44));
    area.removeFromTop (10);

    auto bottomArea = area.removeFromBottom (static_cast<int> (area.getHeight() * 0.36));
    area.removeFromBottom (10);

    auto leftConsole = area.removeFromLeft (area.getWidth() / 2 - 6);
    area.removeFromLeft (12);
    auto rightConsole = area;

    drawWaveformDeck (g, topArea);
    drawPracticeConsole (g, leftConsole);
    drawRecordingConsole (g, rightConsole);
    drawRhythmRibbon (g, bottomArea);
}

void RecordPracticeView::drawWaveformDeck (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (currentTheme.panelEdge);
    g.drawRoundedRectangle (area.toFloat(), 8.0f, 2.0f);

    const auto blueCol = juce::Colour (0xff3498db);

    // Waveform box
    if (! waveformBounds.isEmpty())
    {
        g.setColour (currentTheme.background);
        g.fillRect (waveformBounds);
        g.setColour (currentTheme.panelEdge.withAlpha (0.4f));
        g.drawRect (waveformBounds, 1);

        auto& trackPlayer = processor.getBackingTrackPlayer();
        const double totalDur = std::max (0.01, trackPlayer.getDurationSeconds());
        const double visibleDur = totalDur / std::max (1.0f, zoomFactor);
        const int w = waveformBounds.getWidth();
        const int h = waveformBounds.getHeight();
        const int midY = waveformBounds.getCentreY();

        auto timeToX = [&](double tSec) -> float
        {
            if (visibleDur <= 0.0) return static_cast<float> (waveformBounds.getX());
            const float norm = static_cast<float> ((tSec - viewStartSec) / visibleDur);
            return static_cast<float> (waveformBounds.getX()) + norm * static_cast<float> (w);
        };

        if (trackPlayer.hasTrack())
        {
            const auto& buf = trackPlayer.getDecodedBuffer();
            const int totalSamples = buf.getNumSamples();
            const float* l = buf.getReadPointer (0);

            // Draw audio waveform bars
            g.setColour (blueCol);
            const double samplesPerSec = totalDur > 0.0 ? (static_cast<double> (totalSamples) / totalDur) : 44100.0;
            const double visibleSamples = visibleDur * samplesPerSec;
            const double samplesPerPixel = std::max (1.0, visibleSamples / std::max (1, w));
            const int64_t startSample = static_cast<int64_t> (viewStartSec * samplesPerSec);

            for (int x = 0; x < w; ++x)
            {
                const int64_t sStart = startSample + static_cast<int64_t> (x * samplesPerPixel);
                const int64_t sEnd = std::min (static_cast<int64_t> (totalSamples), sStart + std::max (static_cast<int64_t> (1), static_cast<int64_t> (samplesPerPixel)));

                if (sStart < totalSamples && sStart >= 0)
                {
                    float maxVal = 0.0f;
                    const int step = std::max (1, static_cast<int> (sEnd - sStart) / 16);
                    for (int64_t s = sStart; s < sEnd; s += step)
                        maxVal = std::max (maxVal, std::abs (l[s]));

                    const int barH = static_cast<int> (maxVal * static_cast<float> (h / 2));
                    g.drawLine (static_cast<float> (waveformBounds.getX() + x),
                                static_cast<float> (midY - barH),
                                static_cast<float> (waveformBounds.getX() + x),
                                static_cast<float> (midY + barH),
                                1.5f);
                }
            }

            // Draw vertical beat grid guidelines through waveform
            const double secPerBeat = 60.0 / std::max (20.0, detectedBpm);
            const int64_t kStart = static_cast<int64_t> (std::floor ((viewStartSec - metronomeOffsetSec) / secPerBeat));
            const int64_t kEnd = static_cast<int64_t> (std::ceil ((viewStartSec + visibleDur - metronomeOffsetSec) / secPerBeat));

            for (int64_t k = kStart; k <= kEnd; ++k)
            {
                const double tBeat = metronomeOffsetSec + static_cast<double> (k) * secPerBeat;
                const float bx = timeToX (tBeat);
                if (bx >= static_cast<float> (waveformBounds.getX()) && bx <= static_cast<float> (waveformBounds.getRight()))
                {
                    const int mBeat = static_cast<int> (((k % 4) + 4) % 4) + 1;
                    if (mBeat == 1)
                    {
                        // Beat 1: thicker guideline in accent colour
                        g.setColour (currentTheme.accent.withAlpha (0.42f));
                        g.drawLine (bx, static_cast<float> (waveformBounds.getY()),
                                    bx, static_cast<float> (waveformBounds.getBottom()), 2.0f);
                    }
                    else
                    {
                        // Other beats: thinner guideline
                        g.setColour (currentTheme.inkSoft.withAlpha (0.16f));
                        g.drawLine (bx, static_cast<float> (waveformBounds.getY()),
                                    bx, static_cast<float> (waveformBounds.getBottom()), 1.0f);
                    }
                }
            }

            // Playhead
            const double curSec = trackPlayer.getPlayheadSeconds();
            const float playheadX = timeToX (curSec);

            if (playheadX >= static_cast<float> (waveformBounds.getX()) && playheadX <= static_cast<float> (waveformBounds.getRight()))
            {
                g.setColour (currentTheme.accent);
                g.drawLine (playheadX, static_cast<float> (waveformBounds.getY()),
                            playheadX, static_cast<float> (waveformBounds.getBottom()), 2.5f);

                juce::Path cap;
                cap.addTriangle (playheadX - 5.0f, static_cast<float> (waveformBounds.getY()),
                                 playheadX + 5.0f, static_cast<float> (waveformBounds.getY()),
                                 playheadX, static_cast<float> (waveformBounds.getY() + 7.0f));
                g.fillPath (cap);
            }

            // Time badge
            g.setFont (displayFont (13.0f));
            g.setColour (currentTheme.ink);
            const auto timeStr = juce::String::formatted ("%02d:%02d / %02d:%02d",
                static_cast<int> (curSec) / 60, static_cast<int> (curSec) % 60,
                static_cast<int> (totalDur) / 60, static_cast<int> (totalDur) % 60);
            g.drawText (timeStr, waveformBounds.reduced (6), juce::Justification::topRight);

            juce::String trackDisplay = trackPlayer.getTrackName();
            if (zoomFactor > 1.05f)
                trackDisplay += juce::String::formatted ("  [Zoom: %.1fx]", zoomFactor);
            g.drawText (trackDisplay, waveformBounds.reduced (6), juce::Justification::topLeft);
        }
        else
        {
            g.setFont (uiFont (16.0f));
            g.setColour (currentTheme.ink.withAlpha (0.6f));
            g.drawText ("Drag & Drop WAV / MP3 / FLAC Backing Track Here", waveformBounds, juce::Justification::centred);
        }

        // Draw synchronized beat timeline bar
        drawBeatTimelineBar (g, timelineBounds);

        // Label beside bpmEditor
        if (! bpmEditor.getBounds().isEmpty())
        {
            g.setFont (displayFont (13.0f));
            g.setColour (blueCol);
            juce::String bpmInfo = "BPM:";
            if (isAnalyzing)
                bpmInfo = "Analyzing...";
            else if (bpmConfidence > 0.0f)
                bpmInfo = juce::String::formatted ("(%.0f%%) BPM:", bpmConfidence * 100.0f);

            auto labelArea = bpmEditor.getBounds().translated (-105, 0).withWidth (100);
            g.drawText (bpmInfo, labelArea, juce::Justification::centredRight);
        }
    }
}

void RecordPracticeView::drawBeatTimelineBar (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    g.setColour (currentTheme.background.interpolatedWith (currentTheme.panel, 0.45f));
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    g.setColour (currentTheme.panelEdge.withAlpha (0.55f));
    g.drawRoundedRectangle (area.toFloat(), 3.0f, 1.2f);

    const double totalDur = std::max (0.01, processor.getBackingTrackPlayer().getDurationSeconds());
    const double visibleDur = totalDur / std::max (1.0f, zoomFactor);
    const int w = area.getWidth();

    auto timeToX = [&](double tSec) -> float
    {
        if (visibleDur <= 0.0) return static_cast<float> (area.getX());
        const float norm = static_cast<float> ((tSec - viewStartSec) / visibleDur);
        return static_cast<float> (area.getX()) + norm * static_cast<float> (w);
    };

    const double secPerBeat = 60.0 / std::max (20.0, detectedBpm);
    const int64_t kStart = static_cast<int64_t> (std::floor ((viewStartSec - metronomeOffsetSec) / secPerBeat));
    const int64_t kEnd = static_cast<int64_t> (std::ceil ((viewStartSec + visibleDur - metronomeOffsetSec) / secPerBeat));

    for (int64_t k = kStart; k <= kEnd; ++k)
    {
        const double tBeat = metronomeOffsetSec + static_cast<double> (k) * secPerBeat;
        const float bx = timeToX (tBeat);
        if (bx >= static_cast<float> (area.getX()) && bx <= static_cast<float> (area.getRight()))
        {
            const int mBeat = static_cast<int> (((k % 4) + 4) % 4) + 1;
            const int barNum = static_cast<int> (std::floor (static_cast<double> (k) / 4.0)) + 1;

            if (mBeat == 1)
            {
                // Thicker line for Beat 1 (downbeat/bar line)
                g.setColour (currentTheme.accent);
                g.drawLine (bx, static_cast<float> (area.getY()),
                            bx, static_cast<float> (area.getBottom()), 2.2f);

                // Bar number label
                g.setFont (displayFont (11.0f).boldened());
                g.drawText (juce::String (barNum),
                            juce::Rectangle<float> (bx + 3.0f, static_cast<float> (area.getY() + 1), 32.0f, static_cast<float> (area.getHeight() - 2)),
                            juce::Justification::centredLeft);
            }
            else
            {
                // Thinner line for each other beat
                g.setColour (currentTheme.inkSoft.withAlpha (0.65f));
                g.drawLine (bx, static_cast<float> (area.getY() + 6),
                            bx, static_cast<float> (area.getBottom() - 2), 1.0f);

                g.setFont (uiFont (9.0f));
                g.drawText (juce::String (mBeat),
                            juce::Rectangle<float> (bx + 2.0f, static_cast<float> (area.getY() + 6), 16.0f, static_cast<float> (area.getHeight() - 8)),
                            juce::Justification::centredLeft);
            }
        }
    }

    // Grid offset badge on the right
    g.setFont (displayFont (11.0f));
    g.setColour (currentTheme.accent);
    const double ms = metronomeOffsetSec * 1000.0;
    juce::String offText = juce::String::formatted ("Grid: %+.1f ms  (Shift+Drag / 'A' Align)", ms);
    g.drawText (offText, area.reduced (6, 2), juce::Justification::centredRight);
}

void RecordPracticeView::drawPracticeConsole (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (currentTheme.panelEdge);
    g.drawRoundedRectangle (area.toFloat(), 8.0f, 2.0f);

    // Section title
    g.setFont (displayFont (15.0f));
    g.setColour (currentTheme.ink);
    g.drawText ("PRACTICE & METRONOME", area.reduced (12, 8), juce::Justification::topLeft);

    // Flashing Beat Counter (1 - 2 - 3 - 4)
    auto beatArea = area.removeFromBottom (45).reduced (12, 0);
    const int beatW = beatArea.getWidth() / 4;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const bool isFlashing = (now - lastFlashTime) < 0.18;

    for (int b = 1; b <= 4; ++b)
    {
        auto circleBounds = juce::Rectangle<float> (
            static_cast<float> (beatArea.getX() + (b - 1) * beatW + beatW / 2 - 14),
            static_cast<float> (beatArea.getCentreY() - 14),
            28.0f, 28.0f);

        const bool active = isFlashing && (currentBeatFlash == b);
        if (active)
        {
            g.setColour (isDownbeatFlash ? currentTheme.warning : currentTheme.accent);
            g.fillEllipse (circleBounds);
            g.setColour (currentTheme.panelEdge);
            g.drawEllipse (circleBounds, 2.5f);
            g.setColour (juce::Colours::white);
        }
        else
        {
            g.setColour (currentTheme.panelEdge.withAlpha (0.15f));
            g.fillEllipse (circleBounds);
            g.setColour (currentTheme.panelEdge.withAlpha (0.5f));
            g.drawEllipse (circleBounds, 1.5f);
            g.setColour (currentTheme.ink.withAlpha (0.6f));
        }

        g.setFont (displayFont (14.0f));
        g.drawText (juce::String (b), circleBounds, juce::Justification::centred);
    }
}

void RecordPracticeView::drawRecordingConsole (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (currentTheme.panelEdge);
    g.drawRoundedRectangle (area.toFloat(), 8.0f, 2.0f);

    // Section title
    g.setFont (displayFont (15.0f));
    g.setColour (currentTheme.ink);
    g.drawText ("DUAL RECORDING CONSOLE", area.reduced (12, 8), juce::Justification::topLeft);

    // Status readout
    auto statusArea = area.removeFromBottom (45).reduced (12, 0);
    juce::String statusStr = "STATUS: IDLE";
    juce::Colour statusCol = currentTheme.ink.withAlpha (0.6f);

    if (processor.getMetronome().isCountingIn())
    {
        statusStr = juce::String::formatted ("COUNT-IN: %d BEATS REMAINING",
            processor.getMetronome().getBeatsRemainingInCountIn());
        statusCol = currentTheme.accent;
    }
    else if (processor.getAudioRecorder().isRecording())
    {
        const double sec = processor.getAudioRecorder().getRecordedSeconds();
        statusStr = juce::String::formatted ("RECORDING (%02d:%02d)",
            static_cast<int> (sec) / 60, static_cast<int> (sec) % 60);
        statusCol = currentTheme.warning;
    }
    else if (isRecArmed)
    {
        statusStr = "REC ARMED (Press Play or Space)";
        statusCol = juce::Colour (0xffe74c3c);
    }

    g.setFont (displayFont (14.0f));
    g.setColour (statusCol);
    g.drawText (statusStr, statusArea, juce::Justification::centredLeft);

    const int midiEvents = processor.getMidiRecorder().getEventCount();
    g.setFont (uiFont (14.0f));
    g.setColour (currentTheme.ink.withAlpha (0.7f));
    g.drawText (juce::String::formatted ("MIDI Events: %d", midiEvents), statusArea, juce::Justification::centredRight);
}

void RecordPracticeView::drawRhythmRibbon (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    g.setColour (currentTheme.panelEdge);
    g.drawRoundedRectangle (area.toFloat(), 8.0f, 2.0f);

    auto inner = area.reduced (10);
    const int numLanes = 6;
    const float laneH = static_cast<float> (inner.getHeight()) / static_cast<float> (numLanes);
    const char* laneNames[] = { "KCK", "SNR", "HH", "TOM", "CYM", "RIDE" };
    const juce::Colour laneColours[] = {
        currentTheme.accent,
        juce::Colour (0xff3498db),
        juce::Colour (0xff2ecc71),
        currentTheme.accent.withAlpha (0.7f),
        currentTheme.warning,
        juce::Colour (0xff9b59b6)
    };

    for (int i = 0; i < numLanes; ++i)
    {
        const float y = static_cast<float> (inner.getY()) + static_cast<float> (i) * laneH;
        g.setColour (currentTheme.panelEdge.withAlpha (0.15f));
        g.drawLine (static_cast<float> (inner.getX()), y, static_cast<float> (inner.getRight()), y, 1.0f);

        // Lane label
        g.setFont (displayFont (12.0f));
        g.setColour (currentTheme.ink.withAlpha (0.6f));
        g.drawText (laneNames[i], static_cast<float> (inner.getX()), y, 36.0f, laneH, juce::Justification::centredLeft);
    }

    // Draw active moving note capsules
    const float noteStartX = static_cast<float> (inner.getX() + 40);
    const float noteTrackW = static_cast<float> (inner.getWidth() - 45);

    for (const auto& note : activeRibbonNotes)
    {
        if (note.lane >= 0 && note.lane < numLanes)
        {
            const float x = noteStartX + note.position * noteTrackW;
            const float y = static_cast<float> (inner.getY()) + static_cast<float> (note.lane) * laneH + 2.0f;
            const float w = 18.0f * note.velocity + 6.0f;
            const float h = laneH - 4.0f;

            g.setColour (laneColours[note.lane]);
            g.fillRoundedRectangle (x, y, w, h, 3.0f);
            g.setColour (currentTheme.panelEdge);
            g.drawRoundedRectangle (x, y, w, h, 3.0f, 1.2f);
        }
    }
}

} // namespace glorp
