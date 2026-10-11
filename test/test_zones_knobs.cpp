#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Editor.h"
#include "Knob.h"
#include <iostream>
#include <cmath>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    glorp::PluginProcessor proc;
    const auto localKit = juce::File::getCurrentWorkingDirectory().getChildFile ("test/kits/GlorpDemoKit/drumkit.xml");
    if (localKit.existsAsFile())
        proc.loadKit (localKit.getFullPathName());
    proc.prepareToPlay (44100.0, 512);

    std::cout << "Kit loaded: " << proc.getCurrentKitPath().toStdString() << "\n";
    std::cout << "Num instruments: " << proc.getSampler().numInstruments() << "\n";

    for (int i = 0; i < proc.getSampler().numInstruments(); ++i)
    {
        std::cout << "Instrument " << i << ": " << proc.getSampler().instrumentName(i).toStdString()
                  << " note: " << proc.getMidiMap().noteForInstrument(i)
                  << " bus: " << proc.getSampler().getInstrumentBus(i) << "\n";
    }

    // Let's create the editor
    auto* editor = proc.createEditorIfNeeded();
    if (! editor)
    {
        std::cout << "Failed to create editor\n";
        return 1;
    }
    editor->setSize (1180, 860);

    glorp::Editor* coreEditor = nullptr;
    for (auto* c : editor->getChildren())
    {
        if (auto* ed = dynamic_cast<glorp::Editor*> (c))
        {
            coreEditor = ed;
            break;
        }
    }

    if (! coreEditor)
    {
        std::cout << "Failed to find Editor component\n";
        return 1;
    }

    std::cout << "\nLogical instruments in Editor: " << coreEditor->getLogicalInstruments().size() << "\n";
    for (size_t l = 0; l < coreEditor->getLogicalInstruments().size(); ++l)
    {
        const auto& inst = coreEditor->getLogicalInstruments()[l];
        std::cout << "  Logical " << l << ": " << inst.name.toStdString()
                  << " zones: " << inst.zones.size() << "\n";
        for (size_t z = 0; z < inst.zones.size(); ++z)
        {
            std::cout << "    Zone " << z << ": " << inst.zones[z].zoneName.toStdString()
                      << " (samplerIdx " << inst.zones[z].samplerIndex << ")\n";
        }
    }

    // Now test testing parameters for each zone!
    for (size_t l = 0; l < coreEditor->getLogicalInstruments().size(); ++l)
    {
        const auto& inst = coreEditor->getLogicalInstruments()[l];
        for (size_t z = 0; z < inst.zones.size(); ++z)
        {
            int sIdx = inst.zones[z].samplerIndex;
            std::cout << "\n--- Testing " << inst.name.toStdString() << " / Zone " << inst.zones[z].zoneName.toStdString()
                      << " (samplerIdx " << sIdx << ") ---\n";

            coreEditor->selectLogicalInstrument (static_cast<int> (l));
            coreEditor->selectZone (static_cast<int> (l), static_cast<int> (z), false);

            int activeIdx = coreEditor->getActiveSamplerIndex();
            std::cout << "Active sampler index in editor: " << activeIdx << "\n";

            auto* volParam = proc.findParameter ("inst" + juce::String (sIdx) + ".level");
            auto* panParam = proc.findParameter ("inst" + juce::String (sIdx) + ".pan");
            auto* pitchParam = proc.findParameter ("inst" + juce::String (sIdx) + ".pitch");
            auto* dynParam = proc.findParameter ("inst" + juce::String (sIdx) + ".dynamics");

            std::cout << "Params found: vol=" << (volParam != nullptr)
                      << " pan=" << (panParam != nullptr)
                      << " pitch=" << (pitchParam != nullptr)
                      << " dyn=" << (dynParam != nullptr) << "\n";

            // Test Volume change
            if (volParam)
            {
                proc.getSampler().allNotesOff();
                volParam->setValueNotifyingHost (0.1f); // quiet
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                int note = proc.getMidiMap().noteForInstrument (sIdx);
                if (note >= 0)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
                    proc.processBlock (buf, midi);
                    float peakQuiet = buf.getMagnitude (0, 512);

                    proc.getSampler().allNotesOff();
                    volParam->setValueNotifyingHost (0.9f); // loud
                    juce::AudioBuffer<float> bufLoud (2, 512);
                    juce::MidiBuffer midi2;
                    midi2.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
                    proc.processBlock (bufLoud, midi2);
                    float peakLoud = bufLoud.getMagnitude (0, 512);

                    std::cout << "Volume test: quiet peak=" << peakQuiet << ", loud peak=" << peakLoud << "\n";
                }
                else
                {
                    std::cout << "Warning: No MIDI note assigned to instrument " << sIdx << "!\n";
                }
            }

            // Test Pan change
            if (panParam)
            {
                proc.getSampler().allNotesOff();
                panParam->setValueNotifyingHost (0.0f); // Hard Left
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer midi;
                int note = proc.getMidiMap().noteForInstrument (sIdx);
                if (note >= 0)
                {
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
                    proc.processBlock (buf, midi);
                    float leftMag = buf.getMagnitude (0, 0, 512);
                    float rightMag = buf.getMagnitude (1, 0, 512);
                    std::cout << "Pan Hard Left test: L=" << leftMag << " R=" << rightMag << "\n";

                    proc.getSampler().allNotesOff();
                    panParam->setValueNotifyingHost (1.0f); // Hard Right
                    juce::AudioBuffer<float> bufR (2, 512);
                    juce::MidiBuffer midiR;
                    midiR.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
                    proc.processBlock (bufR, midiR);
                    float leftMagR = bufR.getMagnitude (0, 0, 512);
                    float rightMagR = bufR.getMagnitude (1, 0, 512);
                    std::cout << "Pan Hard Right test: L=" << leftMagR << " R=" << rightMagR << "\n";
                }
            }

            // Test Pitch change (-12 semitones vs +12 semitones)
            if (pitchParam)
            {
                proc.getSampler().allNotesOff();
                pitchParam->setValueNotifyingHost (0.0f); // -12 semitones
                int note = proc.getMidiMap().noteForInstrument (sIdx);
                if (note >= 0)
                {
                    float peakD = 0.0f;
                    juce::AudioBuffer<float> bufDown (2, 512);
                    juce::MidiBuffer midiD;
                    midiD.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
                    for (int b = 0; b < 8; ++b)
                    {
                        proc.processBlock (bufDown, midiD);
                        peakD = std::max (peakD, bufDown.getMagnitude (0, 512));
                        midiD.clear();
                    }

                    proc.getSampler().allNotesOff();
                    pitchParam->setValueNotifyingHost (1.0f); // +12 semitones
                    float peakU = 0.0f;
                    juce::AudioBuffer<float> bufUp (2, 512);
                    juce::MidiBuffer midiU;
                    midiU.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 0);
                    for (int b = 0; b < 8; ++b)
                    {
                        proc.processBlock (bufUp, midiU);
                        peakU = std::max (peakU, bufUp.getMagnitude (0, 512));
                        midiU.clear();
                    }

                    std::cout << "Pitch test: -12st peak=" << peakD << ", +12st peak=" << peakU << "\n";
                }
            }

            // Test Dynamics change (0.0 compressed vs 1.0 expanded at moderate velocity)
            if (dynParam)
            {
                proc.getSampler().allNotesOff();
                pitchParam->setValueNotifyingHost (0.5f); // reset pitch to 0
                dynParam->setValueNotifyingHost (0.0f); // compressed -> softer note should be boosted
                int note = proc.getMidiMap().noteForInstrument (sIdx);
                if (note >= 0)
                {
                    float peakComp = 0.0f;
                    juce::AudioBuffer<float> bufComp (2, 512);
                    juce::MidiBuffer midiC;
                    midiC.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 60), 0);
                    for (int b = 0; b < 4; ++b)
                    {
                        proc.processBlock (bufComp, midiC);
                        peakComp = std::max (peakComp, bufComp.getMagnitude (0, 512));
                        midiC.clear();
                    }

                    proc.getSampler().allNotesOff();
                    dynParam->setValueNotifyingHost (1.0f); // expanded -> softer note should be much quieter
                    float peakExp = 0.0f;
                    juce::AudioBuffer<float> bufExp (2, 512);
                    juce::MidiBuffer midiE;
                    midiE.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 60), 0);
                    for (int b = 0; b < 4; ++b)
                    {
                        proc.processBlock (bufExp, midiE);
                        peakExp = std::max (peakExp, bufExp.getMagnitude (0, 512));
                        midiE.clear();
                    }

                    std::cout << "Dynamics test (vel=60): compressed peak=" << peakComp << ", expanded peak=" << peakExp << "\n";
                }
            }
        }
    }

    // Test Choking functionality
    std::cout << "\n--- Testing Choke Behavior ---\n";
    {
        // 1. Crash1 Choke test (Crash1Choke chokes Crash1Edge)
        proc.getSampler().allNotesOff();
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 49, (juce::uint8) 127), 0); // Crash1Edge
        proc.processBlock (buf, midi);
        float edgeRinging = buf.getMagnitude (0, 512);
        std::cout << "Crash1Edge hit: peak=" << edgeRinging << "\n";

        // Choke it with Crash1Choke
        juce::MidiBuffer chokeMidi;
        chokeMidi.addEvent (juce::MidiMessage::noteOn (1, 54, (juce::uint8) 127), 0); // Crash1Choke
        proc.processBlock (buf, chokeMidi);

        // Render 10 blocks (~116ms). Crash1Choke lasts 55ms.
        float postChokePeak = 0.0f;
        for (int b = 0; b < 10; ++b)
        {
            buf.clear();
            chokeMidi.clear();
            proc.processBlock (buf, chokeMidi);
            if (b >= 7) // past 80ms
                postChokePeak = std::max (postChokePeak, buf.getMagnitude (0, 512));
        }
        std::cout << "Crash1 after choke: post-choke peak (at 100ms)=" << postChokePeak
                  << (postChokePeak < 0.05f ? " [CHOKE PASSED]" : " [CHOKE FAILED]") << "\n";
    }
    {
        // 2. Ride Choke test (RideChoke chokes RideBow)
        proc.getSampler().allNotesOff();
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 51, (juce::uint8) 127), 0); // RideBow
        proc.processBlock (buf, midi);
        float rideRinging = buf.getMagnitude (0, 512);
        std::cout << "RideBow hit: peak=" << rideRinging << "\n";

        // Choke with RideChoke
        juce::MidiBuffer chokeMidi;
        chokeMidi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0); // RideChoke
        proc.processBlock (buf, chokeMidi);

        float postChokePeak = 0.0f;
        for (int b = 0; b < 10; ++b)
        {
            buf.clear();
            chokeMidi.clear();
            proc.processBlock (buf, chokeMidi);
            if (b >= 7) // past 80ms
                postChokePeak = std::max (postChokePeak, buf.getMagnitude (0, 512));
        }
        std::cout << "Ride after choke: post-choke peak (at 100ms)=" << postChokePeak
                  << (postChokePeak < 0.05f ? " [CHOKE PASSED]" : " [CHOKE FAILED]") << "\n";
    }
    {
        // 3. Hihat Choke test (Closed chokes Open)
        proc.getSampler().allNotesOff();
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 46, (juce::uint8) 127), 0); // HihatOpen
        proc.processBlock (buf, midi);
        float openRinging = buf.getMagnitude (0, 512);
        std::cout << "HihatOpen hit: peak=" << openRinging << "\n";

        // Choke with closed hat (decay is 65ms)
        juce::MidiBuffer chokeMidi;
        chokeMidi.addEvent (juce::MidiMessage::noteOn (1, 42, (juce::uint8) 127), 0); // HihatClosed
        proc.processBlock (buf, chokeMidi);

        float postChokePeak = 0.0f;
        for (int b = 0; b < 10; ++b)
        {
            buf.clear();
            chokeMidi.clear();
            proc.processBlock (buf, chokeMidi);
            if (b >= 7) // past 80ms
                postChokePeak = std::max (postChokePeak, buf.getMagnitude (0, 512));
        }
        std::cout << "Hihat after closed choke: post-choke peak (at 100ms)=" << postChokePeak
                  << (postChokePeak < 0.05f ? " [CHOKE PASSED]" : " [CHOKE FAILED]") << "\n";
    }

    std::cout << "Done testing!\n";
    return 0;
}
