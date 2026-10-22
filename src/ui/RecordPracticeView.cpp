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
        updateButtonStyles();
    };

    addAndMakeVisible (countInToggleButton);
    countInToggleButton.onClick = [this]()
    {
        auto cur = processor.getMetronome().getCountInMode();
        if (cur == Metronome::CountInMode::off)
            processor.getMetronome().setCountInMode (Metronome::CountInMode::oneBar);
        else
            processor.getMetronome().setCountInMode (Metronome::CountInMode::off);

        updateCountInButtons();
    };

    addAndMakeVisible (countIn1BarButton);
    countIn1BarButton.onClick = [this]()
    {
        if (processor.getMetronome().getCountInMode() != Metronome::CountInMode::off)
        {
            processor.getMetronome().setCountInMode (Metronome::CountInMode::oneBar);
            updateCountInButtons();
        }
    };

    addAndMakeVisible (countIn2BarsButton);
    countIn2BarsButton.onClick = [this]()
    {
        if (processor.getMetronome().getCountInMode() != Metronome::CountInMode::off)
        {
            processor.getMetronome().setCountInMode (Metronome::CountInMode::twoBars);
            updateCountInButtons();
        }
    };

    addAndMakeVisible (clickPresetButton);
    clickPresetButton.onClick = [this]()
    {
        juce::PopupMenu m;
        const char* names[] = {
            "Woodblock", "Studio Click", "Side Stick", "Cowbell",
            "Hi-Hat Pedal", "Digital Beep", "Clave", "Triangle"
        };
        const auto curSound = static_cast<int> (processor.getMetronome().getClickSound());
        for (int i = 0; i < 8; ++i)
            m.addItem (i + 1, names[i], true, i == curSound);

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clickPresetButton),
            [this, names](int result)
            {
                if (result >= 1 && result <= 8)
                {
                    auto sound = static_cast<Metronome::ClickSound> (result - 1);
                    processor.getMetronome().setClickSound (sound);
                    clickPresetButton.setLabel (juce::String ("Click Preset: ") + names[result - 1]);
                    repaint();
                }
            });
    };

    beatsPerMeasure = processor.getMetronome().getTimeSignatureNumerator();
    addAndMakeVisible (beatsEditor);
    beatsEditor.setText (juce::String (beatsPerMeasure), false);
    beatsEditor.setJustification (juce::Justification::centred);
    beatsEditor.setInputRestrictions (2, "0123456789");
    beatsEditor.onReturnKey = [this]()
    {
        int val = std::clamp (beatsEditor.getText().getIntValue(), 1, 16);
        beatsPerMeasure = val;
        beatsEditor.setText (juce::String (val), false);
        processor.getMetronome().setTimeSignature (beatsPerMeasure, 4);
        resized();
        repaint();
    };
    beatsEditor.onFocusLost = [this]()
    {
        int val = std::clamp (beatsEditor.getText().getIntValue(), 1, 16);
        beatsPerMeasure = val;
        beatsEditor.setText (juce::String (val), false);
        processor.getMetronome().setTimeSignature (beatsPerMeasure, 4);
        resized();
        repaint();
    };

    clickVolumeKnob = std::make_unique<Knob> ("Click Vol.", 0.0f, 2.5f, 1.0f);
    addAndMakeVisible (*clickVolumeKnob);
    clickVolumeKnob->onValueChanged = [this](float val)
    {
        processor.getMetronome().setLevel (val);
    };

    // Recording controls
    addAndMakeVisible (recordButton);
    recordButton.setFontSize (20.0f);
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
    stopButton.setFontSize (18.0f);
    stopButton.onClick = [this]()
    {
        stopTransportAndRecording();
    };

    addAndMakeVisible (alsoRecordTrackButton);
    alsoRecordTrackButton.setFontSize (14.0f);
    alsoRecordTrackButton.onClick = [this]()
    {
        alsoRecordBackingTrack = ! alsoRecordBackingTrack;
        processor.getAudioRecorder().setSource (alsoRecordBackingTrack
            ? AudioRecorder::RecordingSource::fullMix
            : AudioRecorder::RecordingSource::drumsOnly);
        updateButtonStyles();
        repaint();
    };

    addAndMakeVisible (exportWavButton);
    exportWavButton.setFontSize (14.0f);
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
    exportMidButton.setFontSize (14.0f);
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
        activeBeatFlash = beat;
        isDownbeatFlash = isDownbeat;
        lastBeatFlashTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
        juce::MessageManager::callAsync ([this] { repaint(); });
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

