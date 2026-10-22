/*
    RecordPracticeView.cpp - Hand-drawn comic dashboard for GlorpDrums recording, setlists, and practice.
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

    // Default count-in to 1 bar so setlists and practice sessions have count-in ready
    if (processor.getMetronome().getCountInMode() == Metronome::CountInMode::off)
        processor.getMetronome().setCountInMode (Metronome::CountInMode::oneBar);

    // Sync UI whenever SetlistPlayer loads/advances to a song
    processor.getSetlistPlayer().onSongLoaded = [this](int /*newSongIndex*/, const SongEntry& song, double effectiveBpm)
    {
        auto* mm = juce::MessageManager::getInstanceWithoutCreating();
        if (mm == nullptr || mm->isThisTheMessageThread())
        {
            syncUiFromSongEntry (song, effectiveBpm);
            refreshSetlistSelectorLabel();
            rebuildSetlistRowGeometries();
            repaint();
        }
        else
        {
            juce::MessageManager::callAsync ([this, song, effectiveBpm]()
            {
                syncUiFromSongEntry (song, effectiveBpm);
                refreshSetlistSelectorLabel();
                rebuildSetlistRowGeometries();
                repaint();
            });
        }
    };

    // -- Top Deck: Backing track & Setlist transport buttons ------------------
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
                    loadTrackAndSyncDatabase (file, true);
            });
    };

    addAndMakeVisible (prevSongButton);
    prevSongButton.onClick = [this]()
    {
        const bool wasPlaying = processor.getBackingTrackPlayer().isPlaying()
                                || processor.getMetronome().isCountingIn();
        processor.getSetlistPlayer().previousSong (wasPlaying);
        repaint();
    };

    addAndMakeVisible (playButton);
    playButton.onClick = [this]() { togglePlayPause(); };

    addAndMakeVisible (pauseButton);
    pauseButton.onClick = [this]() { pauseTransport(); };

    addAndMakeVisible (nextSongButton);
    nextSongButton.onClick = [this]()
    {
        const bool wasPlaying = processor.getBackingTrackPlayer().isPlaying()
                                || processor.getMetronome().isCountingIn();
        processor.getSetlistPlayer().nextSong (wasPlaying);
        repaint();
    };

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
        loopButton.setLabel (nextState ? "Loop Song: ON" : "Loop Song: OFF");
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

    trackVolumeKnob = std::make_unique<Knob> ("Track Vol.", 0.0f, 1.5f, 0.8f);
    addAndMakeVisible (*trackVolumeKnob);
    trackVolumeKnob->onValueChanged = [this](float val)
    {
        processor.getBackingTrackPlayer().setLevel (val);
        saveCurrentTrackToDatabase();
    };

    // -- Top Deck: Tempo & Beat Grid controls ---------------------------------
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
                    saveCurrentTrackToDatabase();
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
            saveCurrentTrackToDatabase();
            repaint();
        }
    };

    addAndMakeVisible (halfBpmButton);
    halfBpmButton.onClick = [this]()
    {
        detectedBpm = std::clamp (detectedBpm * 0.5, 30.0, 300.0);
        processor.getMetronome().setBpm (detectedBpm);
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        saveCurrentTrackToDatabase();
        repaint();
    };

    addAndMakeVisible (doubleBpmButton);
    doubleBpmButton.onClick = [this]()
    {
        detectedBpm = std::clamp (detectedBpm * 2.0, 30.0, 300.0);
        processor.getMetronome().setBpm (detectedBpm);
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        saveCurrentTrackToDatabase();
        repaint();
    };

    addAndMakeVisible (nudgeDownButton);
    nudgeDownButton.onClick = [this]()
    {
        detectedBpm = std::clamp (detectedBpm - 0.5, 30.0, 300.0);
        processor.getMetronome().setBpm (detectedBpm);
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        saveCurrentTrackToDatabase();
        repaint();
    };

    addAndMakeVisible (nudgeUpButton);
    nudgeUpButton.onClick = [this]()
    {
        detectedBpm = std::clamp (detectedBpm + 0.5, 30.0, 300.0);
        processor.getMetronome().setBpm (detectedBpm);
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        saveCurrentTrackToDatabase();
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
            saveCurrentTrackToDatabase();
            repaint();
        }
    };
    bpmEditor.onFocusLost = [this]()
    {
        const double val = bpmEditor.getText().getDoubleValue();
        if (val >= 30.0 && val <= 300.0)
        {
            detectedBpm = std::clamp (std::round (val * 10.0) / 10.0, 30.0, 300.0);
            processor.getMetronome().setBpm (detectedBpm);
            saveCurrentTrackToDatabase();
        }
        bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
        repaint();
    };

    // -- Column 1: Setlist & Song Queue controls ------------------------------
    addAndMakeVisible (autoNextToggleButton);
    autoNextToggleButton.setFontSize (12.0f);
    autoNextToggleButton.onClick = [this]()
    {
        bool next = ! processor.getSetlistPlayer().isAutoNextEnabled();
        processor.getSetlistPlayer().setAutoNextEnabled (next);
        autoNextToggleButton.setLabel (next ? "Auto-Next: Count-In ON" : "Auto-Next: OFF");
        updateButtonStyles();
        repaint();
    };

    addAndMakeVisible (setlistSelectButton);
    setlistSelectButton.setFontSize (12.0f);
    setlistSelectButton.setLeftAligned (true);
    setlistSelectButton.onClick = [this]()
    {
        juce::PopupMenu m;
        auto& db = processor.getSongDatabase();
        const auto& setlists = db.getSetlists();
        const int activeIdx = db.getActiveSetlistIndex();

        for (int i = 0; i < static_cast<int> (setlists.size()); ++i)
        {
            const auto label = setlists[static_cast<size_t> (i)].name
                               + " (" + juce::String (setlists[static_cast<size_t> (i)].items.size()) + " songs)";
            m.addItem (i + 1, label, true, i == activeIdx);
        }

        m.addSeparator();
        m.addItem (1000, "Create New Setlist...");
        if (setlists.size() > 1)
            m.addItem (1001, "Delete Current Setlist");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setlistSelectButton),
            [this](int result)
            {
                auto& dbRef = processor.getSongDatabase();
                if (result >= 1 && result <= static_cast<int> (dbRef.getSetlists().size()))
                {
                    dbRef.setActiveSetlistIndex (result - 1);
                    setlistScrollOffset = 0;
                    refreshSetlistSelectorLabel();
                    rebuildSetlistRowGeometries();
                    if (! dbRef.getActiveSetlist().items.empty())
                        processor.getSetlistPlayer().selectSong (0);
                    repaint();
                }
                else if (result == 1000)
                {
                    const int newNum = static_cast<int> (dbRef.getSetlists().size()) + 1;
                    dbRef.createSetlist ("Practice Setlist " + juce::String (newNum));
                    setlistScrollOffset = 0;
                    refreshSetlistSelectorLabel();
                    rebuildSetlistRowGeometries();
                    repaint();
                }
                else if (result == 1001)
                {
                    dbRef.deleteSetlist (dbRef.getActiveSetlistIndex());
                    setlistScrollOffset = 0;
                    refreshSetlistSelectorLabel();
                    rebuildSetlistRowGeometries();
                    repaint();
                }
            });
    };

    addAndMakeVisible (saveSetlistButton);
    saveSetlistButton.setFontSize (12.0f);
    saveSetlistButton.onClick = [this]()
    {
        saveCurrentTrackToDatabase();
        processor.getSongDatabase().save();
        repaint();
    };

    addAndMakeVisible (newSetlistButton);
    newSetlistButton.setFontSize (12.0f);
    newSetlistButton.onClick = [this]()
    {
        auto& db = processor.getSongDatabase();
        const int newNum = static_cast<int> (db.getSetlists().size()) + 1;
        db.createSetlist ("Practice Setlist " + juce::String (newNum));
        setlistScrollOffset = 0;
        refreshSetlistSelectorLabel();
        rebuildSetlistRowGeometries();
        repaint();
    };

    addAndMakeVisible (addFileToSetlistButton);
    addFileToSetlistButton.setFontSize (12.0f);
    addFileToSetlistButton.onClick = [this]()
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Add Backing Track Audio Files to Setlist",
            juce::File::getSpecialLocation (juce::File::userHomeDirectory),
            "*.wav;*.mp3;*.flac;*.aiff;*.ogg");

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
            [this, chooser](const juce::FileChooser& fc)
            {
                auto results = fc.getResults();
                bool firstLoaded = false;
                for (const auto& file : results)
                {
                    if (file.existsAsFile())
                    {
                        if (! firstLoaded)
                        {
                            loadTrackAndSyncDatabase (file, true);
                            firstLoaded = true;
                        }
                        else
                        {
                            processor.getSongDatabase().addSongToActiveSetlist (file);
                        }
                    }
                }
                rebuildSetlistRowGeometries();
                repaint();
            });
    };

    addAndMakeVisible (openSongDbButton);
    openSongDbButton.setFontSize (12.0f);
    openSongDbButton.onClick = [this]()
    {
        setSongDatabaseModalOpen (! isSongDbModalOpen);
    };

    // -- Column 2: Practice & Metronome controls ------------------------------
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
        saveCurrentTrackToDatabase();
        resized();
        repaint();
    };
    beatsEditor.onFocusLost = [this]()
    {
        int val = std::clamp (beatsEditor.getText().getIntValue(), 1, 16);
        beatsPerMeasure = val;
        beatsEditor.setText (juce::String (val), false);
        processor.getMetronome().setTimeSignature (beatsPerMeasure, 4);
        saveCurrentTrackToDatabase();
        resized();
        repaint();
    };

    clickVolumeKnob = std::make_unique<Knob> ("Click Vol.", 0.0f, 2.5f, 1.0f);
    addAndMakeVisible (*clickVolumeKnob);
    clickVolumeKnob->onValueChanged = [this](float val)
    {
        processor.getMetronome().setLevel (val);
    };

    // -- Column 3: Dual Recording Console controls ----------------------------
    addAndMakeVisible (recordButton);
    recordButton.setFontSize (18.0f);
    recordButton.onClick = [this]()
    {
        if (processor.getAudioRecorder().isRecording())
        {
            stopTransportAndRecording();
            return;
        }

        isRecArmed = ! isRecArmed;
        recordButton.setArmed (isRecArmed);
        recordButton.setLabel (isRecArmed ? "REC ARMED" : "REC");
        repaint();
    };

    addAndMakeVisible (stopButton);
    stopButton.setFontSize (16.0f);
    stopButton.onClick = [this]()
    {
        stopTransportAndRecording();
    };

    addAndMakeVisible (alsoRecordTrackButton);
    alsoRecordTrackButton.setFontSize (13.0f);
    alsoRecordTrackButton.onClick = [this]()
    {
        alsoRecordBackingTrack = ! alsoRecordBackingTrack;
        processor.getAudioRecorder().setSource (alsoRecordBackingTrack
            ? AudioRecorder::RecordingSource::fullMix
            : AudioRecorder::RecordingSource::drumsOnly);
        updateButtonStyles();
        repaint();
    };

    addAndMakeVisible (recordEntireSetlistButton);
    recordEntireSetlistButton.setFontSize (13.0f);
    recordEntireSetlistButton.onClick = [this]()
    {
        recordEntireSetlistTake = ! recordEntireSetlistTake;
        updateButtonStyles();
        repaint();
    };

    addAndMakeVisible (exportWavButton);
    exportWavButton.setFontSize (13.0f);
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
    exportMidButton.setFontSize (13.0f);
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

    // -- Persistent Song Database Modal Child Controls ------------------------
    addChildComponent (songDbModalOverlay);
    songDbModalOverlay.addAndMakeVisible (songDbSearchEditor);
    songDbSearchEditor.setTextToShowWhenEmpty ("Search Song Library...", juce::Colour (0xff666a73));
    songDbSearchEditor.onTextChange = [this]() { songDbModalOverlay.repaint(); };

    songDbModalOverlay.addAndMakeVisible (songDbImportButton);
    songDbImportButton.setFontSize (13.0f);
    songDbImportButton.onClick = [this]()
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Import Backing Tracks into Persistent Song Database",
            juce::File::getSpecialLocation (juce::File::userHomeDirectory),
            "*.wav;*.mp3;*.flac;*.aiff;*.ogg");

        chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
            [this, chooser](const juce::FileChooser& fc)
            {
                for (const auto& f : fc.getResults())
                {
                    if (f.existsAsFile())
                    {
                        const auto entry = processor.getSongDatabase().findOrRegisterSong (f);
                        selectedSongDbIds.insert (entry.songId);
                    }
                }
                songDbAddSelectedButton.setLabel ("Add Selected (" + juce::String (selectedSongDbIds.size()) + ") to Setlist");
                songDbModalOverlay.repaint();
            });
    };

    songDbModalOverlay.addAndMakeVisible (songDbAddSelectedButton);
    songDbAddSelectedButton.setFontSize (13.0f);
    songDbAddSelectedButton.onClick = [this]()
    {
        auto& db = processor.getSongDatabase();
        for (const auto& id : selectedSongDbIds)
            db.addSongIdToActiveSetlist (id);

        selectedSongDbIds.clear();
        setSongDatabaseModalOpen (false);
        rebuildSetlistRowGeometries();
        repaint();
    };

    songDbModalOverlay.addAndMakeVisible (songDbCloseButton);
    songDbCloseButton.setFontSize (13.0f);
    songDbCloseButton.onClick = [this]()
    {
        setSongDatabaseModalOpen (false);
    };

    // Metronome beat tick notification
    processor.getMetronome().onBeat = [this](int beat, bool isDownbeat)
    {
        activeBeatFlash = beat;
        isDownbeatFlash = isDownbeat;
        lastBeatFlashTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
        juce::MessageManager::callAsync ([this] { repaint(); });
    };

    refreshSetlistSelectorLabel();
    applyTheme (Theme::sketch());
    startTimerHz (30);
}

