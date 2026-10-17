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

    // 4. Verify Tom 2, Tom 1, and Floor Tom pieceForName mapping
    assert (glorp::DrumKitView::pieceForName ("Tom 2") == glorp::KitPiece::tom2);
    assert (glorp::DrumKitView::pieceForName ("Tom2") == glorp::KitPiece::tom2);
    assert (glorp::DrumKitView::pieceForName ("Tom2Head") == glorp::KitPiece::tom2);
    assert (glorp::DrumKitView::pieceForName ("Tom 13") == glorp::KitPiece::tom2);
    assert (glorp::DrumKitView::pieceForName ("Tom 13 Signia ctr") == glorp::KitPiece::tom2);
    assert (glorp::DrumKitView::pieceForName ("Tom 1") == glorp::KitPiece::tom1);
    assert (glorp::DrumKitView::pieceForName ("Tom1Head") == glorp::KitPiece::tom1);
    assert (glorp::DrumKitView::pieceForName ("Tom 12 Signia ctr") == glorp::KitPiece::tom1);
    assert (glorp::DrumKitView::pieceForName ("Floor Tom") == glorp::KitPiece::floorTom);
    assert (glorp::DrumKitView::pieceForName ("FTomHead") == glorp::KitPiece::floorTom);
    assert (glorp::DrumKitView::pieceForName ("Tom 16 Signia ctr") == glorp::KitPiece::floorTom);
    std::cout << "-> Tom 1, Tom 2, and Floor Tom mapping verification: PASSED!\n";

    // 5. Verify Crash 1 & Crash 2 pieceForName mapping
    assert (glorp::DrumKitView::pieceForName ("Crash 1") == glorp::KitPiece::crash1);
    assert (glorp::DrumKitView::pieceForName ("Crash 1 bow") == glorp::KitPiece::crash1);
    assert (glorp::DrumKitView::pieceForName ("Crash 1 edge") == glorp::KitPiece::crash1);
    assert (glorp::DrumKitView::pieceForName ("Crash 2") == glorp::KitPiece::crash2);
    assert (glorp::DrumKitView::pieceForName ("Crash 2 bow") == glorp::KitPiece::crash2);
    assert (glorp::DrumKitView::pieceForName ("Crash 2 edge") == glorp::KitPiece::crash2);
    std::cout << "-> Crash 1 & Crash 2 piece mapping verification: PASSED!\n";

    // 6. Verify ReaperDrummer preset availability and note mapping
    const auto presets = glorp::MidiMap::availablePresets();
    bool foundReaperDrummer = false;
    for (const auto& p : presets)
    {
        if (p.name == "ReaperDrummer")
        {
            foundReaperDrummer = true;
            assert (p.noteToInstrumentName.at (36) == "Kick");
            assert (p.noteToInstrumentName.at (38) == "SnareHead");
            assert (p.noteToInstrumentName.at (40) == "Rimshot");
            assert (p.noteToInstrumentName.at (37) == "RimClick");
            assert (p.noteToInstrumentName.at (42) == "HihatClosed");
            assert (p.noteToInstrumentName.at (46) == "HihatOpen");
            assert (p.noteToInstrumentName.at (44) == "HihatPedal");
            assert (p.noteToInstrumentName.at (63) == "HihatSplash");
            assert (p.noteToInstrumentName.at (23) == "HihatOpen1Bow");
            assert (p.noteToInstrumentName.at (24) == "HihatOpen1Edge");
            assert (p.noteToInstrumentName.at (49) == "Crash1Bow");
            assert (p.noteToInstrumentName.at (55) == "Crash1Edge");
            assert (p.noteToInstrumentName.at (57) == "Crash2Bow");
            assert (p.noteToInstrumentName.at (52) == "Crash2Edge");
            assert (p.noteToInstrumentName.at (48) == "Tom1Head");
            assert (p.noteToInstrumentName.at (50) == "Tom1Rim");
            assert (p.noteToInstrumentName.at (47) == "Tom2Head");
            assert (p.noteToInstrumentName.at (60) == "Tom2Rim");
            assert (p.noteToInstrumentName.at (43) == "FTomHead");
            assert (p.noteToInstrumentName.at (58) == "FTomRim");
            break;
        }
    }
    assert (foundReaperDrummer);
    std::cout << "-> ReaperDrummer built-in preset verification: PASSED!\n";

    // 7. Verify suggestedMap matching with dots and spaces
    std::vector<juce::String> kitInsts = {
        "Kick", "Snare head", "Crash 1 bow", "Crash 1 edge", "Crash 2 bow", "Crash 2 edge",
        "Hihat closed", "Hihat open", "Hihat open1-bow", "Hihat splash",
        "Tom 1 head", "Tom 2 head", "Floor Tom head"
    };
    glorp::MidiMap mmap;
    for (const auto& p : presets)
    {
        if (p.name == "ReaperDrummer")
        {
            mmap.applyPreset (p, kitInsts);
            break;
        }
    }
    // Verify Crash 1 bow vs edge mapped to distinct indices
    int c1b = mmap.instrumentForNote (49);
    int c1e = mmap.instrumentForNote (55);
    int c2b = mmap.instrumentForNote (57);
    int c2e = mmap.instrumentForNote (52);
    int t1 = mmap.instrumentForNote (48);
    int t2 = mmap.instrumentForNote (47);
    int hhClosed = mmap.instrumentForNote (42);
    int hhOpen = mmap.instrumentForNote (46);
    int hhOpen1 = mmap.instrumentForNote (23);
    int hhSplash = mmap.instrumentForNote (63);

    assert (c1b >= 0 && c1e >= 0 && c1b != c1e);
    assert (c2b >= 0 && c2e >= 0 && c2b != c2e);
    assert (t1 >= 0 && t2 >= 0 && t1 != t2);
    assert (hhClosed >= 0 && hhOpen >= 0 && hhClosed != hhOpen);
    assert (hhOpen1 >= 0 && hhSplash >= 0);
    assert (kitInsts[static_cast<size_t> (c1b)] == "Crash 1 bow");
    assert (kitInsts[static_cast<size_t> (c1e)] == "Crash 1 edge");
    assert (kitInsts[static_cast<size_t> (c2b)] == "Crash 2 bow");
    assert (kitInsts[static_cast<size_t> (c2e)] == "Crash 2 edge");
    assert (kitInsts[static_cast<size_t> (t2)] == "Tom 2 head");
    std::cout << "-> MidiMap resolution of ReaperDrummer articulations: PASSED!\n";

    std::cout << "=== ALL VERIFICATIONS PASSED SUCCESSFULLY ===\n";
    return 0;
}