void RecordPracticeView::setBeatsPerMeasure (int beats)
{
    beatsPerMeasure = std::clamp (beats, 1, 16);
    beatsEditor.setText (juce::String (beatsPerMeasure), false);
    processor.getMetronome().setTimeSignature (beatsPerMeasure, 4);
    resized();
    repaint();
}

void RecordPracticeView::triggerBeatFlash (int beat, bool isDownbeat)
{
    activeBeatFlash = beat;
    isDownbeatFlash = isDownbeat;
    lastBeatFlashTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    repaint();
}

void RecordPracticeView::setAlsoRecordBackingTrack (bool alsoRecord)
{
    alsoRecordBackingTrack = alsoRecord;
    processor.getAudioRecorder().setSource (alsoRecordBackingTrack
        ? AudioRecorder::RecordingSource::fullMix
        : AudioRecorder::RecordingSource::drumsOnly);
    updateButtonStyles();
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

    const auto mintGreen = juce::Colour (0xffa8e6cf);
    const auto skyBlue   = juce::Colour (0xff89d4f7);
    const auto tanCol    = juce::Colour (0xfff8d7b3);
    const auto coralRed  = juce::Colour (0xffe27771);

    styleBtn (loadTrackButton, currentTheme.panel);
    styleBtn (clearTrackButton, currentTheme.panel);
    styleBtn (playButton, mintGreen);
    styleBtn (pauseButton, currentTheme.panel);
    styleBtn (rewindButton, currentTheme.panel);
    styleBtn (loopButton, currentTheme.panel);
    styleBtn (muteTrackButton, currentTheme.panel);

    styleBtn (detectBpmButton, skyBlue);
    styleBtn (tapBpmButton, currentTheme.panel);
    styleBtn (alignBeatOneButton, currentTheme.panel);
    styleBtn (halfBpmButton, currentTheme.panel);
    styleBtn (doubleBpmButton, currentTheme.panel);
    styleBtn (nudgeDownButton, currentTheme.panel);
    styleBtn (nudgeUpButton, currentTheme.panel);

    // Metronome
    const bool isMetroOn = processor.getMetronome().isEnabled();
    metroToggleButton.setLabel (isMetroOn ? "Metronome: ON" : "Metronome: OFF");
    styleBtn (metroToggleButton, isMetroOn ? tanCol : currentTheme.panel);

    clickPresetButton.setTheme (currentTheme);
    clickPresetButton.setBaseColour (currentTheme.panel);

    updateCountInButtons();

    // Dual Recording Console
    styleBtn (recordButton, coralRed);
    styleBtn (stopButton, currentTheme.panel);

    styleBtn (alsoRecordTrackButton, alsoRecordBackingTrack ? juce::Colour (0xff54c0f8) : currentTheme.panel);

    styleBtn (exportWavButton, mintGreen);
    styleBtn (exportMidButton, tanCol);

    beatsEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    beatsEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xff1a1a1a));
    beatsEditor.setColour (juce::TextEditor::outlineColourId, currentTheme.panelEdge);
    beatsEditor.setColour (juce::TextEditor::focusedOutlineColourId, skyBlue);
    beatsEditor.setFont (displayFont (16.0f).boldened());
    beatsEditor.applyColourToAllText (juce::Colour (0xff1a1a1a));
    beatsEditor.applyFontToAllText (displayFont (16.0f).boldened());

    bpmEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    bpmEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xff1a1a1a));
    bpmEditor.setColour (juce::TextEditor::highlightedTextColourId, currentTheme.panel);
    bpmEditor.setColour (juce::TextEditor::highlightColourId, currentTheme.accent);
    bpmEditor.setColour (juce::TextEditor::outlineColourId, currentTheme.panelEdge);
    bpmEditor.setColour (juce::TextEditor::focusedOutlineColourId, currentTheme.accent);
    bpmEditor.setFont (uiFont (14.0f).boldened());
    bpmEditor.applyColourToAllText (juce::Colour (0xff1a1a1a));
    bpmEditor.applyFontToAllText (uiFont (14.0f).boldened());
}

