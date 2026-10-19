#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Editor.h"
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    glorp::PluginProcessor proc;
    auto pluginEditor = proc.createEditorIfNeeded();
    if (! pluginEditor)
    {
        std::cout << "Failed to create editor!\n";
        return 1;
    }

    pluginEditor->setSize (1180, 860);
    std::cout << "Editor created. Bounds: " << pluginEditor->getBounds().toString().toStdString() << std::endl;
    std::cout << "Proc current kit: " << proc.getCurrentKitPath().toStdString() << std::endl;
    std::cout << "Proc scanned kit count: " << proc.getKitList().size() << std::endl;
    for (const auto& entry : proc.getKitList())
        std::cout << "  Kit: " << entry.displayName << " | valid=" << entry.valid << " | path=" << entry.drumkitPath << " | error=" << entry.error << std::endl;

    // 1. Take snapshot of default idle state
    {
        auto image = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
        juce::File outFile ("/home/servandoc/Documents/DEV/GlorpDrums/test/editor_snapshot.png");
        outFile.deleteFile();
        juce::FileOutputStream fos (outFile);
        if (fos.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image, fos);
            std::cout << "Saved idle snapshot to: " << outFile.getFullPathName().toStdString() << std::endl;
        }
    }

    // 2. Simulate hitting Snare drum (trigger hit + select instrument)
    // Find Snare instrument index
    int snareIdx = -1;
    for (int i = 0; i < proc.getSampler().numInstruments(); ++i)
    {
        if (proc.getSampler().instrumentName(i).containsIgnoreCase ("snare"))
        {
            snareIdx = i;
            break;
        }
    }

    if (snareIdx >= 0)
    {
        // Hit note on snare at max velocity (1.0f) like a click
        proc.getSampler().noteOn (snareIdx, 38, 1.0f, proc.getSampler().currentSamplePosition());
        
        // Drain sampler hit into editor timer
        if (auto* pe = dynamic_cast<glorp::PluginEditor*> (pluginEditor))
        {
            if (auto* ed = pe->findChildWithID ("") != nullptr ? pe : pe)
            {
                // Call timer callback on editor to process hit event
                // Find DrumKitView inside editor
            }
        }

        // Trigger hit animation directly on kit view
        for (auto* child : pluginEditor->getChildren())
        {
            if (auto* ed = dynamic_cast<glorp::Editor*> (child))
            {
                ed->selectInstrument (snareIdx);
                for (auto* edChild : ed->getChildren())
                {
                    if (auto* kv = dynamic_cast<glorp::DrumKitView*> (edChild))
                    {
                        kv->triggerHitForPiece (glorp::KitPiece::snare, 1.0f);
                    }
                }
            }
        }

        auto imageHit = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
        juce::File outHitFile ("/home/servandoc/Documents/DEV/GlorpDrums/test/editor_snapshot_hit.png");
        outHitFile.deleteFile();
        juce::FileOutputStream fos (outHitFile);
        if (fos.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (imageHit, fos);
            std::cout << "Saved hit snapshot to: " << outHitFile.getFullPathName().toStdString() << std::endl;
        }
    }

    // 3. Test multi-zone instrument (Hihat has HihatClosed and HihatOpen)
    int hihatIdx = -1;
    for (int i = 0; i < proc.getSampler().numInstruments(); ++i)
    {
        if (proc.getSampler().instrumentName(i).containsIgnoreCase ("hihat"))
        {
            hihatIdx = i;
            break;
        }
    }

    if (hihatIdx >= 0)
    {
        for (auto* child : pluginEditor->getChildren())
        {
            if (auto* ed = dynamic_cast<glorp::Editor*> (child))
            {
                ed->selectInstrument (hihatIdx);
                for (auto* edChild : ed->getChildren())
                {
                    if (auto* kv = dynamic_cast<glorp::DrumKitView*> (edChild))
                        kv->triggerHitForPiece (glorp::KitPiece::hihat, 1.0f);
                }
            }
        }

        auto imageHihat = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
        juce::File outHihatFile ("/home/servandoc/Documents/DEV/GlorpDrums/test/editor_snapshot_hihat.png");
        outHihatFile.deleteFile();
        juce::FileOutputStream fos (outHihatFile);
        if (fos.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (imageHihat, fos);
            std::cout << "Saved hihat snapshot to: " << outHihatFile.getFullPathName().toStdString() << std::endl;
        }
    }

    // 4. Test 4-zone instrument (Snare, SnareRest, SnareRim, SnareRimShot) matching user mockup
    {
        const auto tmpDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("glorp_4zone_test");
        tmpDir.createDirectory();

        const juce::String snareXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<instrument version="2.0">
  <samples>
    <sample name="1" power="1.0">
      <audiofile channel="OHL" file="s.wav" filechannel="1"/>
      <audiofile channel="OHR" file="s.wav" filechannel="2"/>
    </sample>
  </samples>
</instrument>)";

        tmpDir.getChildFile ("Snare.xml").replaceWithText (snareXml);
        // create dummy wav
        juce::AudioBuffer<float> buf (2, 256);
        buf.clear();
        juce::WavAudioFormat wav;
        auto wavFile = tmpDir.getChildFile ("s.wav");
        wavFile.deleteFile();
        if (auto fos = std::unique_ptr<juce::FileOutputStream> (wavFile.createOutputStream()))
        {
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (fos.get(), 44100.0, 2, 16, {}, 0)))
            {
                fos.release();
                writer->writeFromAudioSampleBuffer (buf, 0, 256);
            }
        }

        const juce::String drumkitXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<drumkit samplerate="44100" version="2.1.0">
  <channels>
    <channel name="OHL"/>
    <channel name="OHR"/>
  </channels>
  <instruments>
    <instrument name="Snare" file="Snare.xml"/>
    <instrument name="SnareRest" file="Snare.xml"/>
    <instrument name="SnareRim" file="Snare.xml"/>
    <instrument name="SnareRimShot" file="Snare.xml"/>
    <instrument name="Kick" file="Snare.xml"/>
    <instrument name="Tom1" file="Snare.xml"/>
    <instrument name="Tom2" file="Snare.xml"/>
    <instrument name="FTom1" file="Snare.xml"/>
  </instruments>
