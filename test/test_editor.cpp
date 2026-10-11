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
    std::cout << "Proc instruments count: " << proc.getSampler().numInstruments() << std::endl;
    for (int i = 0; i < proc.getSampler().numInstruments(); ++i)
        std::cout << "  Inst " << i << ": " << proc.getSampler().instrumentName(i).toStdString() << std::endl;

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
    }

    return 0;
}