void RecordPracticeView::updateCountInButtons()
{
    const auto mode = processor.getMetronome().getCountInMode();
    const bool isCountInOn = (mode != Metronome::CountInMode::off);

    const auto mintGreen = juce::Colour (0xffa8e6cf);
    const auto skyBlue   = juce::Colour (0xff89d4f7);

    countInToggleButton.setTheme (currentTheme);
    countInToggleButton.setLabel (isCountInOn ? "Count In: ON" : "Count In: OFF");
    countInToggleButton.setBaseColour (isCountInOn ? mintGreen : currentTheme.panel);

    countIn1BarButton.setTheme (currentTheme);
    countIn2BarsButton.setTheme (currentTheme);

    countIn1BarButton.setDisabledStyle (! isCountInOn);
    countIn2BarsButton.setDisabledStyle (! isCountInOn);

    if (! isCountInOn)
    {
        countIn1BarButton.setBaseColour (currentTheme.panel);
        countIn2BarsButton.setBaseColour (currentTheme.panel);
    }
    else
    {
        countIn1BarButton.setBaseColour (mode == Metronome::CountInMode::oneBar ? skyBlue : currentTheme.panel);
        countIn2BarsButton.setBaseColour (mode == Metronome::CountInMode::twoBars ? skyBlue : currentTheme.panel);
    }

    repaint();
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
    if (! isVisible())
        return;

    bool needsRepaint = false;

    // Auto-follow playhead when zoomed in and playing
    if (processor.getBackingTrackPlayer().isPlaying())
    {
        needsRepaint = true;

        if (zoomFactor > 1.05f && ! isPanningView && ! isDraggingPlayhead && ! isOffsettingGrid)
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
    }

    // Reset beat flash after aura decay (~0.35s)
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (activeBeatFlash > 0)
    {
        needsRepaint = true;
        if ((now - lastBeatFlashTime) > 0.35)
            activeBeatFlash = 0;
    }

    if (processor.getMetronome().isCountingIn() || isAnalyzing)
        needsRepaint = true;

    const bool isRec = processor.getAudioRecorder().isRecording();
    if (isRecArmed || isRec)
    {
        needsRepaint = true;
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

    if (needsRepaint)
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

    // Top: Backing Track Deck (52% height)
    const int deckH = static_cast<int> (area.getHeight() * 0.52);
    auto topArea = area.removeFromTop (deckH);
    area.removeFromTop (10);

    // Bottom: Split into Practice/Metronome (left 50%) and Recording Console (right 50%)
    auto leftConsole = area.removeFromLeft (area.getWidth() / 2 - 6);
    area.removeFromLeft (12);
    auto rightConsole = area;

    // --- Layout Top Area (Backing Track Deck) ---
    auto deckInner = topArea.reduced (10);
    auto topRow = deckInner.removeFromTop (54);
    auto btnRow = topRow.reduced (0, 11);
    loadTrackButton.setBounds (btnRow.removeFromLeft (95));
    btnRow.removeFromLeft (6);
    clearTrackButton.setBounds (btnRow.removeFromLeft (85));
    btnRow.removeFromLeft (6);
    playButton.setBounds (btnRow.removeFromLeft (60));
    btnRow.removeFromLeft (6);
    pauseButton.setBounds (btnRow.removeFromLeft (60));
    btnRow.removeFromLeft (6);
    rewindButton.setBounds (btnRow.removeFromLeft (65));
    btnRow.removeFromLeft (6);
    loopButton.setBounds (btnRow.removeFromLeft (80));
    btnRow.removeFromLeft (6);
    muteTrackButton.setBounds (btnRow.removeFromLeft (52));

    // Right side of top row: Large Track Volume knob
    if (trackVolumeKnob != nullptr)
        trackVolumeKnob->setBounds (topRow.removeFromRight (56).withHeight (54));

    deckInner.removeFromTop (4);
    auto bpmRow = deckInner.removeFromBottom (28);
    detectBpmButton.setBounds (bpmRow.removeFromLeft (95));
    bpmRow.removeFromLeft (6);
    tapBpmButton.setBounds (bpmRow.removeFromLeft (85));
    bpmRow.removeFromLeft (6);
    alignBeatOneButton.setBounds (bpmRow.removeFromLeft (90));
    bpmRow.removeFromLeft (6);
    halfBpmButton.setBounds (bpmRow.removeFromLeft (32));
    bpmRow.removeFromLeft (4);
    doubleBpmButton.setBounds (bpmRow.removeFromLeft (32));
    bpmRow.removeFromLeft (6);
    nudgeDownButton.setBounds (bpmRow.removeFromLeft (36));
    bpmRow.removeFromLeft (4);
    nudgeUpButton.setBounds (bpmRow.removeFromLeft (36));

    // Right side of bpmRow: editable BPM field
    bpmEditor.setBounds (bpmRow.removeFromRight (60).reduced (0, 1));

    deckInner.removeFromBottom (4);
    timelineBounds = deckInner.removeFromBottom (24);
    deckInner.removeFromBottom (2);
    waveformBounds = deckInner;

    // --- Layout Left Console (Practice & Metronome) ---
    auto leftInner = leftConsole.reduced (12, 10);
    auto titleRow = leftInner.removeFromTop (54);
    if (clickVolumeKnob != nullptr)
        clickVolumeKnob->setBounds (titleRow.removeFromRight (56).withHeight (54));

    leftInner.removeFromTop (6);

    // Row 1: Metronome toggle + Count-In toggle + 1 bar + 2 bars
    auto mRow1 = leftInner.removeFromTop (32);
    metroToggleButton.setBounds (mRow1.removeFromLeft (130));
    mRow1.removeFromLeft (6);
    countInToggleButton.setBounds (mRow1.removeFromLeft (115));
    mRow1.removeFromLeft (6);
    countIn1BarButton.setBounds (mRow1.removeFromLeft (50));
    mRow1.removeFromLeft (6);
    countIn2BarsButton.setBounds (mRow1.removeFromLeft (58));

    leftInner.removeFromTop (6);
    // Row 2: Click Preset
    auto mRow2 = leftInner.removeFromTop (32);
    clickPresetButton.setBounds (mRow2);

    leftInner.removeFromTop (6);
    // Row 3: Beats per measure
    auto mRow3 = leftInner.removeFromTop (26);
    beatsEditor.setBounds (leftInner.getX() + 152, mRow3.getY(), 36, 24);

    leftInner.removeFromTop (6);
    // Row 4: Beat lights
    auto lightsArea = leftInner.removeFromTop (46);
    beatLightBounds.clear();
    if (beatsPerMeasure > 0)
    {
        const int spacing = 6;
        const int totalSpacing = spacing * (beatsPerMeasure - 1);
        int cardW = (lightsArea.getWidth() - totalSpacing) / beatsPerMeasure;
        cardW = std::clamp (cardW, 24, 68);
        int curX = lightsArea.getX();
        for (int i = 0; i < beatsPerMeasure; ++i)
        {
            beatLightBounds.push_back ({ curX, lightsArea.getY(), cardW, lightsArea.getHeight() });
            curX += cardW + spacing;
        }
    }

    // --- Layout Right Console (Dual Recording) ---
    auto rightInner = rightConsole.reduced (12, 10);
    rightInner.removeFromTop (24); // Title space

    // Row 1: REC and STOP
    auto rRow1 = rightInner.removeFromTop (50);
    const int recW = static_cast<int> (rRow1.getWidth() * 0.62);
    recordButton.setBounds (rRow1.removeFromLeft (recW));
    rRow1.removeFromLeft (8);
    stopButton.setBounds (rRow1);

    rightInner.removeFromTop (8);
    // Row 2: Also record backing track
    auto rRow2 = rightInner.removeFromTop (34);
    alsoRecordTrackButton.setBounds (rRow2);

    rightInner.removeFromTop (8);
    // Row 3: Export WAV & Export MIDI
    auto rRow3 = rightInner.removeFromTop (34);
    const int expW = (rRow3.getWidth() - 8) / 2;
    exportWavButton.setBounds (rRow3.removeFromLeft (expW));
    rRow3.removeFromLeft (8);
    exportMidButton.setBounds (rRow3);
}

//==============================================================================
void RecordPracticeView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme.background);

    auto area = getLocalBounds().reduced (12);
    const int deckH = static_cast<int> (area.getHeight() * 0.52);
    auto topArea = area.removeFromTop (deckH);
    area.removeFromTop (10);

    auto leftConsole = area.removeFromLeft (area.getWidth() / 2 - 6);
    area.removeFromLeft (12);
    auto rightConsole = area;

    drawWaveformDeck (g, topArea);
    drawPracticeConsole (g, leftConsole);
    drawRecordingConsole (g, rightConsole);
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
    drawSketchRect (g, area.toFloat(), currentTheme.panelEdge, 2.0f, 101);

    // Section title
    g.setFont (displayFont (16.0f).boldened());
    g.setColour (currentTheme.ink);
    g.drawText ("PRACTICE & METRONOME", area.reduced (14, 10).removeFromTop (24), juce::Justification::topLeft);

    // "Beats per meassure:" label in bright cyan
    if (! beatsEditor.getBounds().isEmpty())
    {
        const auto cyanCol = juce::Colour (0xff0099cc);
        g.setFont (displayFont (14.0f).boldened());
        g.setColour (cyanCol);
        auto labelArea = juce::Rectangle<int> (
            area.getX() + 14,
            beatsEditor.getY() - 1,
            beatsEditor.getX() - (area.getX() + 14) - 4,
            beatsEditor.getHeight() + 2);
        g.drawText ("Beats per meassure:", labelArea, juce::Justification::centredLeft, false);
    }

    // Dynamic Beat Lights
    drawBeatLights (g);
}

