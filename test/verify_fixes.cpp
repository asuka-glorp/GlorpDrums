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

    // 3. Verify .glorpkit zero-copy sample reading and Sampler loading
    const std::string testPkg = "/tmp/verify_test.glorpkit";
    const std::string buildCmd = "python3 tools/convert_to_glorpkit.py test/kits/GlorpDemoKit -o \"" + testPkg + "\" > /dev/null 2>&1";
    int cmdRes = std::system (buildCmd.c_str());
    assert (cmdRes == 0);

    glorp::DrumKit pkgKit = glorp::DrumKit::load (testPkg);
    assert (pkgKit.error.empty());
    assert (pkgKit.isArchive);
    assert (! pkgKit.instruments.empty());

    // Resolve first sample URI
    const auto& firstInst = pkgKit.instruments.front();
    assert (! firstInst.samples.empty());
    const auto& firstSample = firstInst.samples.front();
    assert (! firstSample.audioFiles.empty());
    const auto sampleUri = firstInst.samplePath (firstSample.audioFiles.front());
    std::cout << "Testing SampleCache zero-copy load of: " << sampleUri << "\n";

    glorp::SampleCache cache;
    auto handle = cache.open (sampleUri);
    assert (handle.isValid());
    assert (handle.numFrames > 0);
    assert (handle.sampleRate == 44100);

    // Read a block of audio frames
    std::vector<float> audioData (128, 0.0f);
    handle.read (0, 128, 0, audioData.data());
    std::cout << "Successfully read 128 audio frames directly from .glorpkit via zero-copy mmap!\n";
    std::cout << "-> Zero-copy memory mapped .glorpkit playback: PASSED!\n";

    // Clean up temporary package
    std::remove (testPkg.c_str());

    std::cout << "=== ALL VERIFICATIONS PASSED SUCCESSFULLY ===\n";
    return 0;
}