RecordPracticeView::~RecordPracticeView()
{
    stopTimer();
    processor.getMetronome().onBeat = nullptr;
    processor.getMetronome().onCountInComplete = nullptr;
    processor.getSetlistPlayer().onSongLoaded = nullptr;
}

//==============================================================================
void RecordPracticeView::loadTrackAndSyncDatabase (const juce::File& file, bool addToActiveSetlist)
{
    if (! file.existsAsFile())
        return;

    auto& db = processor.getSongDatabase();
    auto& trackPlayer = processor.getBackingTrackPlayer();

    if (! trackPlayer.loadFile (file))
        return;

    const double durSec = trackPlayer.getDurationSeconds();
    SongEntry entry = db.findOrRegisterSong (file, durSec);

    if (addToActiveSetlist)
    {
        const auto& active = db.getActiveSetlist();
        bool alreadyInSetlist = false;
        int foundIdx = -1;
        for (size_t i = 0; i < active.items.size(); ++i)
        {
            if (active.items[i].songId == entry.songId || juce::File (active.items[i].filePath) == file)
            {
                alreadyInSetlist = true;
                foundIdx = static_cast<int> (i);
                break;
            }
        }

        if (! alreadyInSetlist)
            foundIdx = db.addSongIdToActiveSetlist (entry.songId);

        if (foundIdx >= 0)
            processor.getSetlistPlayer().selectSong (foundIdx);
    }

    if (entry.isConfigured)
    {
        // Song was already configured in a past session or setlist — restore immediately with zero re-setup!
        syncUiFromSongEntry (entry, entry.bpm);
    }
    else
    {
        // Brand-new song: run automatic BPM & downbeat detection and save to SongDatabase on completion
        isAnalyzing = true;
        processor.getTempoWorker().startDetection (
            trackPlayer.getDecodedBuffer(),
            processor.getSampleRate(),
            [this, file, durSec](const TempoDetector::Result& res)
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

                    processor.getSongDatabase().updateSongConfig (
                        file,
                        detectedBpm,
                        metronomeOffsetSec,
                        beatsPerMeasure,
                        processor.getBackingTrackPlayer().getLevel(),
                        durSec);
                }
                repaint();
            });
    }

    rebuildSetlistRowGeometries();
    repaint();
}

void RecordPracticeView::saveCurrentTrackToDatabase()
{
    auto& trackPlayer = processor.getBackingTrackPlayer();
    if (! trackPlayer.hasTrack())
        return;

    const auto file = trackPlayer.getLoadedFile();
    if (! file.existsAsFile())
        return;

    processor.getSongDatabase().updateSongConfig (
        file,
        detectedBpm,
        metronomeOffsetSec,
        beatsPerMeasure,
        trackPlayer.getLevel(),
        trackPlayer.getDurationSeconds());
}

void RecordPracticeView::syncUiFromSongEntry (const SongEntry& entry, double effectiveBpm)
{
    detectedBpm = std::clamp (effectiveBpm > 0.0 ? effectiveBpm : entry.bpm, 30.0, 300.0);
    metronomeOffsetSec = entry.firstBeatOffsetSec;
    beatsPerMeasure = std::clamp (entry.beatsPerBar, 1, 16);

    processor.getMetronome().setBpm (detectedBpm);
    processor.getMetronome().setGridOffset (metronomeOffsetSec);
    processor.getMetronome().setTimeSignature (beatsPerMeasure, 4);
    processor.getBackingTrackPlayer().setLevel (entry.backingLevel);

    bpmEditor.setText (juce::String::formatted ("%.1f", detectedBpm), false);
    beatsEditor.setText (juce::String (beatsPerMeasure), false);
    if (trackVolumeKnob != nullptr)
        trackVolumeKnob->setValue (entry.backingLevel);

    zoomFactor = 1.0f;
    viewStartSec = 0.0;
    resized();
}

void RecordPracticeView::refreshSetlistSelectorLabel()
{
    const auto& active = processor.getSongDatabase().getActiveSetlist();
    setlistSelectButton.setLabel ("Set: " + active.name + " v");
}

