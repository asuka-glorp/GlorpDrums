#include "DrumKitView.h"
#include "Sampler.h"
#include "PluginProcessor.h"
#include <iostream>
#include <cassert>

int main()
{
    std::cout << "=== Running GlorpDrums Verification Suite ===\n";

    // 1. Verify Cowbell vs Ride mapping
    auto cowbellPiece = glorp::DrumKitView::pieceForName ("Cowbell");
    auto cowbellMuffPiece = glorp::DrumKitView::pieceForName ("CowbellMuff");
    auto ridePiece = glorp::DrumKitView::pieceForName ("RideBow");
    auto rideBellPiece = glorp::DrumKitView::pieceForName ("RideBell");

    std::cout << "Cowbell mapped to: " << static_cast<int> (cowbellPiece)
              << " (expected: " << static_cast<int> (glorp::KitPiece::count) << ")\n";
    std::cout << "CowbellMuff mapped to: " << static_cast<int> (cowbellMuffPiece)
              << " (expected: " << static_cast<int> (glorp::KitPiece::count) << ")\n";
    std::cout << "RideBow mapped to: " << static_cast<int> (ridePiece)
              << " (expected: " << static_cast<int> (glorp::KitPiece::ride) << ")\n";
    std::cout << "RideBell mapped to: " << static_cast<int> (rideBellPiece)
              << " (expected: " << static_cast<int> (glorp::KitPiece::ride) << ")\n";

    assert (cowbellPiece == glorp::KitPiece::count);
    assert (cowbellMuffPiece == glorp::KitPiece::count);
    assert (ridePiece == glorp::KitPiece::ride);
    assert (rideBellPiece == glorp::KitPiece::ride);
    std::cout << "-> Cowbell visual mapping fix: PASSED!\n";

    // 2. Verify Multi-out mode in Sampler & PluginProcessor
    glorp::Sampler sampler;
    std::cout << "Sampler multiOutMode default: " << (sampler.isMultiOutMode() ? "ENABLED" : "DISABLED") << "\n";
    assert (sampler.isMultiOutMode() == true);

    juce::ScopedJuceInitialiser_GUI gui;
    glorp::PluginProcessor processor;
    std::cout << "PluginProcessor sampler multiOutMode: "
              << (processor.getSampler().isMultiOutMode() ? "ENABLED" : "DISABLED") << "\n";
    assert (processor.getSampler().isMultiOutMode() == true);

    // Verify isBusesLayoutSupported
    juce::AudioProcessor::BusesLayout stereoOnly;
    stereoOnly.outputBuses.add (juce::AudioChannelSet::stereo());
    assert (processor.isBusesLayoutSupported (stereoOnly) == true);

    juce::AudioProcessor::BusesLayout multiBus16;
    for (int i = 0; i < 16; ++i)
        multiBus16.outputBuses.add (juce::AudioChannelSet::stereo());
    assert (processor.isBusesLayoutSupported (multiBus16) == true);

    juce::AudioProcessor::BusesLayout invalidInput;
    invalidInput.inputBuses.add (juce::AudioChannelSet::stereo());
    invalidInput.outputBuses.add (juce::AudioChannelSet::stereo());
    assert (processor.isBusesLayoutSupported (invalidInput) == false);

    std::cout << "-> Multi-out bus layout & routing verification: PASSED!\n";

    std::cout << "=== ALL VERIFICATIONS PASSED SUCCESSFULLY ===\n";
    return 0;
}