void RecordPracticeView::drawBeatLights (juce::Graphics& g)
{
    for (int i = 0; i < static_cast<int> (beatLightBounds.size()); ++i)
    {
        const int b = i + 1;
        const auto card = beatLightBounds[static_cast<size_t> (i)].toFloat();
        const bool isActive = (activeBeatFlash == b);

        if (isActive && b == 1)
        {
            // Downbeat (Beat 1): radiant red glowing halo aura
            const auto redCol = juce::Colour (0xffe74c3c);
            for (int r = 8; r >= 1; --r)
            {
                const float alpha = 0.45f / static_cast<float> (r);
                g.setColour (redCol.withAlpha (alpha));
                g.drawRoundedRectangle (card.expanded (static_cast<float> (r)), 4.0f + static_cast<float> (r), 2.0f);
            }

            g.setColour (redCol);
            g.fillRoundedRectangle (card, 4.0f);
            drawSketchRect (g, card, juce::Colour (0xff962d22), 2.2f, static_cast<unsigned> (b + 20));

            g.setFont (displayFont (20.0f).boldened());
            g.setColour (juce::Colours::white);
            g.drawText (juce::String (b), card, juce::Justification::centred, false);
        }
        else if (isActive && b > 1)
        {
            // Sub-beats: golden yellow glow
            const auto goldCol = juce::Colour (0xffffd166);
            for (int r = 6; r >= 1; --r)
            {
                const float alpha = 0.35f / static_cast<float> (r);
                g.setColour (goldCol.withAlpha (alpha));
                g.drawRoundedRectangle (card.expanded (static_cast<float> (r)), 4.0f + static_cast<float> (r), 1.8f);
            }

            g.setColour (goldCol);
            g.fillRoundedRectangle (card, 4.0f);
            drawSketchRect (g, card, currentTheme.panelEdge, 2.0f, static_cast<unsigned> (b + 20));

            g.setFont (displayFont (20.0f).boldened());
            g.setColour (juce::Colour (0xff1a1a1a));
            g.drawText (juce::String (b), card, juce::Justification::centred, false);
        }
        else
        {
            // Idle card: amber orange box from mockup
            const auto amberCol = juce::Colour (0xfff5a623);
            g.setColour (amberCol);
            g.fillRoundedRectangle (card, 4.0f);
            drawSketchRect (g, card, currentTheme.panelEdge, 2.0f, static_cast<unsigned> (b + 20));

            g.setFont (displayFont (20.0f).boldened());
            g.setColour (currentTheme.ink);
            g.drawText (juce::String (b), card, juce::Justification::centred, false);
        }
    }
}

void RecordPracticeView::drawRecordingConsole (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (area.toFloat(), 8.0f);
    drawSketchRect (g, area.toFloat(), currentTheme.panelEdge, 2.0f, 202);

    // Section title
    g.setFont (displayFont (16.0f).boldened());
    g.setColour (currentTheme.ink);
    g.drawText ("DUAL RECORDING CONSOLE", area.reduced (14, 10).removeFromTop (24), juce::Justification::topLeft);

    // Status readout and MIDI events counter in bottom strip
    auto statusArea = area.removeFromBottom (34).reduced (14, 0);
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

    g.setFont (displayFont (13.0f).boldened());
    g.setColour (statusCol);
    g.drawText (statusStr, statusArea, juce::Justification::centredLeft, false);

    const int midiEvents = processor.getMidiRecorder().getEventCount();
    g.setFont (uiFont (12.0f));
    g.setColour (currentTheme.inkSoft);
    g.drawText (juce::String::formatted ("MIDI Events: %d", midiEvents), statusArea, juce::Justification::centredRight, false);
}

} // namespace glorp