void RecordPracticeView::setSongDatabaseModalOpen (bool open)
{
    isSongDbModalOpen = open;
    songDbModalOverlay.setVisible (open);

    if (open)
    {
        songDbModalOverlay.toFront (false);
        songDbAddSelectedButton.setLabel (
            selectedSongDbIds.empty()
                ? "Add Selected to Setlist"
                : ("Add Selected (" + juce::String (selectedSongDbIds.size()) + ") to Setlist"));
    }

    resized();
    repaint();
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

    const auto& activeSetlist = processor.getSongDatabase().getActiveSetlist();
    if (! activeSetlist.items.empty())
    {
        processor.getSetlistPlayer().startSetlistPlayback (-1, false);
        repaint();
        return;
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
    processor.getSetlistPlayer().pause();
    repaint();
}

void RecordPracticeView::stopTransportAndRecording()
{
    processor.getSetlistPlayer().stop();
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
    saveCurrentTrackToDatabase();
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
    if (isSongDbModalOpen && key.isKeyCode (juce::KeyPress::escapeKey))
    {
        setSongDatabaseModalOpen (false);
        return true;
    }
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
            saveCurrentTrackToDatabase();
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

    const auto mintGreen  = juce::Colour (0xffa8e6cf);
    const auto skyBlue    = juce::Colour (0xff7ec8f8);
    const auto tanCol     = juce::Colour (0xfff7d9b5);
    const auto coralRed   = juce::Colour (0xffe2786f);
    const auto warmOrange = juce::Colour (0xffea8c16);

    styleBtn (loadTrackButton, currentTheme.panel);
    styleBtn (prevSongButton, currentTheme.panel);
    styleBtn (playButton, mintGreen);
    styleBtn (pauseButton, currentTheme.panel);
    styleBtn (nextSongButton, currentTheme.panel);
    styleBtn (rewindButton, currentTheme.panel);
    styleBtn (loopButton, processor.getBackingTrackPlayer().isLooping() ? currentTheme.accent.withAlpha (0.35f) : currentTheme.panel);
    styleBtn (muteTrackButton, processor.getBackingTrackPlayer().getMuted() ? currentTheme.warning.withAlpha (0.4f) : currentTheme.panel);

    styleBtn (detectBpmButton, skyBlue);
    styleBtn (tapBpmButton, currentTheme.panel);
    styleBtn (alignBeatOneButton, currentTheme.panel);
    styleBtn (halfBpmButton, currentTheme.panel);
    styleBtn (doubleBpmButton, currentTheme.panel);
    styleBtn (nudgeDownButton, currentTheme.panel);
    styleBtn (nudgeUpButton, currentTheme.panel);

    // Setlist & Song Queue buttons
    const bool autoNext = processor.getSetlistPlayer().isAutoNextEnabled();
    autoNextToggleButton.setLabel (autoNext ? "Auto-Next: Count-In ON" : "Auto-Next: OFF");
    styleBtn (autoNextToggleButton, autoNext ? mintGreen : currentTheme.panel);
    styleBtn (setlistSelectButton, tanCol);
    styleBtn (saveSetlistButton, currentTheme.panel);
    styleBtn (newSetlistButton, currentTheme.panel);
    styleBtn (addFileToSetlistButton, skyBlue);
    styleBtn (openSongDbButton, warmOrange);

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
    styleBtn (alsoRecordTrackButton, alsoRecordBackingTrack ? skyBlue : currentTheme.panel);
    styleBtn (recordEntireSetlistButton, recordEntireSetlistTake ? tanCol : currentTheme.panel);
    styleBtn (exportWavButton, mintGreen);
    styleBtn (exportMidButton, tanCol);

    // Song DB Modal buttons
    styleBtn (songDbImportButton, skyBlue);
    styleBtn (songDbAddSelectedButton, mintGreen);
    styleBtn (songDbCloseButton, currentTheme.panel);

    songDbSearchEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    songDbSearchEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xff1a1a1a));
    songDbSearchEditor.setColour (juce::TextEditor::outlineColourId, currentTheme.panelEdge);
    songDbSearchEditor.setColour (juce::TextEditor::focusedOutlineColourId, warmOrange);
    songDbSearchEditor.setFont (uiFont (15.0f));

    beatsEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    beatsEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xff1a1a1a));
    beatsEditor.setColour (juce::TextEditor::outlineColourId, currentTheme.panelEdge);
    beatsEditor.setColour (juce::TextEditor::focusedOutlineColourId, skyBlue);
    beatsEditor.setFont (displayFont (15.0f).boldened());
    beatsEditor.applyColourToAllText (juce::Colour (0xff1a1a1a));
    beatsEditor.applyFontToAllText (displayFont (15.0f).boldened());

    bpmEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::white);
    bpmEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xff1a1a1a));
    bpmEditor.setColour (juce::TextEditor::highlightedTextColourId, currentTheme.panel);
    bpmEditor.setColour (juce::TextEditor::highlightColourId, currentTheme.accent);
    bpmEditor.setColour (juce::TextEditor::outlineColourId, currentTheme.panelEdge);
    bpmEditor.setColour (juce::TextEditor::focusedOutlineColourId, currentTheme.accent);
    bpmEditor.setFont (displayFont (14.0f).boldened());
    bpmEditor.applyColourToAllText (juce::Colour (0xff1a1a1a));
    bpmEditor.applyFontToAllText (displayFont (14.0f).boldened());
}

void RecordPracticeView::updateCountInButtons()
{
    const auto mode = processor.getMetronome().getCountInMode();
    const bool isCountInOn = (mode != Metronome::CountInMode::off);

    const auto mintGreen = juce::Colour (0xffa8e6cf);
    const auto skyBlue   = juce::Colour (0xff7ec8f8);

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
            saveCurrentTrackToDatabase();
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
    bool firstLoaded = false;
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff" || ext == ".ogg")
        {
            juce::File audioFile (f);
            if (! firstLoaded)
            {
                loadTrackAndSyncDatabase (audioFile, true);
                firstLoaded = true;
            }
            else
            {
                processor.getSongDatabase().addSongToActiveSetlist (audioFile);
            }
        }
    }
    rebuildSetlistRowGeometries();
    repaint();
}

//==============================================================================
void RecordPracticeView::timerCallback()
{
    // Advance setlist if current track finished at EOF
    if (processor.getSetlistPlayer().checkAndAdvanceOnEOF())
    {
        // If setlist completed and we were recording, stop recording cleanly
        if (processor.getSetlistPlayer().getState() == SetlistPlayer::State::completed
            || ! recordEntireSetlistTake)
        {
            if (processor.getAudioRecorder().isRecording())
                processor.getAudioRecorder().stopRecording();
            if (processor.getMidiRecorder().isRecording())
                processor.getMidiRecorder().stopRecording();
            isRecArmed = false;
        }
        repaint();
    }

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

            if (curSec < viewStartSec || curSec > (viewStartSec + visibleDur * 0.75))
            {
                const double targetViewStart = std::clamp (curSec - visibleDur * 0.25, 0.0, std::max (0.0, totalDur - visibleDur));
                viewStartSec = targetViewStart;
            }
        }
    }

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
        recordButton.setLabel (isRec ? "RECORDING" : "REC ARMED");
    }
    else
    {
        recordButton.setArmed (false);
        recordButton.setLabel ("REC");
    }

    if (needsRepaint)
        repaint();
}

//==============================================================================
void RecordPracticeView::handleSongDbModalMouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (! songDbModalBounds.contains (pos))
    {
        setSongDatabaseModalOpen (false);
        return;
    }

    for (const auto& rg : songDbRowGeometries)
    {
        if (rg.addBtnBounds.contains (pos))
        {
            processor.getSongDatabase().addSongIdToActiveSetlist (rg.songId);
            selectedSongDbIds.insert (rg.songId);
            rebuildSetlistRowGeometries();
            songDbModalOverlay.repaint();
            repaint();
            return;
        }
        if (rg.rowBounds.contains (pos))
        {
            if (selectedSongDbIds.count (rg.songId) > 0)
                selectedSongDbIds.erase (rg.songId);
            else
                selectedSongDbIds.insert (rg.songId);

            songDbAddSelectedButton.setLabel (
                selectedSongDbIds.empty()
                    ? "Add Selected to Setlist"
                    : ("Add Selected (" + juce::String (selectedSongDbIds.size()) + ") to Setlist"));
            songDbModalOverlay.repaint();
            return;
        }
    }
}

void RecordPracticeView::handleSongDbModalMouseWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (songDbModalBounds.contains (e.getPosition()))
    {
        const int totalSongs = static_cast<int> (processor.getSongDatabase().getSongs().size());
        const int maxScroll = std::max (0, totalSongs - 7);
        if (wheel.deltaY < 0.0f)
            songDbScrollOffset = std::min (maxScroll, songDbScrollOffset + 1);
        else if (wheel.deltaY > 0.0f)
            songDbScrollOffset = std::max (0, songDbScrollOffset - 1);
        songDbModalOverlay.repaint();
    }
}

void RecordPracticeView::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    if (isSongDbModalOpen)
    {
        handleSongDbModalMouseDown (e);
        return;
    }

    // 2. Check clicks inside the Setlist & Song Queue table
    if (setlistTableBodyBounds.contains (pos))
    {
        auto& db = processor.getSongDatabase();
        for (const auto& rg : setlistRowGeometries)
        {
            if (rg.rowBounds.contains (pos))
            {
                if (rg.moveUpBounds.contains (pos))
                {
                    if (db.moveItemInActiveSetlist (rg.itemIndex, rg.itemIndex - 1))
                    {
                        if (processor.getSetlistPlayer().getCurrentSongIndex() == rg.itemIndex)
                            processor.getSetlistPlayer().selectSong (rg.itemIndex - 1);
                        rebuildSetlistRowGeometries();
                        repaint();
                    }
                    return;
                }
                if (rg.moveDownBounds.contains (pos))
                {
                    if (db.moveItemInActiveSetlist (rg.itemIndex, rg.itemIndex + 1))
                    {
                        if (processor.getSetlistPlayer().getCurrentSongIndex() == rg.itemIndex)
                            processor.getSetlistPlayer().selectSong (rg.itemIndex + 1);
                        rebuildSetlistRowGeometries();
                        repaint();
                    }
                    return;
                }
                if (rg.removeBounds.contains (pos))
                {
                    db.removeItemFromActiveSetlist (rg.itemIndex);
                    rebuildSetlistRowGeometries();
                    repaint();
                    return;
                }

                // Single-clicking a row selects & loads that song into the Waveform Deck
                processor.getSetlistPlayer().selectSong (rg.itemIndex);
                repaint();
                return;
            }
        }
        return;
    }

    // 3. Waveform & Synchronized Beat Timeline interaction
    const bool inWave = waveformBounds.contains (pos);
    const bool inTime = timelineBounds.contains (pos);

    if (! inWave && ! inTime)
        return;

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
    if (isSongDbModalOpen)
        return;

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
    if (isOffsettingGrid)
    {
        saveCurrentTrackToDatabase();
    }
    isDraggingPlayhead = false;
    isOffsettingGrid = false;
    isDraggingTimeline = false;
}

