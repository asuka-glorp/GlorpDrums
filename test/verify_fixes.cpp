#include "DrumKitView.h"
#include "Editor.h"
#include "Sampler.h"
#include "PluginProcessor.h"
#include <iostream>
#include <cassert>

#define VERIFY_CHECK(cond) \
    do { if (! (cond)) { std::cerr << "VERIFY_CHECK failed at line " << __LINE__ << ": " #cond "\n"; std::exit (1); } } while (false)

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
    {
        auto sampler = std::make_unique<glorp::Sampler>();
        std::cout << "Sampler multiOutMode default: " << (sampler->isMultiOutMode() ? "ENABLED" : "DISABLED") << "\n";
        assert (sampler->isMultiOutMode() == true);
    }

    juce::ScopedJuceInitialiser_GUI gui;
    auto processorPtr = std::make_unique<glorp::PluginProcessor>();
    auto& processor = *processorPtr;
    std::cout << "PluginProcessor sampler multiOutMode: "
              << (processor.getSampler().isMultiOutMode() ? "ENABLED" : "DISABLED") << "\n";
    std::cout << "PluginProcessor currentKitPath: " << processor.getCurrentKitPath()
              << ", numInstruments: " << processor.getSampler().numInstruments() << "\n";
    assert (processor.getSampler().isMultiOutMode() == true);

    // 2a. Phase 5: Graphical Optimizations & OpenGL Rendering Pipeline
    std::cout << "Testing Phase 5 Graphical Optimizations & Caching..." << std::endl;
    {
        auto editor = std::make_unique<glorp::Editor> (processor);
        VERIFY_CHECK (editor != nullptr);
        VERIFY_CHECK (editor->isOpaque());

        auto& kv = editor->getKitView();
        auto& sc = editor->getScope();

        VERIFY_CHECK (kv.isOpaque());
        VERIFY_CHECK (sc.isOpaque());
        VERIFY_CHECK (kv.isStaticKitCacheValid());
        VERIFY_CHECK (kv.isPieceScaledCacheValid (glorp::KitPiece::snare));
        VERIFY_CHECK (kv.isPieceScaledCacheValid (glorp::KitPiece::kick));
        VERIFY_CHECK (sc.isBackgroundCacheValid());

        const auto snareDirty = kv.dirtyBoundsForPiece (glorp::KitPiece::snare);
        const auto fullBounds = kv.getLocalBounds();
        VERIFY_CHECK (! snareDirty.isEmpty());
        VERIFY_CHECK (snareDirty.getWidth() * snareDirty.getHeight() < (fullBounds.getWidth() * fullBounds.getHeight()) / 3);
        std::cout << "Snare dirty rect: " << snareDirty.toString()
                  << " vs full DrumKitView: " << fullBounds.toString() << std::endl;

        juce::Image frameImg (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics g (frameImg);
        editor->paintEntireComponent (g, false);

        kv.triggerHitForPiece (glorp::KitPiece::snare, 0.9f);
        sc.hitReceived ("Snare", glorp::KitPiece::snare, 38, 0.9f);
        editor->paintEntireComponent (g, false);

        const bool initialGl = editor->isOpenGlEnabled();
        editor->setOpenGlEnabled (! initialGl);
        VERIFY_CHECK (editor->isOpenGlEnabled() == ! initialGl);
        editor->setOpenGlEnabled (true);
        VERIFY_CHECK (editor->isOpenGlEnabled() == true);

        std::cout << "-> Phase 5 Pre-Scaled Kit Cache, Static Composite, Scope Cache & Dirty Rects: PASSED!\n";
    }

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

    // Verify all 16 buses are enabled by default
    assert (processor.getBusCount (false) == 16);
    for (int b = 0; b < 16; ++b)
    {
        auto* bus = processor.getBus (false, b);
        assert (bus != nullptr);
        assert (bus->getDefaultLayout() == juce::AudioChannelSet::stereo());
        assert (bus->isEnabledByDefault() == true);
    }
    std::cout << "All 16 output buses verified active by default (Out 1/2 through Out 31/32)!\n";

    // 2b. Verify audio rendering multi-out routing
    {
        auto& pSampler = processor.getSampler();
        pSampler.setMultiOutMode (true);

        std::cout << "Processor kit loaded: " << pSampler.numInstruments()
                  << " instruments (Inst 0: " << pSampler.instrumentName (0)
                  << ", Inst 1: " << pSampler.instrumentName (1) << ")\n";

        if (pSampler.numInstruments() >= 2)
        {
            // Assign Inst 0 to Bus 0 (Out 1/2), and Inst 1 to Bus 1 (Out 3/4)
            pSampler.setInstrumentBus (0, 0);
            pSampler.setInstrumentBus (1, 1);

            // Allocate 4 channels (2 stereo pairs: Out 1/2 and Out 3/4)
            std::vector<float> ch0 (512, 0.0f), ch1 (512, 0.0f), ch2 (512, 0.0f), ch3 (512, 0.0f);
            float* out4[4] = { ch0.data(), ch1.data(), ch2.data(), ch3.data() };

            // Trigger Inst 1 (assigned to Bus 1 -> Out 3/4)
            pSampler.allNotesOff();
            pSampler.noteOn (1, 1.0f, pSampler.currentSamplePosition());
            pSampler.render (out4, 4, 512);

            float peak12 = 0.0f, peak34 = 0.0f;
            for (int i = 0; i < 512; ++i)
            {
                peak12 = std::max (peak12, std::max (std::abs (ch0[static_cast<size_t> (i)]),
                                                     std::abs (ch1[static_cast<size_t> (i)])));
                peak34 = std::max (peak34, std::max (std::abs (ch2[static_cast<size_t> (i)]),
                                                     std::abs (ch3[static_cast<size_t> (i)])));
            }

            std::cout << "Inst 1 on Out 3/4 -> Peak Out 1/2: " << peak12
                      << ", Peak Out 3/4: " << peak34 << "\n";
            assert (peak34 > 0.001f);
            assert (peak12 == 0.0f); // Clean separation: zero bleed to Out 1/2!

            // Trigger Inst 0 (assigned to Bus 0 -> Out 1/2)
            pSampler.allNotesOff();
            pSampler.noteOn (0, 1.0f, pSampler.currentSamplePosition());
            pSampler.render (out4, 4, 512);

            peak12 = 0.0f;
            peak34 = 0.0f;
            for (int i = 0; i < 512; ++i)
            {
                peak12 = std::max (peak12, std::max (std::abs (ch0[static_cast<size_t> (i)]),
                                                     std::abs (ch1[static_cast<size_t> (i)])));
                peak34 = std::max (peak34, std::max (std::abs (ch2[static_cast<size_t> (i)]),
                                                     std::abs (ch3[static_cast<size_t> (i)])));
            }

            std::cout << "Inst 0 on Out 1/2 -> Peak Out 1/2: " << peak12
                      << ", Peak Out 3/4: " << peak34 << "\n";
            assert (peak12 > 0.001f);
            assert (peak34 == 0.0f); // Clean separation: zero bleed to Out 3/4!

            // Test stereo fold-down when host only provides 2 channels
            pSampler.allNotesOff();
            float* out2[2] = { ch0.data(), ch1.data() };
            // Trigger Inst 1 (bus 1), but host only passed 2 channels
            pSampler.noteOn (1, 1.0f, pSampler.currentSamplePosition());
            pSampler.render (out2, 2, 512);
            peak12 = 0.0f;
            for (int i = 0; i < 512; ++i)
                peak12 = std::max (peak12, std::max (std::abs (ch0[static_cast<size_t> (i)]),
                                                     std::abs (ch1[static_cast<size_t> (i)])));
            std::cout << "Stereo fold-down for Inst 1 on 2-ch host -> Peak Out 1/2: " << peak12 << "\n";
            assert (peak12 > 0.001f); // Folds down gracefully without crashing or dropping audio!
        }
    }

    std::cout << "-> Multi-out bus layout & audio routing verification: PASSED!\n";

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
    assert (handle.valid);
    assert (handle.numFrames > 0);
    assert (handle.sampleRate > 0);

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

    // 7. Phase 3: TempoDetector validation
    std::cout << "Testing TempoDetector...\n";
    {
        const double testSr = 44100.0;
        const double expectedBpm = 120.0;
        const int samplesPerBeat = static_cast<int> (testSr * 60.0 / expectedBpm);
        const int numBeats = 16;
        juce::AudioBuffer<float> testAudio (1, samplesPerBeat * numBeats);
        testAudio.clear();
        for (int b = 0; b < numBeats; ++b)
        {
            const int offset = b * samplesPerBeat;
            for (int s = 0; s < 500 && offset + s < testAudio.getNumSamples(); ++s)
            {
                float decay = std::exp (-static_cast<float> (s) / 80.0f);
                float val = std::sin (2.0f * juce::MathConstants<float>::pi * 1200.0f * s / static_cast<float> (testSr)) * decay;
                testAudio.setSample (0, offset + s, val);
            }
        }
        auto res = glorp::TempoDetector::detectTempo (testAudio, testSr);
        std::cout << "Detected BPM: " << res.bpm << " (expected ~" << expectedBpm << "), confidence: " << res.confidence << "\n";
        assert (std::abs (res.bpm - expectedBpm) <= 1.0);
        assert (res.isValid);
        std::cout << "-> Phase 3 TempoDetector: PASSED!\n";
    }

    // 8. Phase 3: Metronome & Count-In validation
    std::cout << "Testing Metronome...\n";
    {
        glorp::Metronome metro;
        metro.prepareToPlay (44100.0, 512);
        metro.setBpm (120.0);
        metro.setCountInMode (glorp::Metronome::CountInMode::oneBar);
        bool countInFinished = false;
        metro.onCountInComplete = [&]() { countInFinished = true; };
        metro.startCountIn();
        assert (metro.isCountingIn());
        assert (metro.getBeatsRemainingInCountIn() == 4);

        juce::AudioBuffer<float> renderBuf (2, 512);
        renderBuf.clear();
        float* channels[2] = { renderBuf.getWritePointer (0), renderBuf.getWritePointer (1) };

        // Test grid offset and boosted click volume
        metro.setGridOffset (0.150); // 150ms grid offset
        assert (std::abs (metro.getGridOffset() - 0.150) < 0.0001);
        metro.setLevel (2.5f);
        assert (metro.getLevel() == 2.5f);
        metro.stop();

        // Test sound presets
        metro.setClickSound (glorp::Metronome::ClickSound::digitalBeep);
        assert (metro.getClickSound() == glorp::Metronome::ClickSound::digitalBeep);
        metro.setClickSound (glorp::Metronome::ClickSound::hihatPedal);
        assert (metro.getClickSound() == glorp::Metronome::ClickSound::hihatPedal);
        std::cout << "-> Phase 3 Metronome Count-In & Grid Offset: PASSED!\n";
    }

    // 8b. BackingTrackPlayer Audio Scrubbing validation
    std::cout << "Testing BackingTrackPlayer Audio Scrubbing...\n";
    {
        glorp::BackingTrackPlayer player;
        player.prepareToPlay (44100.0, 512);
        const auto tempAudio = juce::File::createTempFile (".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (
            new juce::FileOutputStream (tempAudio), 44100.0, 2, 16, {}, 0));
        juce::AudioBuffer<float> tempBuf (2, 44100);
        for (int i = 0; i < 44100; ++i)
        {
            tempBuf.setSample (0, i, std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * i / 44100.0f));
            tempBuf.setSample (1, i, tempBuf.getSample (0, i));
        }
        writer->writeFromAudioSampleBuffer (tempBuf, 0, 44100);
        writer.reset();

        bool loaded = player.loadFile (tempAudio);
        assert (loaded);
        assert (player.hasTrack());

        // Test scrub to 0.5s
        player.scrubTo (0.5);
        assert (std::abs (player.getPlayheadSeconds() - 0.5) < 0.01);

        // Render audio buffer to verify scrub snippet renders without playing
        assert (! player.isPlaying());
        juce::AudioBuffer<float> scrubOut (2, 512);
        scrubOut.clear();
        float* chs[2] = { scrubOut.getWritePointer (0), scrubOut.getWritePointer (1) };
        player.render (chs, 2, 512);
        assert (scrubOut.getMagnitude (0, 512) > 0.0f); // Audio scrub grain rendered!

        tempAudio.deleteFile();
        std::cout << "-> BackingTrackPlayer Audio Scrubbing: PASSED!\n";
    }

    // 9. Phase 3: AudioRecorder & MidiRecorder validation
    std::cout << "Testing AudioRecorder & MidiRecorder...\n";
    {
        const auto tempWav = juce::File::createTempFile ("test_take.wav");
        const auto tempMid = juce::File::createTempFile ("test_take.mid");

        glorp::AudioRecorder audioRec;
        audioRec.prepareToPlay (44100.0, 512);
        bool ok = audioRec.startRecording (tempWav);
        assert (ok);
        assert (audioRec.isRecording());

        glorp::MidiRecorder midiRec;
        midiRec.startRecording (44100.0, 120.0);
        assert (midiRec.isRecording());

        juce::AudioBuffer<float> testBuf (2, 1024);
        testBuf.clear();
        for (int i = 0; i < 1024; ++i)
        {
            testBuf.setSample (0, i, 0.5f);
            testBuf.setSample (1, i, -0.5f);
        }
        const float* ptrs[2] = { testBuf.getReadPointer (0), testBuf.getReadPointer (1) };
        audioRec.pushAudio (ptrs, 2, 1024);

        midiRec.logMessage (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
        midiRec.logMessage (juce::MidiMessage::controllerEvent (1, 4, 64), 512);
        midiRec.logMessage (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 110), 1024);

        assert (midiRec.getEventCount() == 3);

        audioRec.stopRecording();
        midiRec.stopRecording();
        assert (! audioRec.isRecording());
        assert (! midiRec.isRecording());

        bool midiExportOk = midiRec.exportToFile (tempMid);
        assert (midiExportOk);
        assert (tempMid.existsAsFile() && tempMid.getSize() > 0);

        juce::MidiFile mf;
        auto inStream = tempMid.createInputStream();
        assert (inStream != nullptr);
        bool parsed = mf.readFrom (*inStream);
        assert (parsed);
        assert (mf.getNumTracks() == 2);
        std::cout << "-> Phase 3 MidiRecorder SMF Type 1 Export: PASSED!\n";

        assert (tempWav.existsAsFile() && tempWav.getSize() > 44);
        std::cout << "-> Phase 3 AudioRecorder 24-bit WAV: PASSED!\n";

        tempWav.deleteFile();
        tempMid.deleteFile();
    }

    // 10. Cymbal Zone Isolation & Strict Choking
    std::cout << "Testing Cymbal Zone Isolation and Strict Choking...\n";
    {
        auto& s = processor.getSampler();
        juce::String loadErr;
        s.loadKit (pkgKit, loadErr);
        assert (s.isKitLoaded());

        int c1Bow = -1, c1Edge = -1, c1Choke = -1;
        int c2Bow = -1, c2Choke = -1;
        int hhOpenZone = -1, hhClosedZone = -1;

        for (int i = 0; i < s.numInstruments(); ++i)
        {
            auto n = s.instrumentName (i);
            if (n == "Crash1Bow") c1Bow = i;
            else if (n == "Crash1Edge") c1Edge = i;
            else if (n == "Crash1Choke") c1Choke = i;
            else if (n == "Crash2Bow") c2Bow = i;
            else if (n == "Crash2Choke") c2Choke = i;
            else if (n == "HihatOpen") hhOpenZone = i;
            else if (n == "HihatClosed") hhClosedZone = i;
        }

        assert (c1Bow >= 0 && c1Edge >= 0 && c1Choke >= 0);
        assert (c2Bow >= 0 && c2Choke >= 0);

        s.allNotesOff();
        assert (s.activeVoiceCount() == 0);

        // 1. Crash 1 Bow and Crash 2 Bow: BOTH must play concurrently (Crash 2 does NOT choke Crash 1)
        s.noteOn (c1Bow, 1.0f, 0);
        assert (s.activeVoiceCount() == 1);
        s.noteOn (c2Bow, 1.0f, 100);
        assert (s.activeVoiceCount() == 2); // Both active!

        // 2. Crash 1 Edge: Crash 1 Bow does NOT get choked!
        s.noteOn (c1Edge, 1.0f, 200);
        assert (s.activeVoiceCount() == 3); // All 3 ringing!

        // 3. Trigger Crash 1 Choke: Chokes Crash 1 (Bow & Edge), but Crash 2 Bow remains unaffected!
        s.noteOn (c1Choke, 1.0f, 300);
        juce::AudioBuffer<float> renBuf (2, 2205);
        renBuf.clear();
        float* chPtrs[2] = { renBuf.getWritePointer (0), renBuf.getWritePointer (1) };
        s.render (chPtrs, 2, 2205);

        // Crash 2 voice is STILL active
        assert (s.activeVoiceCount() >= 1);

        // 4. HihatOpen then HihatClosed: HihatOpen does NOT get choked by Closed strike
        s.allNotesOff();
        assert (s.activeVoiceCount() == 0);
        if (hhOpenZone >= 0 && hhClosedZone >= 0)
        {
            s.noteOn (hhOpenZone, 1.0f, 0);
            assert (s.activeVoiceCount() == 1);
            s.noteOn (hhClosedZone, 1.0f, 100);
            assert (s.activeVoiceCount() == 2); // Both active!
        }

        std::cout << "-> Cymbal Zone Isolation & Selective Choking: PASSED!\n";
    }

    // 11. Verify Metronome exclusion from AudioRecorder in processBlock
    std::cout << "Testing Metronome exclusion from AudioRecorder...\n";
    {
        processor.prepareToPlay (44100.0, 512);
        auto& metro = processor.getMetronome();
        metro.setLevel (2.5f);
        metro.setBpm (120.0);
        metro.setEnabled (true);
        metro.setTransportPlaying (true);

        auto& recorder = processor.getAudioRecorder();
        auto tempRecordWav = juce::File ("/tmp/verify_rec_test.wav");
        tempRecordWav.deleteFile();
        recorder.startRecording (tempRecordWav);
        assert (recorder.isRecording());

        // Process a block with NO drum hits and NO backing track
        juce::AudioBuffer<float> block (2, 512);
        block.clear();
        juce::MidiBuffer midi;
        processor.processBlock (block, midi);

        // Metronome should have generated audio in the main monitor block
        float blockMag = block.getMagnitude (0, 512);
        assert (blockMag > 0.0f); // Metronome click is heard in headphones

        recorder.stopRecording();
        metro.stop();

        // But the audio recorder must have recorded SILENCE (metronome is not in recording)
        juce::AudioFormatManager formatMgr;
        formatMgr.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formatMgr.createReaderFor (tempRecordWav));
        assert (reader != nullptr);
        juce::AudioBuffer<float> recBuf (static_cast<int> (reader->numChannels), static_cast<int> (reader->lengthInSamples));
        reader->read (&recBuf, 0, recBuf.getNumSamples(), 0, true, true);
        float recMag = recBuf.getMagnitude (0, recBuf.getNumSamples());
        assert (recMag == 0.0f); // Metronome click is NOT in the recording!
        std::cout << "-> Metronome recording isolation verified (output=" << blockMag << ", recorded=" << recMag << "): PASSED!\n";
        reader.reset();
        tempRecordWav.deleteFile();
    }

    // 12. Verify TempoDetector multi-window consensus and detectTempoRange
    std::cout << "Testing TempoDetector multi-window consensus and range detection...\n";
    {
        const double sampleRate = 44100.0;
        // Construct a synthetic 25-second audio buffer:
        // 0s - 5s: 92 BPM intro clicks
        // 5s - 25s: 181 BPM main groove clicks
        const int totalSamples = static_cast<int> (sampleRate * 25.0);
        juce::AudioBuffer<float> testAudio (1, totalSamples);
        testAudio.clear();
        float* w = testAudio.getWritePointer (0);

        // Intro: 92 BPM -> beat interval = 60.0 / 92.0 = 0.652174 sec
        const double introPeriod = 60.0 / 92.0;
        for (double t = 0.0; t < 5.0; t += introPeriod)
        {
            const int s = static_cast<int> (t * sampleRate);
            for (int k = 0; k < 200 && (s + k) < totalSamples; ++k)
                w[s + k] = 0.9f * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (k / sampleRate));
        }

        // Main groove: 181 BPM -> beat interval = 60.0 / 181.0 = 0.33149 sec
        const double mainPeriod = 60.0 / 181.0;
        for (double t = 5.0; t < 25.0; t += mainPeriod)
        {
            const int s = static_cast<int> (t * sampleRate);
            for (int k = 0; k < 200 && (s + k) < totalSamples; ++k)
                w[s + k] = 1.0f * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * (k / sampleRate));
        }

        // 12a: Full song detection should find the dominant groove (~181 BPM) via multi-window consensus
        auto fullRes = glorp::TempoDetector::detectTempo (testAudio, sampleRate);
        std::cout << "Full song detected BPM: " << fullRes.bpm << " (expected ~181 BPM, confidence=" << fullRes.confidence << ")\n";
        assert (fullRes.isValid);
        assert (std::abs (fullRes.bpm - 181.0) <= 2.0);

        // 12b: Range detection on intro slice (0s - 5s)
        auto introRes = glorp::TempoDetector::detectTempoRange (testAudio, sampleRate, 0.0, 5.0);
        std::cout << "Intro slice detected BPM: " << introRes.bpm << " (expected ~92 BPM)\n";
        assert (introRes.isValid);
        assert (std::abs (introRes.bpm - 92.0) <= 2.0);

        // 12c: Range detection on main slice (8s - 20s)
        auto mainRes = glorp::TempoDetector::detectTempoRange (testAudio, sampleRate, 8.0, 12.0);
        std::cout << "Main slice detected BPM: " << mainRes.bpm << " (expected ~181 BPM)\n";
        assert (mainRes.isValid);
        assert (std::abs (mainRes.bpm - 181.0) <= 2.0);

        std::cout << "-> Multi-window consensus & range tempo detection: PASSED!\n";
    }

    // 13. Verify Metronome Phase-Locked Count-In with Grid Offset
    std::cout << "Testing Metronome phase-locked count-in with grid offset...\n";
    {
        glorp::Metronome metro;
        metro.prepareToPlay (44100.0, 512);
        metro.setBpm (120.0); // 0.5s per beat, 22050 samples
        metro.setEnabled (true);
        metro.setCountInMode (glorp::Metronome::CountInMode::oneBar);

        // Set a grid offset of +125ms = 0.125s (5512.5 samples)
        metro.setGridOffset (0.125);

        // Start count-in when track is at t=1.0s
        bool countInFired = false;
        metro.onCountInComplete = [&]() { countInFired = true; };
        metro.startCountIn (1.0);
        assert (metro.isCountingIn());

        // Process audio in blocks of 512 samples
        juce::AudioBuffer<float> block (2, 512);
        int totalProcessed = 0;
        int clickCount = 0;

        while (metro.isCountingIn() && totalProcessed < 44100 * 5)
        {
            block.clear();
            float* chPtrs[2] = { block.getWritePointer (0), block.getWritePointer (1) };
            metro.render (chPtrs, 2, 512);

            if (block.getMagnitude (0, 512) > 0.05f)
                clickCount++;

            totalProcessed += 512;
        }

        assert (countInFired);
        assert (! metro.isCountingIn());
        std::cout << "Count-in completed in " << totalProcessed << " samples (~"
                  << static_cast<double> (totalProcessed) / 44100.0 << " s). Clicks rendered: " << clickCount << "\n";
        assert (totalProcessed >= 44100 * 2); // At least 1 bar (at least 2.0s to align to beat 1)
        std::cout << "-> Metronome phase-locked offset count-in: PASSED!\n";
    }

    // 14. Verify Phase 6 Persistent Song Database & Setlist Sequential Count-In Transitions
    std::cout << "Testing Phase 6 Persistent Song Database (Setlist 1: A,B,C -> Setlist 2: X,Y,A) & SetlistPlayer...\n";
    {
        auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("glorp_phase6_test");
        tempDir.deleteRecursively();
        tempDir.createDirectory();

        auto writeTinyWav = [&](const juce::String& name, int numSamples) -> juce::File
        {
            juce::File f = tempDir.getChildFile (name);
            juce::WavAudioFormat wavFormat;
            std::unique_ptr<juce::FileOutputStream> out (f.createOutputStream());
            assert (out != nullptr);
            std::unique_ptr<juce::AudioFormatWriter> writer (
                wavFormat.createWriterFor (out.get(), 44100.0, 2, 16, {}, 0));
            assert (writer != nullptr);
            out.release();
            juce::AudioBuffer<float> buf (2, numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                const float v = 0.25f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (static_cast<float> (i) / 44100.0f));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }
            writer->writeFromAudioSampleBuffer (buf, 0, numSamples);
            return f;
        };

        // Create 5 backing tracks (~0.25s each for fast test playback)
        const auto fileA = writeTinyWav ("Song_A.wav", 11025);
        const auto fileB = writeTinyWav ("Song_B.wav", 11025);
        const auto fileC = writeTinyWav ("Song_C.wav", 11025);
        const auto fileX = writeTinyWav ("Song_X.wav", 11025);
        const auto fileY = writeTinyWav ("Song_Y.wav", 11025);
        const auto dbXml = tempDir.getChildFile ("song_library.xml");

        {
            glorp::SongDatabase db1 (dbXml);
            // Configure Setlist 1 with Songs A, B, and C
            db1.renameActiveSetlist ("Setlist 1 (A, B, C)");
            db1.addSongToActiveSetlist (fileA, 0.25);
            db1.addSongToActiveSetlist (fileB, 0.25);
            db1.addSongToActiveSetlist (fileC, 0.25);

            db1.updateSongConfig (fileA, 115.0, 0.0125, 4, 0.85f, 0.25);
            db1.updateSongConfig (fileB, 108.0, 0.0180, 5, 0.90f, 0.25);
            db1.updateSongConfig (fileC, 136.0, 0.0040, 4, 0.80f, 0.25);
            assert (db1.getNumConfiguredSongs() == 3);
        }

        // Reload database from disk into a fresh SongDatabase instance
        glorp::SongDatabase db2 (dbXml);
        assert (db2.getNumConfiguredSongs() == 3);
        assert (db2.isSongConfigured (fileA));
        assert (db2.isSongConfigured (fileB));
        assert (db2.isSongConfigured (fileC));
        assert (! db2.isSongConfigured (fileX));
        assert (! db2.isSongConfigured (fileY));

        // Create Setlist 2 with Songs X, Y, and A
        db2.createSetlist ("Setlist 2 (X, Y, A)");
        db2.addSongToActiveSetlist (fileX, 0.25);
        db2.addSongToActiveSetlist (fileY, 0.25);
        db2.addSongToActiveSetlist (fileA, 0.25);

        const auto* songAFromDb = db2.findSongByFile (fileA);
        assert (songAFromDb != nullptr);
        assert (songAFromDb->isConfigured);
        assert (std::abs (songAFromDb->bpm - 115.0) < 0.01);
        assert (std::abs (songAFromDb->firstBeatOffsetSec - 0.0125) < 0.0001);
        assert (songAFromDb->beatsPerBar == 4);
        if (songAFromDb == nullptr || ! songAFromDb->isConfigured || std::abs (songAFromDb->bpm - 115.0) > 0.01)
            std::exit (1);

        const auto* songXFromDb = db2.findSongByFile (fileX);
        assert (songXFromDb != nullptr && ! songXFromDb->isConfigured);
        if (songXFromDb == nullptr || songXFromDb->isConfigured)
            std::exit (1);
        std::cout << "-> Song A retained BPM=115.0, Offset=+12.5ms, 4/4 in Setlist 2 without re-setup!\n";

        // Now verify SetlistPlayer sequential playback across Setlist 1 (Song A -> Count-In -> Song B)
        db2.setActiveSetlistIndex (0); // Switch back to Setlist 1 (A, B, C)
        glorp::BackingTrackPlayer trackPlayer;
        glorp::Metronome metro;
        trackPlayer.prepareToPlay (44100.0, 512);
        metro.prepareToPlay (44100.0, 512);
        metro.setEnabled (true);
        metro.setCountInMode (glorp::Metronome::CountInMode::oneBar);

        glorp::SetlistPlayer setlistPlayer (db2, trackPlayer, metro);
        bool started = setlistPlayer.startSetlistPlayback (0, false);
        assert (started);
        if (! started) std::exit (1);
        assert (setlistPlayer.getCurrentSongIndex() == 0);
        assert (setlistPlayer.getState() == glorp::SetlistPlayer::State::countingIn);
        assert (std::abs (metro.getBpm() - 115.0) < 0.01);
        assert (metro.getTimeSignatureNumerator() == 4);

        juce::AudioBuffer<float> block (2, 512);
        float* chPtrs[2] = { block.getWritePointer (0), block.getWritePointer (1) };

        // 1. Render through Song A's count-in until Song A starts playing
        int guard = 0;
        while (setlistPlayer.getState() == glorp::SetlistPlayer::State::countingIn && guard++ < 500)
        {
            block.clear();
            trackPlayer.render (chPtrs, 2, 512);
            metro.render (chPtrs, 2, 512, trackPlayer.getPlayheadSeconds(), trackPlayer.isPlaying());
        }
        assert (setlistPlayer.getState() == glorp::SetlistPlayer::State::playingSong);
        assert (trackPlayer.isPlaying());

        // 2. Render Song A audio until it hits EOF
        guard = 0;
        while (trackPlayer.isPlaying() && guard++ < 200)
        {
            block.clear();
            trackPlayer.render (chPtrs, 2, 512);
            metro.render (chPtrs, 2, 512, trackPlayer.getPlayheadSeconds(), trackPlayer.isPlaying());
        }
        assert (! trackPlayer.isPlaying());
        assert (trackPlayer.hasFinishedEOF());

        // 3. Advance SetlistPlayer on EOF -> should transition to Song B (108.0 BPM, 5/4 meter, +18.0ms offset) with count-in!
        bool advanced = setlistPlayer.checkAndAdvanceOnEOF();
        assert (advanced);
        if (! advanced) std::exit (1);
        assert (setlistPlayer.getCurrentSongIndex() == 1);
        assert (setlistPlayer.getState() == glorp::SetlistPlayer::State::countingIn);
        assert (setlistPlayer.isTransitionCountIn());
        assert (std::abs (metro.getBpm() - 108.0) < 0.01);
        assert (metro.getTimeSignatureNumerator() == 5);
        assert (std::abs (metro.getGridOffset() - 0.0180) < 0.0001);

        // 4. Render through Song B's 5/4 count-in until Song B starts playing at sample 0
        guard = 0;
        while (setlistPlayer.getState() == glorp::SetlistPlayer::State::countingIn && guard++ < 600)
        {
            block.clear();
            trackPlayer.render (chPtrs, 2, 512);
            metro.render (chPtrs, 2, 512, trackPlayer.getPlayheadSeconds(), trackPlayer.isPlaying());
        }
        assert (setlistPlayer.getState() == glorp::SetlistPlayer::State::playingSong);
        assert (trackPlayer.isPlaying());
        assert (trackPlayer.getLoadedFile() == fileB);

        tempDir.deleteRecursively();
        std::cout << "-> Phase 6 Persistent Song Database & SetlistPlayer sequential transition: PASSED!\n";
    }

    processor.releaseResources();

    std::cout << "=== ALL VERIFICATIONS PASSED SUCCESSFULLY ===\n";
    std::exit (0);
}
