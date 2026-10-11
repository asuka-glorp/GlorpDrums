#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>
#include <cmath>
#include <cassert>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    glorp::PluginProcessor proc;

    std::cout << "=== Testing GlorpDrums Audio Engine ===" << std::endl;
    std::cout << "Kit Loaded: " << proc.getSampler().isKitLoaded() << std::endl;
    std::cout << "Current Kit Path: " << proc.getCurrentKitPath().toStdString() << std::endl;
    std::cout << "Num Instruments: " << proc.getSampler().numInstruments() << std::endl;

    if (! proc.getSampler().isKitLoaded())
    {
        std::cerr << "FAIL: Kit not loaded!" << std::endl;
        return 1;
    }

    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    proc.prepareToPlay (sampleRate, blockSize);

    // 1. Test MIDI Note 38 (Snare)
    juce::AudioBuffer<float> buffer (2, blockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 110), 0);

    float peakSnare = 0.0f;
    for (int block = 0; block < 30; ++block)
    {
        buffer.clear();
        proc.processBlock (buffer, midi);
        midi.clear(); // only on first block

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* r = buffer.getReadPointer (ch);
            for (int i = 0; i < blockSize; ++i)
                peakSnare = std::max (peakSnare, std::abs (r[i]));
        }
    }

    const float dbSnare = 20.0f * std::log10 (std::max (1e-5f, peakSnare));
    std::cout << "Snare MIDI Trigger Peak: " << peakSnare << " (" << dbSnare << " dBFS)" << std::endl;

    if (peakSnare < 0.05f)
    {
        std::cerr << "FAIL: Snare audio is silent or too quiet!" << std::endl;
        return 1;
    }

    // 2. Test Direct Piece Click (DrumKitView onPieceHit emulation)
    std::cout << "\nTesting Direct Drum Hit (mouse click emulation)..." << std::endl;
    // Find Kick instrument
    int kickIdx = -1;
    for (int i = 0; i < proc.getSampler().numInstruments(); ++i)
    {
        if (proc.getSampler().instrumentName (i).containsIgnoreCase ("kick"))
        {
            kickIdx = i;
            break;
        }
    }
    std::cout << "Found Kick at index: " << kickIdx << std::endl;
    proc.getSampler().noteOn (kickIdx, 36, 0.90f, proc.getSampler().currentSamplePosition());

    float peakKick = 0.0f;
    for (int block = 0; block < 30; ++block)
    {
        buffer.clear();
        midi.clear();
        proc.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* r = buffer.getReadPointer (ch);
            for (int i = 0; i < blockSize; ++i)
                peakKick = std::max (peakKick, std::abs (r[i]));
        }
    }

    const float dbKick = 20.0f * std::log10 (std::max (1e-5f, peakKick));
    std::cout << "Kick Click Trigger Peak: " << peakKick << " (" << dbKick << " dBFS)" << std::endl;

    if (peakKick < 0.05f)
    {
        std::cerr << "FAIL: Kick click trigger is silent!" << std::endl;
        return 1;
    }

    // 3. Test Master Volume control
    std::cout << "\nTesting Master Volume attenuation..." << std::endl;
    if (auto* p = proc.findParameter ("masterLevel"))
        p->setValueNotifyingHost (0.1f); // lower master volume

    proc.getSampler().noteOn (kickIdx, 36, 0.90f, proc.getSampler().currentSamplePosition());
    float peakAttenuated = 0.0f;
    for (int block = 0; block < 30; ++block)
    {
        buffer.clear();
        midi.clear();
        proc.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* r = buffer.getReadPointer (ch);
            for (int i = 0; i < blockSize; ++i)
                peakAttenuated = std::max (peakAttenuated, std::abs (r[i]));
        }
    }
    std::cout << "Attenuated Kick Peak: " << peakAttenuated << " (vs original " << peakKick << ")" << std::endl;
    assert (peakAttenuated < peakKick * 0.5f);

    std::cout << "\nALL AUDIO TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