void RecordPracticeView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isSongDbModalOpen)
        return;

    const auto pos = e.getPosition();
    if (setlistTableBodyBounds.contains (pos))
    {
        for (const auto& rg : setlistRowGeometries)
        {
            if (rg.rowBounds.contains (pos)
                && ! rg.moveUpBounds.contains (pos)
                && ! rg.moveDownBounds.contains (pos)
                && ! rg.removeBounds.contains (pos))
            {
                processor.getSetlistPlayer().startSetlistPlayback (rg.itemIndex, false);
                repaint();
                return;
            }
        }
    }

    if (timelineBounds.contains (pos))
    {
        metronomeOffsetSec = 0.0;
        processor.getMetronome().setGridOffset (0.0);
        saveCurrentTrackToDatabase();
        repaint();
    }
}

void RecordPracticeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto pos = e.getPosition();

    if (isSongDbModalOpen && songDbModalBounds.contains (pos))
    {
        const int totalSongs = static_cast<int> (processor.getSongDatabase().getSongs().size());
        const int maxScroll = std::max (0, totalSongs - 7);
        if (wheel.deltaY < 0.0f)
            songDbScrollOffset = std::min (maxScroll, songDbScrollOffset + 1);
        else if (wheel.deltaY > 0.0f)
            songDbScrollOffset = std::max (0, songDbScrollOffset - 1);
        repaint();
        return;
    }

    if (setlistConsoleBounds.contains (pos))
    {
        const int totalItems = static_cast<int> (processor.getSongDatabase().getActiveSetlist().items.size());
        const int maxScroll = std::max (0, totalItems - 5);
        if (wheel.deltaY < 0.0f)
            setlistScrollOffset = std::min (maxScroll, setlistScrollOffset + 1);
        else if (wheel.deltaY > 0.0f)
            setlistScrollOffset = std::max (0, setlistScrollOffset - 1);
        rebuildSetlistRowGeometries();
        repaint();
        return;
    }

    if (waveformBounds.contains (pos) || timelineBounds.contains (pos))
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
void RecordPracticeView::rebuildSetlistRowGeometries()
{
    setlistRowGeometries.clear();
    if (setlistTableBodyBounds.isEmpty())
        return;

    const auto& items = processor.getSongDatabase().getActiveSetlist().items;
    const int n = static_cast<int> (items.size());
    const int maxScroll = std::max (0, n - 5);
    setlistScrollOffset = std::clamp (setlistScrollOffset, 0, maxScroll);

    const int rowH = 37;
    const int rowGap = 3;
    const int x1 = setlistTableBodyBounds.getX();
    const int x2 = setlistTableBodyBounds.getRight();

    for (int visIdx = 0; visIdx < 5; ++visIdx)
    {
        const int itemIdx = setlistScrollOffset + visIdx;
        if (itemIdx >= n)
            break;

        const int ry1 = setlistTableBodyBounds.getY() + visIdx * (rowH + rowGap);
        const int ry2 = ry1 + rowH;
        if (ry2 > setlistTableBodyBounds.getBottom() + 4)
            break;

        SetlistRowGeometry rg;
        rg.itemIndex = itemIdx;
        rg.rowBounds = { x1, ry1, x2 - x1, rowH };
        rg.moveUpBounds   = { x2 - 42, ry1 + 7, 13, rowH - 14 };
        rg.moveDownBounds = { x2 - 27, ry1 + 7, 13, rowH - 14 };
        rg.removeBounds   = { x2 - 12, ry1 + 7, 10, rowH - 14 };
        setlistRowGeometries.push_back (rg);
    }
}

void RecordPracticeView::resized()
{
    auto area = getLocalBounds().reduced (12, 6);

    // 1. Top Full-Width Waveform & Tempo Deck (~47.5% height)
    const int deckH = static_cast<int> (area.getHeight() * 0.475);
    topDeckBounds = area.removeFromTop (deckH);
    area.removeFromTop (10);

    // 2. Bottom 3-Column Console:
    //    Col 1: SETLIST & SONG QUEUE (~45% width)
    //    Col 2: PRACTICE & METRONOME (~29% width)
    //    Col 3: DUAL RECORDING CONSOLE (~26% width)
    const int totalW = area.getWidth();
    const int col1W = static_cast<int> (totalW * 0.45);
    const int col2W = static_cast<int> (totalW * 0.285);

    setlistConsoleBounds = area.removeFromLeft (col1W);
    area.removeFromLeft (10);
    practiceConsoleBounds = area.removeFromLeft (col2W);
    area.removeFromLeft (10);
    recordingConsoleBounds = area;

    // --- Layout Top Deck (Waveform & Transport) ---
    auto deckInner = topDeckBounds.reduced (12, 8);
    auto topRow = deckInner.removeFromTop (44);
    if (trackVolumeKnob != nullptr)
        trackVolumeKnob->setBounds (topRow.removeFromRight (52).withHeight (44));

    topRow.removeFromRight (8);
    auto btnRow = topRow.withTrimmedTop (4).withTrimmedBottom (10);

    loadTrackButton.setBounds (btnRow.removeFromLeft (98));
    btnRow.removeFromLeft (6);
    prevSongButton.setBounds (btnRow.removeFromLeft (66));
    btnRow.removeFromLeft (6);
    playButton.setBounds (btnRow.removeFromLeft (106));
    btnRow.removeFromLeft (6);
    pauseButton.setBounds (btnRow.removeFromLeft (64));
    btnRow.removeFromLeft (6);
    nextSongButton.setBounds (btnRow.removeFromLeft (68));
    btnRow.removeFromLeft (6);
    rewindButton.setBounds (btnRow.removeFromLeft (68));
    btnRow.removeFromLeft (6);
    loopButton.setBounds (btnRow.removeFromLeft (104));
    btnRow.removeFromLeft (6);
    muteTrackButton.setBounds (btnRow.removeFromLeft (56));
    btnRow.removeFromLeft (8);
    setlistStatusPillBounds = btnRow;

    deckInner.removeFromTop (2);
    auto bpmRow = deckInner.removeFromBottom (28);
    detectBpmButton.setBounds (bpmRow.removeFromLeft (94));
    bpmRow.removeFromLeft (6);
    tapBpmButton.setBounds (bpmRow.removeFromLeft (84));
    bpmRow.removeFromLeft (6);
    alignBeatOneButton.setBounds (bpmRow.removeFromLeft (92));
    bpmRow.removeFromLeft (6);
    halfBpmButton.setBounds (bpmRow.removeFromLeft (36));
    bpmRow.removeFromLeft (4);
    doubleBpmButton.setBounds (bpmRow.removeFromLeft (36));
    bpmRow.removeFromLeft (6);
    nudgeDownButton.setBounds (bpmRow.removeFromLeft (48));
    bpmRow.removeFromLeft (4);
    nudgeUpButton.setBounds (bpmRow.removeFromLeft (48));

    bpmEditor.setBounds (bpmRow.removeFromRight (74).reduced (0, 1));

    deckInner.removeFromBottom (4);
    timelineBounds = deckInner.removeFromBottom (22);
    waveformBounds = deckInner;

    // --- Layout Column 1: SETLIST & SONG QUEUE ---
    auto c1Inner = setlistConsoleBounds.reduced (12, 8);
    auto c1Header = c1Inner.removeFromTop (26);
    autoNextToggleButton.setBounds (c1Header.removeFromRight (188));

    c1Inner.removeFromTop (6);
    auto c1Toolbar = c1Inner.removeFromTop (26);
    openSongDbButton.setBounds (c1Toolbar.removeFromRight (88));
    c1Toolbar.removeFromRight (5);
    addFileToSetlistButton.setBounds (c1Toolbar.removeFromRight (78));
    c1Toolbar.removeFromRight (5);
    newSetlistButton.setBounds (c1Toolbar.removeFromRight (46));
    c1Toolbar.removeFromRight (5);
    saveSetlistButton.setBounds (c1Toolbar.removeFromRight (48));
    c1Toolbar.removeFromRight (5);
    setlistSelectButton.setBounds (c1Toolbar);

    c1Inner.removeFromTop (6);
    setlistTableHeaderBounds = c1Inner.removeFromTop (20);
    c1Inner.removeFromTop (3);
    c1Inner.removeFromBottom (22); // Reserve footer strip
    setlistTableBodyBounds = c1Inner;
    rebuildSetlistRowGeometries();

    // --- Layout Column 2: PRACTICE & METRONOME ---
    auto c2Inner = practiceConsoleBounds.reduced (12, 8);
    auto c2Header = c2Inner.removeFromTop (44);
    if (clickVolumeKnob != nullptr)
        clickVolumeKnob->setBounds (c2Header.removeFromRight (50).withHeight (44));

    auto mRow1 = c2Inner.removeFromTop (30);
    const int halfM = (mRow1.getWidth() - 6) / 2;
    metroToggleButton.setBounds (mRow1.removeFromLeft (halfM));
    mRow1.removeFromLeft (6);
    countInToggleButton.setBounds (mRow1);

    c2Inner.removeFromTop (6);
    auto mRow2 = c2Inner.removeFromTop (28);
    mRow2.removeFromLeft (106); // Space for "Between Songs:" label
    const int barBtnW = (mRow2.getWidth() - 6) / 2;
    countIn1BarButton.setBounds (mRow2.removeFromLeft (barBtnW));
    mRow2.removeFromLeft (6);
    countIn2BarsButton.setBounds (mRow2);

    c2Inner.removeFromTop (6);
    clickPresetButton.setBounds (c2Inner.removeFromTop (30));

    c2Inner.removeFromTop (8);
    auto mRow4 = c2Inner.removeFromTop (26);
    beatsEditor.setBounds (mRow4.getX() + 146, mRow4.getY(), 46, 26);

    c2Inner.removeFromTop (8);
    auto lightsArea = c2Inner.removeFromTop (56);
    beatLightBounds.clear();
    if (beatsPerMeasure > 0)
    {
        const int spacing = 5;
        const int totalSpacing = spacing * (beatsPerMeasure - 1);
        int cardW = (lightsArea.getWidth() - totalSpacing) / beatsPerMeasure;
        cardW = std::clamp (cardW, 20, 62);
        int curX = lightsArea.getX();
        for (int i = 0; i < beatsPerMeasure; ++i)
        {
            beatLightBounds.push_back ({ curX, lightsArea.getY(), cardW, lightsArea.getHeight() });
            curX += cardW + spacing;
        }
    }

    // --- Layout Column 3: DUAL RECORDING CONSOLE ---
    auto c3Inner = recordingConsoleBounds.reduced (12, 8);
    c3Inner.removeFromTop (34); // Header space

    auto rRow1 = c3Inner.removeFromTop (52);
    const int recW = static_cast<int> (rRow1.getWidth() * 0.62);
    recordButton.setBounds (rRow1.removeFromLeft (recW));
    rRow1.removeFromLeft (6);
    stopButton.setBounds (rRow1);

    c3Inner.removeFromTop (8);
    alsoRecordTrackButton.setBounds (c3Inner.removeFromTop (34));

    c3Inner.removeFromTop (8);
    recordEntireSetlistButton.setBounds (c3Inner.removeFromTop (34));

    c3Inner.removeFromTop (8);
    auto rRow4 = c3Inner.removeFromTop (38);
    const int expW = (rRow4.getWidth() - 6) / 2;
    exportWavButton.setBounds (rRow4.removeFromLeft (expW));
    rRow4.removeFromLeft (6);
    exportMidButton.setBounds (rRow4);

    // --- Layout Persistent Song Database Modal Overlay ---
    songDbModalOverlay.setBounds (getLocalBounds());
    const int mw = std::min (840, getWidth() - 60);
    const int mh = std::min (560, getHeight() - 40);
    songDbModalBounds = juce::Rectangle<int> ((getWidth() - mw) / 2, (getHeight() - mh) / 2, mw, mh);

    if (isSongDbModalOpen)
    {
        const int mx = songDbModalBounds.getX() + 24;
        const int my = songDbModalBounds.getY() + 14;
        const int mr = songDbModalBounds.getRight() - 24;

        songDbCloseButton.setBounds (mr - 88, my, 88, 30);

        const int barY = my + 66;
        songDbAddSelectedButton.setBounds (mr - 172, barY, 172, 32);
        songDbImportButton.setBounds (mr - 172 - 8 - 178, barY, 178, 32);
        songDbSearchEditor.setBounds (mx, barY, (mr - 172 - 8 - 178 - 10) - mx, 32);
    }
}