</drumkit>)";

        auto kitFile = tmpDir.getChildFile ("drumkit.xml");
        kitFile.replaceWithText (drumkitXml);

        proc.loadKit (kitFile.getFullPathName());

        // Hit snare and capture 4-zone layout
        for (auto* child : pluginEditor->getChildren())
        {
            if (auto* ed = dynamic_cast<glorp::Editor*> (child))
            {
                ed->selectInstrument (0); // Snare
                for (auto* edChild : ed->getChildren())
                {
                    if (auto* kv = dynamic_cast<glorp::DrumKitView*> (edChild))
                        kv->triggerHitForPiece (glorp::KitPiece::snare, 1.0f);
                }
            }
        }

        auto image4Zones = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
        juce::File out4ZoneFile ("/home/servandoc/Documents/DEV/GlorpDrums/test/editor_snapshot_4zones.png");
        out4ZoneFile.deleteFile();
        juce::FileOutputStream fos (out4ZoneFile);
        if (fos.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (image4Zones, fos);
            std::cout << "Saved 4-zone snapshot to: " << out4ZoneFile.getFullPathName().toStdString() << std::endl;
        }

        // 5. Test WYSIWYG edit mode
        for (auto* child : pluginEditor->getChildren())
        {
            if (auto* ed = dynamic_cast<glorp::Editor*> (child))
            {
                for (auto* edChild : ed->getChildren())
                {
                    if (auto* kv = dynamic_cast<glorp::DrumKitView*> (edChild))
                    {
                        kv->setEditMode (true);
                        kv->setSelectedPiece (glorp::KitPiece::snare);
                    }
                }
            }
        }

        auto imageEditMode = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
        juce::File outEditFile ("/home/servandoc/Documents/DEV/GlorpDrums/test/editor_snapshot_wysiwyg.png");
        outEditFile.deleteFile();
        juce::FileOutputStream fosEdit (outEditFile);
        if (fosEdit.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (imageEditMode, fosEdit);
            std::cout << "Saved WYSIWYG snapshot to: " << outEditFile.getFullPathName().toStdString() << std::endl;
        }

        // 6. Test Scope 6-Lane Drum Rhythm Ribbon with simultaneous hits
        std::cout << "Testing Scope 6-Lane Drum Rhythm Ribbon..." << std::endl;
        for (auto* child : pluginEditor->getChildren())
        {
            if (auto* ed = dynamic_cast<glorp::Editor*> (child))
            {
                for (auto* edChild : ed->getChildren())
                {
                    if (auto* kv = dynamic_cast<glorp::DrumKitView*> (edChild))
                        kv->setEditMode (false);

                    if (auto* sc = dynamic_cast<glorp::Scope*> (edChild))
                    {
                        const double t = juce::Time::getMillisecondCounterHiRes() * 0.001;

                        // 1. Simultaneous chord: Kick + Snare Rimshot + Hi-Hat Closed (Downbeat 1)
                        sc->hitReceived ("Kick", glorp::KitPiece::kick, 36, 110.0f / 127.0f, t);
                        sc->hitReceived ("Rimshot", glorp::KitPiece::snare, 40, 122.0f / 127.0f, t);
                        sc->hitReceived ("HihatClosed", glorp::KitPiece::hihat, 42, 75.0f / 127.0f, t);

                        // 2. Offbeat Hi-Hat ticks & Snare Ghost note
                        sc->hitReceived ("HihatClosed", glorp::KitPiece::hihat, 42, 65.0f / 127.0f, t - 0.35);
                        sc->hitReceived ("SnareHead", glorp::KitPiece::snare, 38, 38.0f / 127.0f, t - 0.70);
                        sc->hitReceived ("HihatClosed", glorp::KitPiece::hihat, 42, 70.0f / 127.0f, t - 1.05);

                        // 3. Tom fill: Tom 1, Tom 2, Floor Tom
                        sc->hitReceived ("Tom1Head", glorp::KitPiece::tom1, 48, 85.0f / 127.0f, t - 1.40);
                        sc->hitReceived ("Tom2Head", glorp::KitPiece::tom2, 45, 96.0f / 127.0f, t - 1.65);
                        sc->hitReceived ("FTomHead", glorp::KitPiece::floorTom, 41, 118.0f / 127.0f, t - 1.90);

                        // 4. Simultaneous Crash 1 + Kick accent
                        sc->hitReceived ("Crash1Edge", glorp::KitPiece::crash1, 49, 126.0f / 127.0f, t - 2.30);
                        sc->hitReceived ("Kick", glorp::KitPiece::kick, 36, 115.0f / 127.0f, t - 2.30);

                        // 5. Crash 2 & Ride hits
                        sc->hitReceived ("Crash2Bow", glorp::KitPiece::crash2, 89, 90.0f / 127.0f, t - 2.70);
                        sc->hitReceived ("RideBell", glorp::KitPiece::ride, 53, 98.0f / 127.0f, t - 3.00);

                        // 6. ETC / Aux Cowbell
                        sc->hitReceived ("Cowbell", glorp::KitPiece::count, 56, 105.0f / 127.0f, t - 3.25);
                    }
                }
            }
        }

        auto imageScope = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
        juce::File outScopeFile ("/home/servandoc/Documents/DEV/GlorpDrums/test/editor_snapshot_scope.png");
        outScopeFile.deleteFile();
        juce::FileOutputStream fosScope (outScopeFile);
        if (fosScope.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (imageScope, fosScope);
            std::cout << "Saved Scope snapshot to: " << outScopeFile.getFullPathName().toStdString() << std::endl;
        }
    }

    // 6. Capture E-Drum Wizard overlay snapshot
    {
        for (auto* child : pluginEditor->getChildren())
        {
            if (auto* ed = dynamic_cast<glorp::Editor*> (child))
            {
                ed->openEDrumWizard();
                break;
            }
        }

        auto imageWizard = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
        juce::File outWizardFile ("/home/servandoc/Documents/DEV/GlorpDrums/docs/edrum_wizard_screenshot.png");
        outWizardFile.deleteFile();
        juce::FileOutputStream fosWizard (outWizardFile);
        if (fosWizard.openedOk())
        {
            juce::PNGImageFormat png;
            png.writeImageToStream (imageWizard, fosWizard);
            std::cout << "Saved E-Drum Wizard snapshot to: " << outWizardFile.getFullPathName().toStdString() << std::endl;
        }
    }

    // 6. Test Mixer View snapshot
    for (auto* child : pluginEditor->getChildren())
    {
        if (auto* ed = dynamic_cast<glorp::Editor*> (child))
        {
            ed->setViewMode (glorp::Editor::EditorView::mixerView);
            auto imageMixer = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
            juce::File outMixerFile ("/home/servandoc/Documents/DEV/GlorpDrums/docs/mixer_view_screenshot.png");
            outMixerFile.deleteFile();
            juce::FileOutputStream fosMixer (outMixerFile);
            if (fosMixer.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (imageMixer, fosMixer);
                std::cout << "Saved Mixer View snapshot to: " << outMixerFile.getFullPathName().toStdString() << std::endl;
            }

            if (auto* mv = ed->getMixerView())
            {
                mv->openChannelFX (0);
                auto imageFX = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
                juce::File outFXFile ("/home/servandoc/Documents/DEV/GlorpDrums/docs/mixer_fx_screenshot.png");
                outFXFile.deleteFile();
                juce::FileOutputStream fosFX (outFXFile);
                if (fosFX.openedOk())
                {
                    juce::PNGImageFormat png;
                    png.writeImageToStream (imageFX, fosFX);
                    std::cout << "Saved Mixer FX Drawer snapshot to: " << outFXFile.getFullPathName().toStdString() << std::endl;
                }
            }

            // Create and load a test backing track file for authentic waveforms & beat grid
            juce::File backingWav ("/tmp/demo_backing_track.wav");
            backingWav.deleteFile();
            {
                const double sr = 44100.0;
                const int numSamples = static_cast<int> (sr * 16.0); // 16 seconds
                juce::AudioBuffer<float> backingBuf (2, numSamples);
                backingBuf.clear();
                const double bpm = 120.0;
                const int spb = static_cast<int> (sr * 60.0 / bpm);
                for (int s = 0; s < numSamples; ++s)
                {
                    int beatIndex = s / spb;
                    int beatOffset = s % spb;
                    float beatEnv = std::exp (-static_cast<float> (beatOffset) / 1200.0f);
                    float freq = (beatIndex % 4 == 0) ? 120.0f : 240.0f;
                    float val = std::sin (2.0f * juce::MathConstants<float>::pi * freq * beatOffset / static_cast<float> (sr)) * beatEnv * 0.75f;
                    backingBuf.setSample (0, s, val);
                    backingBuf.setSample (1, s, val);
                }
                juce::WavAudioFormat wav;
                auto fos = backingWav.createOutputStream();
                if (fos)
                {
                    std::unique_ptr<juce::AudioFormatWriter> writer (
                        wav.createWriterFor (fos.get(), sr, 2, 16, {}, 0));
                    if (writer)
                    {
                        fos.release();
                        writer->writeFromAudioSampleBuffer (backingBuf, 0, numSamples);
                    }
                }
            }
            proc.getBackingTrackPlayer().loadFile (backingWav);

            // 7. Test Practice & Record View snapshot (Light Theme matching mockup)
            ed->setViewMode (glorp::Editor::EditorView::practiceView);
            proc.getMetronome().setEnabled (true);
            proc.getMetronome().setCountInMode (glorp::Metronome::CountInMode::oneBar);
            if (auto* pv = ed->getPracticeView())
            {
                pv->setBeatsPerMeasure (5);
                pv->setAlsoRecordBackingTrack (true);
                pv->triggerBeatFlash (1, true);
            }

            auto imagePractice = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
            juce::File outPracticeFile ("/home/servandoc/Documents/DEV/GlorpDrums/docs/practice_view_screenshot.png");
            outPracticeFile.deleteFile();
            juce::FileOutputStream fosPractice (outPracticeFile);
            if (fosPractice.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (imagePractice, fosPractice);
                std::cout << "Saved Practice View snapshot to: " << outPracticeFile.getFullPathName().toStdString() << std::endl;
            }

            // 8. Test Dark Theme Overhaul (Kit View + Thick White Drumkit Outline + Dark Mixer View + Dark Practice View)
            ed->applyTheme (glorp::Theme::dark());
            ed->setViewMode (glorp::Editor::EditorView::kitView);

            // Trigger hit on Snare for dark theme hit snapshot
            for (auto* edChild : ed->getChildren())
            {
                if (auto* kv = dynamic_cast<glorp::DrumKitView*> (edChild))
                {
                    kv->triggerHitForPiece (glorp::KitPiece::snare, 1.0f);
                }
            }

            auto darkKitImg = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
            juce::File darkKitFile ("/home/servandoc/Documents/DEV/GlorpDrums/test/editor_snapshot_dark.png");
            darkKitFile.deleteFile();
            juce::FileOutputStream fosDarkKit (darkKitFile);
            if (fosDarkKit.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (darkKitImg, fosDarkKit);
                std::cout << "Saved Dark Kit View snapshot to: " << darkKitFile.getFullPathName().toStdString() << std::endl;
            }

            // Dark Mixer View snapshot
            ed->setViewMode (glorp::Editor::EditorView::mixerView);
            if (auto* mv = ed->getMixerView())
            {
                // Dismiss open popup so channel strips are fully exposed
                mv->closePopups();
            }
            auto darkMixerImg = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
            juce::File darkMixerFile ("/home/servandoc/Documents/DEV/GlorpDrums/test/mixer_view_snapshot_dark.png");
            darkMixerFile.deleteFile();
            juce::FileOutputStream fosDarkMixer (darkMixerFile);
            if (fosDarkMixer.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (darkMixerImg, fosDarkMixer);
                std::cout << "Saved Dark Mixer View snapshot to: " << darkMixerFile.getFullPathName().toStdString() << std::endl;
            }

            // Dark Practice View snapshot
            ed->setViewMode (glorp::Editor::EditorView::practiceView);
            auto darkPracticeImg = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
            juce::File darkPracticeFile ("/home/servandoc/Documents/DEV/GlorpDrums/docs/practice_view_snapshot_dark.png");
            darkPracticeFile.deleteFile();
            juce::FileOutputStream fosDarkPractice (darkPracticeFile);
            if (fosDarkPractice.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (darkPracticeImg, fosDarkPractice);
                std::cout << "Saved Dark Practice View snapshot to: " << darkPracticeFile.getFullPathName().toStdString() << std::endl;
            }

            // Dark Practice View with REC armed (red pulsing halo glow)
            for (auto* edChild : ed->getChildren())
            {
                if (auto* pv = dynamic_cast<glorp::RecordPracticeView*> (edChild))
                {
                    for (auto* pvChild : pv->getChildren())
                    {
                        if (auto* btn = dynamic_cast<juce::Button*> (pvChild))
                        {
                            if (btn->getButtonText() == "REC" || btn->getName() == "REC")
                            {
                                if (btn->onClick)
                                    btn->onClick();
                            }
                        }
                    }
                }
            }

            auto armedPracticeImg = pluginEditor->createComponentSnapshot (pluginEditor->getLocalBounds());
            juce::File armedPracticeFile ("/home/servandoc/Documents/DEV/GlorpDrums/docs/practice_view_rec_armed.png");
            armedPracticeFile.deleteFile();
            juce::FileOutputStream fosArmed (armedPracticeFile);
            if (fosArmed.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (armedPracticeImg, fosArmed);
                std::cout << "Saved Armed Practice View snapshot to: " << armedPracticeFile.getFullPathName().toStdString() << std::endl;
            }

            break;
        }
    }

    // Update main docs/screenshot.png with latest UI
    {
        // Re-create idle editor for clean main screenshot
        glorp::PluginProcessor cleanProc;
        auto cleanEditor = cleanProc.createEditorIfNeeded();
        if (cleanEditor)
        {
            cleanEditor->setSize (1180, 860);
            auto cleanImage = cleanEditor->createComponentSnapshot (cleanEditor->getLocalBounds());
            juce::File mainShot ("/home/servandoc/Documents/DEV/GlorpDrums/docs/screenshot.png");
            mainShot.deleteFile();
            juce::FileOutputStream fosMain (mainShot);
            if (fosMain.openedOk())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (cleanImage, fosMain);
                std::cout << "Updated main screenshot: " << mainShot.getFullPathName().toStdString() << std::endl;
            }
        }
    }

    return 0;
}