//==============================================================================
void RecordPracticeView::paint (juce::Graphics& g)
{
    g.fillAll (currentTheme.background);

    drawWaveformDeck (g, topDeckBounds);
    drawSetlistConsole (g, setlistConsoleBounds);
    drawPracticeConsole (g, practiceConsoleBounds);
    drawRecordingConsole (g, recordingConsoleBounds);
}

void RecordPracticeView::drawWaveformDeck (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    drawSketchRect (g, area.toFloat(), currentTheme.panelEdge, 2.0f, 10);

    const auto blueCol = juce::Colour (0xff3498db);
    const auto warmOrange = juce::Colour (0xffea8c16);

    // Update Play button label based on setlist
    const auto& activeSetlist = processor.getSongDatabase().getActiveSetlist();
    playButton.setLabel (activeSetlist.items.empty() ? "Play" : "Play Setlist");

    // Draw Live Setlist Status Pill in top transport bar
    if (! setlistStatusPillBounds.isEmpty())
    {
        const auto pillBg = currentTheme.background.getPerceivedBrightness() < 0.35f
                                ? juce::Colour (0xff2b2218)
                                : juce::Colour (0xfffff3db);
        g.setColour (pillBg);
        g.fillRoundedRectangle (setlistStatusPillBounds.toFloat(), 4.0f);
        drawSketchRect (g, setlistStatusPillBounds.toFloat(), warmOrange, 1.5f, 44);

        juce::String statusTxt = "SETLIST READY  |  ADD SONGS BELOW";
        if (! activeSetlist.items.empty())
        {
            const int curIdx = std::clamp (processor.getSetlistPlayer().getCurrentSongIndex(),
                                           0, static_cast<int> (activeSetlist.items.size()) - 1);
            const int total = static_cast<int> (activeSetlist.items.size());
            if (curIdx + 1 < total)
            {
                const auto& nextItem = activeSetlist.items[static_cast<size_t> (curIdx + 1)];
                juce::String nextTitle = juce::File (nextItem.filePath).getFileNameWithoutExtension();
                if (const auto* s = processor.getSongDatabase().findSongById (nextItem.songId))
                    nextTitle = juce::File (s->title).getFileNameWithoutExtension();
                if (nextTitle.length() > 18)
                    nextTitle = nextTitle.substring (0, 16) + "..";
                const double nextBpm = processor.getSongDatabase().getEffectiveBpmForItem (nextItem);
                statusTxt = juce::String::formatted ("SETLIST #%d/%d  |  NEXT: %02d. ", curIdx + 1, total, curIdx + 2)
                            + nextTitle + juce::String::formatted (" (%.0f BPM)", nextBpm);
            }
            else
            {
                statusTxt = juce::String::formatted ("SETLIST #%d/%d  |  FINAL SONG IN SETLIST", curIdx + 1, total);
            }
        }

        g.setFont (displayFont (11.5f).boldened());
        g.setColour (currentTheme.background.getPerceivedBrightness() < 0.35f ? juce::Colour (0xffffb84d) : juce::Colour (0xffb35c00));
        g.drawText (statusTxt, setlistStatusPillBounds.reduced (6, 0), juce::Justification::centred, true);
    }

    // Waveform box
    if (! waveformBounds.isEmpty())
    {
        g.setColour (currentTheme.background);
        g.fillRect (waveformBounds);
        g.setColour (currentTheme.panelEdge.withAlpha (0.5f));
        g.drawRect (waveformBounds, 1);

        auto& trackPlayer = processor.getBackingTrackPlayer();
        const double totalDur = std::max (0.01, trackPlayer.getDurationSeconds());
        const double visibleDur = totalDur / std::max (1.0f, zoomFactor);
        const int w = waveformBounds.getWidth();
        const int h = waveformBounds.getHeight();
        const int midY = waveformBounds.getCentreY();
        const int numSig = std::max (1, beatsPerMeasure);

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

            // Draw vertical beat grid guidelines using actual beatsPerMeasure
            const double secPerBeat = 60.0 / std::max (20.0, detectedBpm);
            const int64_t kStart = static_cast<int64_t> (std::floor ((viewStartSec - metronomeOffsetSec) / secPerBeat));
            const int64_t kEnd = static_cast<int64_t> (std::ceil ((viewStartSec + visibleDur - metronomeOffsetSec) / secPerBeat));

            for (int64_t k = kStart; k <= kEnd; ++k)
            {
                const double tBeat = metronomeOffsetSec + static_cast<double> (k) * secPerBeat;
                const float bx = timeToX (tBeat);
                if (bx >= static_cast<float> (waveformBounds.getX()) && bx <= static_cast<float> (waveformBounds.getRight()))
                {
                    const int mBeat = static_cast<int> (((k % numSig) + numSig) % numSig) + 1;
                    if (mBeat == 1)
                    {
                        g.setColour (warmOrange.withAlpha (0.65f));
                        g.drawLine (bx, static_cast<float> (waveformBounds.getY()),
                                    bx, static_cast<float> (waveformBounds.getBottom()), 2.0f);
                    }
                    else
                    {
                        g.setColour (currentTheme.inkSoft.withAlpha (0.22f));
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
                g.setColour (juce::Colour (0xffe74c3c));
                g.drawLine (playheadX, static_cast<float> (waveformBounds.getY()),
                            playheadX, static_cast<float> (waveformBounds.getBottom()), 2.5f);

                juce::Path cap;
                cap.addTriangle (playheadX - 5.5f, static_cast<float> (waveformBounds.getY()),
                                 playheadX + 5.5f, static_cast<float> (waveformBounds.getY()),
                                 playheadX, static_cast<float> (waveformBounds.getY() + 8.0f));
                g.fillPath (cap);
            }

            // Top-left Track Title Pill + [DB SAVED] badge
            const bool isSavedInDb = processor.getSongDatabase().isSongConfigured (trackPlayer.getLoadedFile());
            juce::String trackDisplay = trackPlayer.getTrackName();
            if (isSavedInDb)
                trackDisplay += "  [ DB SAVED ]";
            if (zoomFactor > 1.05f)
                trackDisplay += juce::String::formatted ("  [%.1fx]", zoomFactor);

            juce::Rectangle<float> titlePill (static_cast<float> (waveformBounds.getX() + 8),
                                              static_cast<float> (waveformBounds.getY() + 6),
                                              310.0f, 22.0f);
            g.setColour (currentTheme.panel);
            g.fillRoundedRectangle (titlePill, 4.0f);
            g.setColour (currentTheme.panelEdge);
            g.drawRoundedRectangle (titlePill, 4.0f, 1.2f);
            g.setFont (displayFont (12.5f));
            g.setColour (currentTheme.ink);
            g.drawText (trackDisplay, titlePill.reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);

            // Top-right Grid Offset Pill + Time Pill
            const double ms = metronomeOffsetSec * 1000.0;
            const auto gridStr = juce::String::formatted ("Grid: %+.1f ms (DB Saved)", ms);
            juce::Rectangle<float> gridPill (static_cast<float> (waveformBounds.getRight() - 304),
                                             static_cast<float> (waveformBounds.getY() + 6),
                                             178.0f, 22.0f);
            g.setColour (currentTheme.panel);
            g.fillRoundedRectangle (gridPill, 4.0f);
            g.setColour (currentTheme.panelEdge);
            g.drawRoundedRectangle (gridPill, 4.0f, 1.2f);
            g.setFont (displayFont (11.5f));
            g.setColour (warmOrange);
            g.drawText (gridStr, gridPill, juce::Justification::centred, false);

            const auto timeStr = juce::String::formatted ("%02d:%02d / %02d:%02d",
                static_cast<int> (curSec) / 60, static_cast<int> (curSec) % 60,
                static_cast<int> (totalDur) / 60, static_cast<int> (totalDur) % 60);
            juce::Rectangle<float> timePill (static_cast<float> (waveformBounds.getRight() - 120),
                                             static_cast<float> (waveformBounds.getY() + 6),
                                             112.0f, 22.0f);
            g.setColour (currentTheme.panel);
            g.fillRoundedRectangle (timePill, 4.0f);
            g.setColour (currentTheme.panelEdge);
            g.drawRoundedRectangle (timePill, 4.0f, 1.2f);
            g.setFont (displayFont (12.5f));
            g.setColour (currentTheme.ink);
            g.drawText (timeStr, timePill, juce::Justification::centred, false);
        }
        else
        {
            g.setFont (uiFont (16.0f));
            g.setColour (currentTheme.ink.withAlpha (0.65f));
            g.drawText ("Drag & Drop Audio Tracks Here, Click 'Load Track...', or Pick From '+ Song DB'",
                        waveformBounds, juce::Justification::centred);
        }

        // Between-Song Count-In Banner Overlay
        if (processor.getMetronome().isCountingIn())
        {
            const int curBeat = std::max (1, activeBeatFlash > 0 ? activeBeatFlash : 1);
            juce::String beatSeq;
            for (int b = 1; b <= numSig; ++b)
            {
                if (b > 1) beatSeq += " - ";
                if (b == curBeat)
                    beatSeq += "[ " + juce::String (b) + " ]";
                else
                    beatSeq += juce::String (b);
            }

            const int songNum = processor.getSetlistPlayer().getCurrentSongIndex() + 1;
            const auto bannerTxt = juce::String::formatted ("COUNT-IN TO SONG #%d:  ", songNum)
                                   + beatSeq + juce::String::formatted ("  (%.1f BPM)", detectedBpm);

            juce::Rectangle<float> bannerRect (
                static_cast<float> (waveformBounds.getCentreX() - 230),
                static_cast<float> (waveformBounds.getCentreY() - 18),
                460.0f, 36.0f);
            g.setColour (warmOrange);
            g.fillRoundedRectangle (bannerRect, 6.0f);
            drawSketchRect (g, bannerRect, juce::Colours::white, 2.0f, 99);
            g.setFont (displayFont (14.5f).boldened());
            g.setColour (juce::Colour (0xff101114));
            g.drawText (bannerTxt, bannerRect, juce::Justification::centred, false);
        }

        drawBeatTimelineBar (g, timelineBounds);

        // Auto-saved status note in bottom tempo bar + BPM label
        if (! bpmEditor.getBounds().isEmpty())
        {
            if (trackPlayer.hasTrack() && processor.getSongDatabase().isSongConfigured (trackPlayer.getLoadedFile()))
            {
                g.setFont (uiFont (14.5f));
                g.setColour (juce::Colour (0xff27ae60));
                const auto savedNote = juce::String::formatted (
                    "Song settings auto-saved to Library (BPM, %+.1fms Beat 1 Offset, %d/4 Meter)",
                    metronomeOffsetSec * 1000.0, beatsPerMeasure);
                juce::Rectangle<int> noteArea (
                    nudgeUpButton.getRight() + 14,
                    bpmEditor.getY(),
                    bpmEditor.getX() - 80 - (nudgeUpButton.getRight() + 14),
                    bpmEditor.getHeight());
                g.drawText (savedNote, noteArea, juce::Justification::centred, true);
            }

            g.setFont (displayFont (13.5f).boldened());
            g.setColour (blueCol);
            juce::String bpmInfo = "BPM:";
            if (isAnalyzing)
                bpmInfo = "Analyzing...";
            else if (bpmConfidence > 0.0f)
                bpmInfo = juce::String::formatted ("(%.0f%%) BPM:", bpmConfidence * 100.0f);

            auto labelArea = bpmEditor.getBounds().translated (-96, 0).withWidth (90);
            g.drawText (bpmInfo, labelArea, juce::Justification::centredRight);
        }
    }
}

void RecordPracticeView::drawBeatTimelineBar (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty())
        return;

    g.setColour (currentTheme.background.interpolatedWith (currentTheme.panel, 0.45f));
    g.fillRoundedRectangle (area.toFloat(), 2.0f);
    g.setColour (currentTheme.panelEdge.withAlpha (0.55f));
    g.drawRoundedRectangle (area.toFloat(), 2.0f, 1.0f);

    const double totalDur = std::max (0.01, processor.getBackingTrackPlayer().getDurationSeconds());
    const double visibleDur = totalDur / std::max (1.0f, zoomFactor);
    const int w = area.getWidth();
    const int numSig = std::max (1, beatsPerMeasure);

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
            const int mBeat = static_cast<int> (((k % numSig) + numSig) % numSig) + 1;
            const int barNum = static_cast<int> (std::floor (static_cast<double> (k) / static_cast<double> (numSig))) + 1;

            if (mBeat == 1)
            {
                g.setColour (juce::Colour (0xffea8c16));
                g.drawLine (bx, static_cast<float> (area.getY()),
                            bx, static_cast<float> (area.getBottom()), 2.0f);

                g.setFont (displayFont (11.0f).boldened());
                g.drawText (juce::String (barNum),
                            juce::Rectangle<float> (bx + 3.0f, static_cast<float> (area.getY() + 1), 28.0f, static_cast<float> (area.getHeight() - 2)),
                            juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (currentTheme.inkSoft.withAlpha (0.65f));
                g.drawLine (bx, static_cast<float> (area.getY() + 7),
                            bx, static_cast<float> (area.getBottom() - 1), 1.0f);

                g.setFont (uiFont (9.5f));
                g.drawText (juce::String (mBeat),
                            juce::Rectangle<float> (bx + 2.0f, static_cast<float> (area.getY() + 5), 14.0f, static_cast<float> (area.getHeight() - 6)),
                            juce::Justification::centredLeft);
            }
        }
    }
}

void RecordPracticeView::drawSetlistConsole (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    drawSketchRect (g, area.toFloat(), currentTheme.panelEdge, 2.0f, 21);

    // Header Title
    g.setFont (displayFont (15.5f).boldened());
    g.setColour (currentTheme.ink);
    g.drawText ("SETLIST & SONG QUEUE", area.reduced (14, 8).removeFromTop (24), juce::Justification::centredLeft);

    // Table Header
    if (! setlistTableHeaderBounds.isEmpty())
    {
        const auto troughCol = currentTheme.background.interpolatedWith (currentTheme.panel, 0.35f);
        g.setColour (troughCol);
        g.fillRect (setlistTableHeaderBounds);
        g.setColour (currentTheme.panelEdge);
        g.drawRect (setlistTableHeaderBounds, 1);

        const int hx = setlistTableHeaderBounds.getX();
        const int hy = setlistTableHeaderBounds.getY();
        const int hh = setlistTableHeaderBounds.getHeight();
        const int hr = setlistTableHeaderBounds.getRight();

        g.setFont (displayFont (11.0f));
        g.setColour (currentTheme.inkSoft);
        g.drawText ("#",          hx + 2,   hy, 28, hh, juce::Justification::centred);
        g.drawText ("SONG TITLE", hx + 32,  hy, (hr - 252) - (hx + 32), hh, juce::Justification::centredLeft);
        g.drawText ("BPM",        hr - 252, hy, 60, hh, juce::Justification::centred);
        g.drawText ("OFFSET",     hr - 192, hy, 62, hh, juce::Justification::centred);
        g.drawText ("SIG",        hr - 130, hy, 38, hh, juce::Justification::centred);
        g.drawText ("TIME",       hr - 92,  hy, 46, hh, juce::Justification::centred);
        g.drawText ("ORDER",      hr - 46,  hy, 44, hh, juce::Justification::centred);
    }

    // Song Rows
    const auto& db = processor.getSongDatabase();
    const auto& activeSetlist = db.getActiveSetlist();
    const int currentIdx = processor.getSetlistPlayer().getCurrentSongIndex();
    const bool isDark = currentTheme.background.getPerceivedBrightness() < 0.35f;

    if (activeSetlist.items.empty())
    {
        g.setFont (uiFont (15.0f));
        g.setColour (currentTheme.inkSoft);
        g.drawText ("Setlist is empty -- click '+ Add File', '+ Song DB', or drag audio files here!",
                    setlistTableBodyBounds.reduced (12), juce::Justification::centred);
    }
    else
    {
        for (const auto& rg : setlistRowGeometries)
        {
            if (rg.itemIndex < 0 || rg.itemIndex >= static_cast<int> (activeSetlist.items.size()))
                continue;

            const auto& item = activeSetlist.items[static_cast<size_t> (rg.itemIndex)];
            const SongEntry* entry = db.findSongById (item.songId);

            const bool isCurrent = (rg.itemIndex == currentIdx);
            const bool isCompleted = (rg.itemIndex < currentIdx);

            const auto rowBg = isCurrent
                                   ? (isDark ? juce::Colour (0xff2b2218) : juce::Colour (0xfffff3db))
                                   : ((rg.itemIndex % 2 == 1)
                                          ? currentTheme.background.interpolatedWith (currentTheme.panel, 0.65f)
                                          : currentTheme.panel);
            const auto rowEdge = isCurrent ? juce::Colour (0xffea8c16) : currentTheme.panelEdge;

            g.setColour (rowBg);
            g.fillRoundedRectangle (rg.rowBounds.toFloat(), 4.0f);
            drawSketchRect (g, rg.rowBounds.toFloat(), rowEdge, isCurrent ? 2.0f : 1.0f, static_cast<unsigned> (100 + rg.itemIndex));

            const int rx = rg.rowBounds.getX();
            const int ry = rg.rowBounds.getY();
            const int rh = rg.rowBounds.getHeight();
            const int rr = rg.rowBounds.getRight();

            // 1. Track #
            const auto numStr = (isCurrent ? ">" : "") + juce::String::formatted ("%02d", rg.itemIndex + 1);
            g.setFont (displayFont (12.0f).boldened());
            g.setColour (isCurrent ? juce::Colour (0xffea8c16) : (isCompleted ? juce::Colour (0xff27ae60) : currentTheme.inkSoft));
            g.drawText (numStr, rx + 2, ry, 28, rh, juce::Justification::centred);

            // 2. Song Title
            juce::String title = entry != nullptr ? entry->title : juce::File (item.filePath).getFileName();
            g.setFont (displayFont (12.0f));
            g.setColour (currentTheme.ink);
            g.drawText (title, rx + 32, ry, (rr - 254) - (rx + 32), rh, juce::Justification::centredLeft, true);

            // 3. BPM Pill
            const double bpm = db.getEffectiveBpmForItem (item);
            juce::Rectangle<float> bpmRect (static_cast<float> (rr - 248), static_cast<float> (ry + 6), 52.0f, static_cast<float> (rh - 12));
            g.setColour (isDark ? juce::Colour (0xff1f3a2e) : juce::Colour (0xffd9f2e6));
            g.fillRoundedRectangle (bpmRect, 3.0f);
            g.setColour (currentTheme.panelEdge);
            g.drawRoundedRectangle (bpmRect, 3.0f, 1.0f);
            g.setFont (displayFont (11.5f));
            g.setColour (isDark ? juce::Colour (0xffa8e6cf) : juce::Colour (0xff101114));
            g.drawText (juce::String::formatted ("%.1f", bpm), bpmRect, juce::Justification::centred, false);

            // 4. Offset (ms)
            const double offMs = (entry != nullptr ? entry->firstBeatOffsetSec : 0.0) * 1000.0;
            g.setFont (uiFont (13.5f).boldened());
            g.setColour (juce::Colour (0xffea8c16));
            g.drawText (juce::String::formatted ("%+.1fms", offMs), rr - 192, ry, 62, rh, juce::Justification::centred);

            // 5. Time Sig
            const int sig = entry != nullptr ? entry->beatsPerBar : 4;
            g.setFont (displayFont (11.5f));
            g.setColour (currentTheme.inkSoft);
            g.drawText (juce::String (sig) + "/4", rr - 130, ry, 38, rh, juce::Justification::centred);

            // 6. Duration
            const double dur = entry != nullptr ? entry->durationSeconds : 0.0;
            const auto durStr = dur > 0.0
                                    ? juce::String::formatted ("%02d:%02d", static_cast<int> (dur) / 60, static_cast<int> (dur) % 60)
                                    : "--:--";
            g.setFont (uiFont (13.0f));
            g.setColour (currentTheme.inkSoft);
            g.drawText (durStr, rr - 92, ry, 46, rh, juce::Justification::centred);

            // 7. Reorder / Remove buttons (^ v x)
            g.setColour (currentTheme.panel);
            g.fillRoundedRectangle (rg.moveUpBounds.toFloat(), 2.0f);
            g.fillRoundedRectangle (rg.moveDownBounds.toFloat(), 2.0f);
            g.setColour (juce::Colour (0xffe2786f));
            g.fillRoundedRectangle (rg.removeBounds.toFloat(), 2.0f);

            g.setColour (currentTheme.panelEdge);
            g.drawRoundedRectangle (rg.moveUpBounds.toFloat(), 2.0f, 1.0f);
            g.drawRoundedRectangle (rg.moveDownBounds.toFloat(), 2.0f, 1.0f);
            g.drawRoundedRectangle (rg.removeBounds.toFloat(), 2.0f, 1.0f);

            g.setFont (displayFont (10.0f));
            g.setColour (currentTheme.ink);
            g.drawText ("^", rg.moveUpBounds, juce::Justification::centred);
            g.drawText ("v", rg.moveDownBounds, juce::Justification::centred);
            g.setColour (juce::Colours::white);
            g.drawText ("x", rg.removeBounds, juce::Justification::centred);
        }
    }

    // Footer Summary Strip
    double totalSetSec = 0.0;
    for (const auto& item : activeSetlist.items)
        if (const auto* s = db.findSongById (item.songId))
            totalSetSec += s->durationSeconds;

    const auto footerStr = juce::String::formatted (
        "Total Setlist: %d Songs (%02d:%02d)  |  Song DB: %d Configured Tracks  |  Drag files or click + Song DB",
        static_cast<int> (activeSetlist.items.size()),
        static_cast<int> (totalSetSec) / 60,
        static_cast<int> (totalSetSec) % 60,
        db.getNumConfiguredSongs());

    g.setFont (uiFont (13.0f));
    g.setColour (currentTheme.inkSoft);
    g.drawText (footerStr, area.reduced (12, 4).removeFromBottom (20), juce::Justification::centredLeft, true);
}

void RecordPracticeView::drawPracticeConsole (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    drawSketchRect (g, area.toFloat(), currentTheme.panelEdge, 2.0f, 22);

    g.setFont (displayFont (15.5f).boldened());
    g.setColour (currentTheme.ink);
    g.drawText ("PRACTICE & METRONOME", area.reduced (12, 8).removeFromTop (24), juce::Justification::centredLeft);

    // "Between Songs:" label beside 1 bar / 2 bars
    if (! countIn1BarButton.getBounds().isEmpty())
    {
        g.setFont (displayFont (12.5f));
        g.setColour (currentTheme.inkSoft);
        juce::Rectangle<int> lbl (area.getX() + 12, countIn1BarButton.getY(), 102, countIn1BarButton.getHeight());
        g.drawText ("Between Songs:", lbl, juce::Justification::centredLeft);
    }

    // "Beats per measure:" label
    if (! beatsEditor.getBounds().isEmpty())
    {
        g.setFont (displayFont (13.5f).boldened());
        g.setColour (juce::Colour (0xff3498db));
        juce::Rectangle<int> labelArea (
            area.getX() + 12,
            beatsEditor.getY(),
            beatsEditor.getX() - (area.getX() + 12) - 4,
            beatsEditor.getHeight());
        g.drawText ("Beats per measure:", labelArea, juce::Justification::centredLeft, false);
    }

    drawBeatLights (g);

    g.setFont (uiFont (13.0f));
    g.setColour (currentTheme.inkSoft);
    g.drawText ("Count-in auto-adapts to each song's BPM & meter",
                area.reduced (12, 4).removeFromBottom (20), juce::Justification::centredLeft);
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
            const auto redCol = juce::Colour (0xffe74c3c);
            for (int r = 6; r >= 1; --r)
            {
                const float alpha = 0.45f / static_cast<float> (r);
                g.setColour (redCol.withAlpha (alpha));
                g.drawRoundedRectangle (card.expanded (static_cast<float> (r)), 4.0f + static_cast<float> (r), 2.0f);
            }

            g.setColour (redCol);
            g.fillRoundedRectangle (card, 4.0f);
            drawSketchRect (g, card, juce::Colour (0xff962d22), 2.0f, static_cast<unsigned> (b + 20));

            g.setFont (displayFont (18.0f).boldened());
            g.setColour (juce::Colours::white);
            g.drawText (juce::String (b), card, juce::Justification::centred, false);
        }
        else if (isActive && b > 1)
        {
            const auto goldCol = juce::Colour (0xffffd166);
            for (int r = 5; r >= 1; --r)
            {
                const float alpha = 0.35f / static_cast<float> (r);
                g.setColour (goldCol.withAlpha (alpha));
                g.drawRoundedRectangle (card.expanded (static_cast<float> (r)), 4.0f + static_cast<float> (r), 1.8f);
            }

            g.setColour (goldCol);
            g.fillRoundedRectangle (card, 4.0f);
            drawSketchRect (g, card, currentTheme.panelEdge, 2.0f, static_cast<unsigned> (b + 20));

            g.setFont (displayFont (18.0f).boldened());
            g.setColour (juce::Colour (0xff1a1a1a));
            g.drawText (juce::String (b), card, juce::Justification::centred, false);
        }
        else
        {
            const auto amberCol = (b == 1) ? juce::Colour (0xffe74c3c).withAlpha (0.85f) : juce::Colour (0xfff39c12);
            g.setColour (amberCol);
            g.fillRoundedRectangle (card, 4.0f);
            drawSketchRect (g, card, currentTheme.panelEdge, 2.0f, static_cast<unsigned> (b + 20));

            g.setFont (displayFont (18.0f).boldened());
            g.setColour (b == 1 ? juce::Colours::white : juce::Colour (0xff101114));
            g.drawText (juce::String (b), card, juce::Justification::centred, false);
        }
    }
}

void RecordPracticeView::drawRecordingConsole (juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (area.toFloat(), 6.0f);
    drawSketchRect (g, area.toFloat(), currentTheme.panelEdge, 2.0f, 23);

    g.setFont (displayFont (15.5f).boldened());
    g.setColour (currentTheme.ink);
    g.drawText ("DUAL RECORDING CONSOLE", area.reduced (12, 8).removeFromTop (24), juce::Justification::centredLeft);

    auto statusArea = area.removeFromBottom (26).reduced (12, 0);
    juce::String statusStr = "STATUS: READY";
    juce::Colour statusCol = currentTheme.inkSoft;

    if (processor.getMetronome().isCountingIn())
    {
        statusStr = juce::String::formatted ("COUNT-IN: %d BEATS LEFT",
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
        statusStr = "REC ARMED (Press Play)";
        statusCol = juce::Colour (0xffe74c3c);
    }

    g.setFont (displayFont (12.0f).boldened());
    g.setColour (statusCol);
    g.drawText (statusStr, statusArea, juce::Justification::centredLeft, false);

    const int midiEvents = processor.getMidiRecorder().getEventCount();
    g.setFont (uiFont (13.0f));
    g.setColour (currentTheme.inkSoft);
    g.drawText (juce::String::formatted ("MIDI Events: %d", midiEvents), statusArea, juce::Justification::centredRight, false);
}

void RecordPracticeView::drawSongDatabaseModal (juce::Graphics& g)
{
    // Dimmed backdrop
    g.setColour (juce::Colour (0x88101114));
    g.fillAll();

    // Modal window card
    g.setColour (currentTheme.panel);
    g.fillRoundedRectangle (songDbModalBounds.toFloat(), 8.0f);
    drawSketchRect (g, songDbModalBounds.toFloat(), currentTheme.panelEdge, 3.0f, 701);

    const int mx = songDbModalBounds.getX() + 24;
    const int my = songDbModalBounds.getY() + 14;
    const int mr = songDbModalBounds.getRight() - 24;

    g.setFont (displayFont (19.5f).boldened());
    g.setColour (currentTheme.ink);
    g.drawText ("PERSISTENT SONG DATABASE  --  ADD TO SETLIST",
                mx, my, mr - mx - 100, 30, juce::Justification::centredLeft);

    g.setFont (uiFont (15.5f));
    g.setColour (currentTheme.inkSoft);
    g.drawText ("Songs configured in any past setlist remember their BPM, Beat 1 Offset, Meter, and Volume automatically.",
                mx, my + 32, mr - mx, 24, juce::Justification::centredLeft);

    // Table header
    const int thY = my + 110;
    const int thH = 26;
    const auto troughCol = currentTheme.background.interpolatedWith (currentTheme.panel, 0.35f);
    g.setColour (troughCol);
    g.fillRect (mx, thY, mr - mx, thH);
    g.setColour (currentTheme.panelEdge);
    g.drawRect (mx, thY, mr - mx, thH, 1);

    g.setFont (displayFont (11.5f));
    g.setColour (currentTheme.inkSoft);
    g.drawText ("SEL",               mx,       thY, 44,  thH, juce::Justification::centred);
    g.drawText ("SONG TITLE / FILE", mx + 44,  thY, 290, thH, juce::Justification::centredLeft);
    g.drawText ("DB STATUS",         mx + 334, thY, 140, thH, juce::Justification::centred);
    g.drawText ("SAVED BPM",         mx + 474, thY, 80,  thH, juce::Justification::centred);
    g.drawText ("BEAT 1 OFFSET",     mx + 554, thY, 95,  thH, juce::Justification::centred);
    g.drawText ("METER",             mx + 649, thY, 60,  thH, juce::Justification::centred);
    g.drawText ("ACTION",            mx + 709, thY, mr - (mx + 709), thH, juce::Justification::centred);

    // Filter songs by search query
    const auto query = songDbSearchEditor.getText().trim().toLowerCase();
    const auto& allSongs = processor.getSongDatabase().getSongs();
    std::vector<SongEntry> filtered;
    for (const auto& s : allSongs)
    {
        if (query.isEmpty() || s.title.toLowerCase().contains (query) || s.filePath.toLowerCase().contains (query))
            filtered.push_back (s);
    }

    songDbRowGeometries.clear();
    const int maxRows = 7;
    const int maxScroll = std::max (0, static_cast<int> (filtered.size()) - maxRows);
    songDbScrollOffset = std::clamp (songDbScrollOffset, 0, maxScroll);

    const int rowH = 44;
    const int rowGap = 5;
    const bool isDark = currentTheme.background.getPerceivedBrightness() < 0.35f;

    if (filtered.empty())
    {
        g.setFont (uiFont (16.0f));
        g.setColour (currentTheme.inkSoft);
        g.drawText ("No songs in library yet -- click '+ Import New Audio Files...' above to add tracks!",
                    mx, thY + 40, mr - mx, 120, juce::Justification::centred);
    }
    else
    {
        for (int i = 0; i < maxRows; ++i)
        {
            const int idx = songDbScrollOffset + i;
            if (idx >= static_cast<int> (filtered.size()))
                break;

            const auto& s = filtered[static_cast<size_t> (idx)];
            const bool isSel = (selectedSongDbIds.count (s.songId) > 0);
            const int ry = thY + thH + 6 + i * (rowH + rowGap);

            SongDbRowGeometry rg;
            rg.songId = s.songId;
            rg.rowBounds = { mx, ry, mr - mx, rowH };
            rg.checkboxBounds = { mx + 11, ry + 11, 22, 22 };
            rg.addBtnBounds = { mr - 76, ry + 7, 68, rowH - 14 };
            songDbRowGeometries.push_back (rg);

            const auto rbg = isSel
                                 ? (isDark ? juce::Colour (0xff2b2218) : juce::Colour (0xfffff3db))
                                 : ((i % 2 == 1) ? currentTheme.background.interpolatedWith (currentTheme.panel, 0.65f) : currentTheme.panel);
            g.setColour (rbg);
            g.fillRoundedRectangle (rg.rowBounds.toFloat(), 4.0f);
            drawSketchRect (g, rg.rowBounds.toFloat(), isSel ? juce::Colour (0xffea8c16) : currentTheme.panelEdge,
                            isSel ? 2.0f : 1.0f, static_cast<unsigned> (800 + i));

            // Checkbox
            g.setColour (isSel ? juce::Colour (0xffea8c16) : juce::Colours::white);
            g.fillRoundedRectangle (rg.checkboxBounds.toFloat(), 3.0f);
            drawSketchRect (g, rg.checkboxBounds.toFloat(), currentTheme.panelEdge, 2.0f, static_cast<unsigned> (850 + i));
            if (isSel)
            {
                juce::Path check;
                check.startNewSubPath (static_cast<float> (rg.checkboxBounds.getX() + 4), static_cast<float> (rg.checkboxBounds.getY() + 11));
                check.lineTo (static_cast<float> (rg.checkboxBounds.getX() + 9), static_cast<float> (rg.checkboxBounds.getBottom() - 4));
                check.lineTo (static_cast<float> (rg.checkboxBounds.getRight() - 4), static_cast<float> (rg.checkboxBounds.getY() + 5));
                g.setColour (juce::Colour (0xff101114));
                g.strokePath (check, juce::PathStrokeType (2.6f));
            }

            // Title
            g.setFont (displayFont (13.0f));
            g.setColour (currentTheme.ink);
            g.drawText (s.title, mx + 44, ry, 286, rowH, juce::Justification::centredLeft, true);

            // DB Status Pill
            juce::Rectangle<float> statusRect (static_cast<float> (mx + 338), static_cast<float> (ry + 9), 132.0f, static_cast<float> (rowH - 18));
            g.setColour (s.isConfigured ? juce::Colour (0xffa8e6cf) : juce::Colour (0xffffe0a3));
            g.fillRoundedRectangle (statusRect, 4.0f);
            g.setColour (currentTheme.panelEdge);
            g.drawRoundedRectangle (statusRect, 4.0f, 1.0f);
            g.setFont (displayFont (10.5f));
            g.setColour (juce::Colour (0xff101114));
            g.drawText (s.isConfigured ? "READY (Saved in DB)" : "NEW IMPORT (Auto-BPM)", statusRect, juce::Justification::centred, false);

            // Saved BPM, Beat 1 Offset, Meter
            g.setFont (displayFont (13.0f));
            g.setColour (s.isConfigured ? currentTheme.ink : juce::Colour (0xffd35400));
            g.drawText (juce::String::formatted ("%.1f", s.bpm), mx + 474, ry, 80, rowH, juce::Justification::centred);

            g.setFont (uiFont (15.0f).boldened());
            g.setColour (s.isConfigured ? juce::Colour (0xff27ae60) : juce::Colour (0xffd35400));
            g.drawText (juce::String::formatted ("%+.1f ms", s.firstBeatOffsetSec * 1000.0), mx + 554, ry, 95, rowH, juce::Justification::centred);

            g.setFont (displayFont (12.5f));
            g.setColour (currentTheme.ink);
            g.drawText (juce::String (s.beatsPerBar) + "/4", mx + 649, ry, 60, rowH, juce::Justification::centred);

            // Action button
            g.setColour (isSel ? juce::Colour (0xffa8e6cf) : currentTheme.panel);
            g.fillRoundedRectangle (rg.addBtnBounds.toFloat(), 4.0f);
            drawSketchRect (g, rg.addBtnBounds.toFloat(), currentTheme.panelEdge, 1.4f, static_cast<unsigned> (900 + i));
            g.setFont (displayFont (11.5f).boldened());
            g.setColour (isSel ? juce::Colour (0xff101114) : currentTheme.ink);
            g.drawText (isSel ? "Added" : "+ Add", rg.addBtnBounds, juce::Justification::centred, false);
        }
    }

    g.setFont (displayFont (13.0f));
    g.setColour (juce::Colour (0xffb35c00));
    g.drawText ("Tip: Any song configured in a past setlist automatically reuses its BPM, Beat 1 Offset, and Meter in new setlists!",
                mx, songDbModalBounds.getBottom() - 36, mr - mx, 24, juce::Justification::centredLeft);
}

} // namespace glorp
