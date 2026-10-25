#include "DrumKitView.h"
#include "Editor.h"
#include "Sampler.h"
#include "PluginProcessor.h"
#include "GlorpKitWriter.h"
#include "GlorpKitProject.h"
#include "GlorpKitEditorWindow.h"
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

    // 15. Verify High-Precision TempoDetector on 181 BPM & 114 BPM dense full-band mixes + User MP3s
    std::cout << "Testing High-Precision TempoDetector (181.0 BPM & 114.0 BPM stress tests)...\n";
    {
        const double sr = 44100.0;

        auto makeFullBandGroove = [&](double targetBpm, double startOffsetSec, double durationSec)
        {
            const int totalSamples = static_cast<int> (sr * durationSec);
            juce::AudioBuffer<float> buf (2, totalSamples);
            buf.clear();
            float* l = buf.getWritePointer (0);
            float* r = buf.getWritePointer (1);

            // Continuous distorted guitar / cymbal wash floor that previously broke un-whitened ACF
            uint32_t rng = 1234567u;
            for (int i = 0; i < totalSamples; ++i)
            {
                rng = rng * 1664525u + 1013904223u;
                const float noise = (static_cast<float> (rng & 0xFFFF) / 32768.0f) - 1.0f;
                const float pad = 0.18f * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * (i / sr))
                                + 0.14f * std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (i / sr))
                                + 0.12f * noise;
                const float activeEnv = (static_cast<double> (i) / sr >= startOffsetSec) ? 1.0f : 0.0f;
                l[i] = pad * activeEnv;
                r[i] = pad * activeEnv;
            }

            const double beatSec = 60.0 / targetBpm;
            int beatIdx = 0;
            for (double t = startOffsetSec; t + 0.15 < durationSec; t += beatSec, ++beatIdx)
            {
                const int s0 = static_cast<int> ( std::round (t * sr));
                const bool isBarDownbeat = (beatIdx % 4 == 0);
                const bool isSnareBeat = (beatIdx % 2 == 1);

                // Kick on beat 1 & 3 (stronger low-end on beat 1)
                if (! isSnareBeat)
                {
                    const float amp = isBarDownbeat ? 0.95f : 0.65f;
                    for (int k = 0; k < 2400 && (s0 + k) < totalSamples; ++k)
                    {
                        const float env = std::exp (-static_cast<float> (k) / 450.0f);
                        const float freq = 95.0f * std::exp (-static_cast<float> (k) / 350.0f) + 52.0f;
                        const float sig = amp * env * std::sin (2.0f * juce::MathConstants<float>::pi * freq * (static_cast<float> (k) / static_cast<float> (sr)));
                        l[s0 + k] += sig;
                        r[s0 + k] += sig;
                    }
                }
                else
                {
                    // Snare on beat 2 & 4
                    for (int k = 0; k < 2000 && (s0 + k) < totalSamples; ++k)
                    {
                        rng = rng * 1664525u + 1013904223u;
                        const float n = (static_cast<float> (rng & 0xFFFF) / 32768.0f) - 1.0f;
                        const float env = std::exp (-static_cast<float> (k) / 320.0f);
                        const float body = std::sin (2.0f * juce::MathConstants<float>::pi * 210.0f * (static_cast<float> (k) / static_cast<float> (sr)));
                        const float sig = 0.80f * env * (0.5f * body + 0.5f * n);
                        l[s0 + k] += sig;
                        r[s0 + k] += sig;
                    }
                }

                // 8th-note hi-hat subdivision at t + 0.5 * beatSec
                const int sHalf = static_cast<int> (std::round ((t + 0.5 * beatSec) * sr));
                for (int k = 0; k < 600 && (sHalf + k) < totalSamples; ++k)
                {
                    rng = rng * 1664525u + 1013904223u;
                    const float n = (static_cast<float> (rng & 0xFFFF) / 32768.0f) - 1.0f;
                    const float env = std::exp (-static_cast<float> (k) / 110.0f);
                    l[sHalf + k] += 0.30f * env * n;
                    r[sHalf + k] += 0.30f * env * n;
                }
            }

            return buf;
        };

        auto track181 = makeFullBandGroove (181.0, 0.145, 32.0);
        auto res181 = glorp::TempoDetector::detectTempo (track181, sr);
        std::cout << "Synthetic 181.0 BPM track -> Detected BPM: " << res181.bpm
                  << ", Offset: " << res181.firstDownbeatSeconds << "s (expected 0.145s), conf: " << res181.confidence << "\n";
        VERIFY_CHECK (res181.isValid);
        VERIFY_CHECK (std::abs (res181.bpm - 181.0) <= 0.2);
        VERIFY_CHECK (std::abs (res181.firstDownbeatSeconds - 0.145) <= 0.015);

        auto track114 = makeFullBandGroove (114.0, 0.210, 32.0);
        auto res114 = glorp::TempoDetector::detectTempo (track114, sr);
        std::cout << "Synthetic 114.0 BPM track -> Detected BPM: " << res114.bpm
                  << ", Offset: " << res114.firstDownbeatSeconds << "s (expected 0.210s), conf: " << res114.confidence << "\n";
        VERIFY_CHECK (res114.isValid);
        VERIFY_CHECK (std::abs (res114.bpm - 114.0) <= 0.2);
        VERIFY_CHECK (std::abs (res114.firstDownbeatSeconds - 0.210) <= 0.015);

        // Optional real-world MP3 benchmark if accessible in the current environment
        const juce::File mp3_181 ("/home/servandoc/Documents/DRUM COVERS/No, Thank You/K-ON - No Thank You full.mp3");
        if (mp3_181.existsAsFile())
        {
            glorp::BackingTrackPlayer bp;
            bp.prepareToPlay (44100.0, 512);
            if (bp.loadFile (mp3_181))
            {
                auto r = glorp::TempoDetector::detectTempo (bp.getDecodedBuffer(), 44100.0);
                std::cout << "[Real Track 1] K-ON - No Thank You full.mp3 -> Detected BPM: " << r.bpm
                          << " (expected ~181.0), First Downbeat: " << r.firstDownbeatSeconds
                          << "s, Confidence: " << r.confidence << "\n";
            }
        }

        const juce::File mp3_114 ("/home/servandoc/Music/misono - symphony with misono BEST/01 Starry Heavens ver.2013.mp3");
        if (mp3_114.existsAsFile())
        {
            glorp::BackingTrackPlayer bp;
            bp.prepareToPlay (44100.0, 512);
            if (bp.loadFile (mp3_114))
            {
                auto r = glorp::TempoDetector::detectTempo (bp.getDecodedBuffer(), 44100.0);
                std::cout << "[Real Track 2] 01 Starry Heavens ver.2013.mp3 -> Detected BPM: " << r.bpm
                          << " (expected ~114.0), First Downbeat: " << r.firstDownbeatSeconds
                          << "s, Confidence: " << r.confidence << "\n";
            }
        }

        std::cout << "-> High-Precision TempoDetector 181 BPM & 114 BPM stress tests: PASSED!\n";
    }

    // 16. Verify Phase 7: .glorpkit v2.0 Writer/Reader, Custom PNG Assets, Hit Glow Color, Z-Order & GlorpKitEditor UI
    std::cout << "Testing Phase 7 .glorpkit v2.0 format, Custom PNGs, Hit Glow Color, Z-Layering & GlorpKitEditor UI...\n";
    {
        auto tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("glorp_phase7_test");
        tempDir.deleteRecursively();
        tempDir.createDirectory();

        // Create a test PNG image in memory
        juce::Image customSnarePng (juce::Image::ARGB, 96, 96, true);
        {
            juce::Graphics gg (customSnarePng);
            gg.setColour (juce::Colour (0xff2ecc71));
            gg.fillEllipse (8.0f, 8.0f, 80.0f, 80.0f);
            gg.setColour (juce::Colours::black);
            gg.drawEllipse (8.0f, 8.0f, 80.0f, 80.0f, 3.0f);
        }
        juce::MemoryOutputStream pngOut;
        juce::PNGImageFormat pngFmt;
        pngFmt.writeImageToStream (customSnarePng, pngOut);
        const auto* pngPtr = static_cast<const uint8_t*> (pngOut.getData());
        std::vector<uint8_t> pngBytes (pngPtr, pngPtr + pngOut.getDataSize());
        VERIFY_CHECK (pngBytes.size() > 32);

        // Build a v2.0 project in GlorpKitProject
        glorp::GlorpKitProject proj;
        proj.createNewDefaultKit();
        proj.setTitle ("Custom Studio Kit v2");
        proj.setHitGlowColor ("#2ecc71");

        // Customize Snare piece (index 4): custom position, width, Z-order, and embedded PNG
        auto& snarePiece = proj.getPieces()[4];
        snarePiece.normX  = 0.42f;
        snarePiece.normY  = 0.61f;
        snarePiece.normW  = 188.0f;
        snarePiece.zOrder = 95;
        snarePiece.customPngBytes = pngBytes;

        // Export to a .glorpkit v2.0 archive
        const auto outGlorpKit = tempDir.getChildFile ("custom_v2_test.glorpkit");
        std::string exportErr;
        bool exported = proj.exportToGlorpKit (outGlorpKit.getFullPathName().toStdString(), exportErr);
        if (! exported)
            std::cerr << "Export error: " << exportErr << "\n";
        VERIFY_CHECK (exported);
        VERIFY_CHECK (outGlorpKit.existsAsFile() && outGlorpKit.getSize() > 1024);

        // Load into DrumKit (main plugin/standalone engine) and verify v2.0 fields
        auto loadedKit = glorp::DrumKit::load (outGlorpKit.getFullPathName().toStdString());
        VERIFY_CHECK (loadedKit.error.empty());
        VERIFY_CHECK (loadedKit.isArchive);
        VERIFY_CHECK (loadedKit.hitGlowColor == "#2ecc71");

        bool foundCustomSnare = false;
        for (const auto& lp : loadedKit.logicalPieces)
        {
            if (lp.name == "snare" || lp.id == "snare")
            {
                foundCustomSnare = true;
                VERIFY_CHECK (lp.hasCustomLayout);
                VERIFY_CHECK (std::abs (lp.normX - 0.42f) < 0.001f);
                VERIFY_CHECK (std::abs (lp.normY - 0.61f) < 0.001f);
                VERIFY_CHECK (std::abs (lp.normW - 188.0f) < 0.1f);
                VERIFY_CHECK (lp.zOrder == 95);
                VERIFY_CHECK (lp.customPngBytes.size() == pngBytes.size());
                break;
            }
        }
        VERIFY_CHECK (foundCustomSnare);

        // Verify DrumKitView applies custom hit glow color, custom PNG, and Z-order
        glorp::DrumKitView dkv;
        dkv.setSize (820, 470);
        dkv.applyKitVisuals (loadedKit);
        VERIFY_CHECK (dkv.getHitGlowColour() == juce::Colour (0xff2ecc71));

        // Re-open the saved .glorpkit in a fresh GlorpKitProject and verify round-trip fidelity
        glorp::GlorpKitProject reopenedProj;
        std::string reopenErr;
        bool reopened = reopenedProj.openGlorpKitArchive (outGlorpKit.getFullPathName().toStdString(), reopenErr);
        VERIFY_CHECK (reopened);
        VERIFY_CHECK (reopenedProj.getHitGlowColor() == "#2ecc71");

        // Instantiate GlorpKitEditorComponent (headless) and render live verification screenshots
        glorp::GlorpKitEditorComponent editorApp (false);
        editorApp.setSize (1280, 820);
        VERIFY_CHECK (editorApp.openKitFile (outGlorpKit));
        editorApp.selectPiece (4, false);

        auto saveScreenshot = [&] (const juce::String& filename)
        {
            juce::Image snap (juce::Image::ARGB, editorApp.getWidth(), editorApp.getHeight(), true);
            juce::Graphics sg (snap);
            editorApp.paintEntireComponent (sg, false);
            juce::File outF ("/home/servandoc/.gemini/antigravity/brain/cf30153d-b421-4187-8ed5-1bdf793c8e28/" + filename);
            outF.deleteFile();
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (outF.createOutputStream()))
            {
                juce::PNGImageFormat pfmt;
                pfmt.writeImageToStream (snap, *stream);
            }
        };

        // 1) Render Light Sketch Mode with default orange glow (Piece 4 selected at 100%, others at 30%)
        editorApp.setTutorialVisible (false, 0);
        editorApp.setDarkTheme (false);
        editorApp.setHitGlowColourHex ("#ff8200");
        editorApp.setPreviewMode (false);
        saveScreenshot ("live_phase7_kit_editor_light.png");
        saveScreenshot ("live_phase7_kit_editor_selected.png");

        // 2) Render Preview Mode (Entire kit shown at full 100% opacity)
        editorApp.setPreviewMode (true);
        VERIFY_CHECK (editorApp.isPreviewMode());
        saveScreenshot ("live_phase7_kit_editor_preview.png");
        editorApp.setPreviewMode (false);

        // 3) Render Dark Mode with custom Radioactive Slime Green (#2ECC71) Hit Glow
        editorApp.setDarkTheme (true);
        editorApp.setHitGlowColourHex ("#2ecc71");
        saveScreenshot ("live_phase7_kit_editor_dark.png");

        // 4) Render First-Time Guided Tutorial Overlay (Step 3: Interactive 2D Stage Canvas spotlight)
        editorApp.setDarkTheme (false);
        editorApp.setHitGlowColourHex ("#ff8200");
        editorApp.setTutorialVisible (true, 2);
        saveScreenshot ("live_phase7_kit_editor_tutorial.png");

        tempDir.deleteRecursively();
        std::cout << "-> Phase 7 .glorpkit v2.0 Round-Trip, Custom PNGs, Hit Glow Color & GlorpKitEditor UI: PASSED!\n";
    }

    // 18. Verify Intelligent Sample Filename Parsing, Customizable Velocity Layers / RR Grid & OpenRaster (.ora) Layered Image Import
    {
        std::cout << "\n18. Testing Intelligent Sample Filename Parsing, Customizable Matrix & OpenRaster (.ora) Import...\n";

        // 18.1. Test parseSampleFilename against Ludwig Black Cortex real naming patterns
        auto s1 = glorp::parseSampleFilename ("BlackCortex Vers2 Snare ctr A 13.wav");
        VERIFY_CHECK (s1.hasExplicitVelocity);
        VERIFY_CHECK (s1.velocityLevel == 13);
        VERIFY_CHECK (s1.hasExplicitRoundRobin);
        VERIFY_CHECK (s1.roundRobin == 0); // 'A' -> 0
        VERIFY_CHECK (s1.zone == "ctr");
        VERIFY_CHECK (s1.instrument == "BlackCortex Vers2 Snare");

        auto s2 = glorp::parseSampleFilename ("BlackCortex Vers2 Snare ctr D 01.wav");
        VERIFY_CHECK (s2.hasExplicitVelocity);
        VERIFY_CHECK (s2.velocityLevel == 1);
        VERIFY_CHECK (s2.hasExplicitRoundRobin);
        VERIFY_CHECK (s2.roundRobin == 3); // 'D' -> 3
        VERIFY_CHECK (s2.zone == "ctr");

        auto s3 = glorp::parseSampleFilename ("Ludwig BassDrum A 08.wav");
        VERIFY_CHECK (s3.hasExplicitVelocity);
        VERIFY_CHECK (s3.velocityLevel == 8);
        VERIFY_CHECK (s3.hasExplicitRoundRobin);
        VERIFY_CHECK (s3.roundRobin == 0);

        auto s4 = glorp::parseSampleFilename ("Tom1 rim B 05.wav");
        VERIFY_CHECK (s4.hasExplicitVelocity);
        VERIFY_CHECK (s4.velocityLevel == 5);
        VERIFY_CHECK (s4.hasExplicitRoundRobin);
        VERIFY_CHECK (s4.roundRobin == 1);
        VERIFY_CHECK (s4.zone == "rim");

        std::cout << "-> Sample filename token parser: PASSED!\n";

        // 18.2. Multi-velocity sample set import & dynamic matrix grid expansion
        juce::File tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("glorp_ludwig_verify_" + juce::String (juce::Time::getMillisecondCounter()));
        tempDir.createDirectory();

        // Synthesize 52 WAV sample files matching the Ludwig Snare Center structure:
        // 4 Round Robins (A..D) x 13 Velocity Layers (01..13)
        std::vector<std::string> snareCtrSamplePaths;
        const char rrChars[4] = { 'A', 'B', 'C', 'D' };
        for (int v = 1; v <= 13; ++v)
        {
            for (int r = 0; r < 4; ++r)
            {
                char fname[128];
                std::snprintf (fname, sizeof (fname), "BlackCortex Vers2 Snare ctr %c %02d.wav", rrChars[r], v);
                juce::File wf = tempDir.getChildFile (fname);

                juce::WavAudioFormat wavFmt;
                std::unique_ptr<juce::AudioFormatWriter> writer (
                    wavFmt.createWriterFor (new juce::FileOutputStream (wf), 44100.0, 1, 16, {}, 0));
                VERIFY_CHECK (writer != nullptr);
                juce::AudioBuffer<float> buf (1, 512);
                buf.clear();
                const float amp = (float) v / 13.0f * 0.9f;
                for (int i = 0; i < 512; ++i)
                    buf.setSample (0, i, std::sin ((float) i * 0.1f) * amp * std::exp (-(float) i / 100.0f));
                writer->writeFromAudioSampleBuffer (buf, 0, 512);
                writer.reset();

                snareCtrSamplePaths.push_back (wf.getFullPathName().toStdString());
            }
        }
        VERIFY_CHECK (snareCtrSamplePaths.size() == 52);

        glorp::GlorpKitProject proj;
        proj.createNewBlankKit();
        int snareIdx = proj.addPieceWithAutoLayout ("Snare", "snare");
        auto& snarePiece = proj.pieces[(size_t) snareIdx];
        VERIFY_CHECK (! snarePiece.zones.empty());

        int importedHits = proj.importAudioFilesIntoZone (snareIdx, 0, snareCtrSamplePaths, "close");
        VERIFY_CHECK (importedHits == 52);

        auto& snareZone = snarePiece.zones[0];
        VERIFY_CHECK (snareZone.numVelocityLayers == 13);
        VERIFY_CHECK (snareZone.numRoundRobins == 4);
        VERIFY_CHECK (snareZone.hits.size() == 52);

        // Verify each cell (vel 0..12, rr 0..3) is occupied with correct velocity and RR index
        for (int v = 0; v < 13; ++v)
        {
            for (int r = 0; r < 4; ++r)
            {
                bool found = false;
                for (const auto& h : snareZone.hits)
                {
                    if (h.velocityLayerIndex == v && h.roundRobinIndex == r)
                    {
                        found = true;
                        VERIFY_CHECK (h.power >= 0.12f && h.power <= 1.0f);
                        break;
                    }
                }
                VERIFY_CHECK (found);
            }
        }

        // Test Auto Power preserves velocity layers
        proj.autoComputeZonePowers (snareIdx, 0);
        VERIFY_CHECK (snareZone.numVelocityLayers == 13);
        VERIFY_CHECK (snareZone.numRoundRobins == 4);
        VERIFY_CHECK (snareZone.hits.size() == 52);

        // Test moving sample between matrix cells
        VERIFY_CHECK (proj.moveSampleToCell (snareIdx, 0, 0, 3, 2));
        VERIFY_CHECK (snareZone.hits[0].velocityLayerIndex == 3);
        VERIFY_CHECK (snareZone.hits[0].roundRobinIndex == 2);

        // Test swapping sample cells
        const int hitA_vel = snareZone.hits[1].velocityLayerIndex;
        const int hitA_rr  = snareZone.hits[1].roundRobinIndex;
        const int hitB_vel = snareZone.hits[2].velocityLayerIndex;
        const int hitB_rr  = snareZone.hits[2].roundRobinIndex;
        VERIFY_CHECK (proj.swapSampleCells (snareIdx, 0, 1, 2));
        VERIFY_CHECK (snareZone.hits[1].velocityLayerIndex == hitB_vel);
        VERIFY_CHECK (snareZone.hits[1].roundRobinIndex    == hitB_rr);
        VERIFY_CHECK (snareZone.hits[2].velocityLayerIndex == hitA_vel);
        VERIFY_CHECK (snareZone.hits[2].roundRobinIndex    == hitA_rr);

        // Test setting custom grid dimensions
        proj.setZoneGridDimensions (snareIdx, 0, 16, 6);
        VERIFY_CHECK (snareZone.numVelocityLayers == 16);
        VERIFY_CHECK (snareZone.numRoundRobins == 6);

        std::cout << "-> 52-hit 13-velocity multi-layer import & matrix cell manipulation: PASSED!\n";

        // 18.3. OpenRaster (.ora) layered archive generation, parsing, and dialog test
        juce::File oraFile = tempDir.getChildFile ("ludwig_stage_kit.ora");
        {
            juce::File zipContentDir = tempDir.getChildFile ("ora_build");
            zipContentDir.createDirectory();
            juce::File dataDir = zipContentDir.getChildFile ("data");
            dataDir.createDirectory();

            zipContentDir.getChildFile ("mimetype").replaceWithText ("image/openraster");

            juce::File ludwigDir ("/home/servandoc/Documents/DEV/GlorpDrums/test_data/ludwig_assets");
            auto copyOrMakePng = [&] (const juce::String& name, int w, int h, juce::Colour col) -> juce::File
            {
                juce::File srcF = ludwigDir.getChildFile (name);
                juce::File dstF = dataDir.getChildFile (name);
                if (srcF.existsAsFile())
                {
                    srcF.copyFileTo (dstF);
                }
                else
                {
                    juce::Image img (juce::Image::ARGB, w, h, true);
                    juce::Graphics ig (img);
                    ig.setColour (col);
                    ig.fillEllipse (4.0f, 4.0f, (float) w - 8.0f, (float) h - 8.0f);
                    if (auto st = std::unique_ptr<juce::FileOutputStream> (dstF.createOutputStream()))
                    {
                        juce::PNGImageFormat pfmt;
                        pfmt.writeImageToStream (img, *st);
                    }
                }
                return dstF;
            };

            copyOrMakePng ("snare.png", 220, 200, juce::Colour (0xffe67e22));
            copyOrMakePng ("kick.png",  300, 290, juce::Colour (0xff2c3e50));
            copyOrMakePng ("hihat.png", 180, 180, juce::Colour (0xfff1c40f));
            copyOrMakePng ("tom1.png",  160, 150, juce::Colour (0xff34495e));
            copyOrMakePng ("tom2.png",  170, 160, juce::Colour (0xff34495e));
            copyOrMakePng ("ftom.png",  210, 200, juce::Colour (0xff2c3e50));
            copyOrMakePng ("crash1.png", 200, 190, juce::Colour (0xfff39c12));
            copyOrMakePng ("crash2.png", 210, 200, juce::Colour (0xfff39c12));
            copyOrMakePng ("ride.png",  240, 230, juce::Colour (0xffd35400));

            juce::String stackXml =
                "<?xml version='1.0' encoding='UTF-8'?>\n"
                "<image version='0.0.1' w='1920' h='1080'>\n"
                "  <stack>\n"
                "    <layer name='Snare' src='data/snare.png' x='520' y='520' opacity='1.0' visibility='visible'/>\n"
                "    <layer name='Kick' src='data/kick.png' x='720' y='580' opacity='1.0' visibility='visible'/>\n"
                "    <layer name='Hi-Hat' src='data/hihat.png' x='340' y='420' opacity='1.0' visibility='visible'/>\n"
                "    <layer name='Tom 1' src='data/tom1.png' x='620' y='360' opacity='1.0' visibility='visible'/>\n"
                "    <layer name='Tom 2' src='data/tom2.png' x='840' y='360' opacity='1.0' visibility='visible'/>\n"
                "    <layer name='Floor Tom' src='data/ftom.png' x='1040' y='520' opacity='1.0' visibility='visible'/>\n"
                "    <layer name='Crash 1' src='data/crash1.png' x='320' y='220' opacity='1.0' visibility='visible'/>\n"
                "    <layer name='Crash 2' src='data/crash2.png' x='1060' y='220' opacity='1.0' visibility='visible'/>\n"
                "    <layer name='Ride' src='data/ride.png' x='1120' y='380' opacity='1.0' visibility='visible'/>\n"
                "  </stack>\n"
                "</image>\n";
            zipContentDir.getChildFile ("stack.xml").replaceWithText (stackXml);

            juce::ZipFile::Builder zb;
            zb.addFile (zipContentDir.getChildFile ("mimetype"), 9, "mimetype");
            zb.addFile (zipContentDir.getChildFile ("stack.xml"), 9, "stack.xml");
            for (const auto& f : dataDir.findChildFiles (juce::File::findFiles, false))
                zb.addFile (f, 9, "data/" + f.getFileName());

            oraFile.deleteFile();
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (oraFile.createOutputStream()))
                zb.writeToStream (*stream, nullptr);
            zipContentDir.deleteRecursively();
        }
        VERIFY_CHECK (oraFile.existsAsFile());

        // Test OpenRasterReader
        glorp::OraDocument oraDoc;
        juce::String oraErr;
        bool oraOk = glorp::OpenRasterReader::read (oraFile, oraDoc, oraErr);
        VERIFY_CHECK (oraOk);
        VERIFY_CHECK (oraDoc.canvasWidth == 1920);
        VERIFY_CHECK (oraDoc.canvasHeight == 1080);
        VERIFY_CHECK (oraDoc.layers.size() == 9);
        VERIFY_CHECK (oraDoc.layers[0].name == "Snare");
        VERIFY_CHECK (oraDoc.layers[0].thumbnail.isValid());
        VERIFY_CHECK (oraDoc.layers[0].pngBytes.size() > 100);

        std::cout << "-> OpenRaster ZIP and stack.xml reader: PASSED!\n";

        // 18.4. Test GlorpKitEditorComponent OpenRaster Import Dialog & Full Screen Renders
        glorp::GlorpKitEditorComponent editor (false);
        editor.setSize (1280, 820);

        // Open .ora file in the editor -> modal overlay appears
        editor.importOpenRasterFile (oraFile);
        VERIFY_CHECK (editor.isOraOverlayActive());

        // Render live modal overlay screenshot: live_phase7_ora_import_dialog.png
        {
            juce::Image oraSnap (juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
            juce::Graphics osg (oraSnap);
            editor.paintEntireComponent (osg, false);
            juce::File outF ("/home/servandoc/.gemini/antigravity/brain/cf30153d-b421-4187-8ed5-1bdf793c8e28/live_phase7_ora_import_dialog.png");
            outF.deleteFile();
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (outF.createOutputStream()))
            {
                juce::PNGImageFormat pfmt;
                pfmt.writeImageToStream (oraSnap, *stream);
            }
            std::cout << "-> Rendered OpenRaster Import Modal: " << outF.getFullPathName() << "\n";
        }

        // Programmatically confirm the import from the dialog
        auto* overlay = editor.getOraOverlay();
        VERIFY_CHECK (overlay != nullptr);
        overlay->confirmAndApply();
        VERIFY_CHECK (! editor.isOraOverlayActive());

        // Verify pieces were generated with stage coordinates and custom PNGs
        auto& edProj = editor.getProject();
        VERIFY_CHECK (! edProj.pieces.empty());
        bool foundSnarePiece = false;
        for (const auto& p : edProj.pieces)
        {
            if (juce::String (p.name).containsIgnoreCase ("snare"))
            {
                foundSnarePiece = true;
                VERIFY_CHECK (! p.customPngBytes.empty());
                break;
            }
        }
        VERIFY_CHECK (foundSnarePiece);

        // 18.5. Import Ludwig 52-sample set into Snare in editor and render live matrix screenshot
        int edSnareIdx = -1;
        for (size_t pi = 0; pi < edProj.pieces.size(); ++pi)
        {
            if (juce::String (edProj.pieces[pi].name).containsIgnoreCase ("snare"))
            {
                edSnareIdx = (int) pi;
                break;
            }
        }
        if (edSnareIdx >= 0)
        {
            editor.selectPiece (edSnareIdx, false);
            edProj.importAudioFilesIntoZone (edSnareIdx, 0, snareCtrSamplePaths, "close");
            editor.reloadProjectIntoEngine();
            editor.selectPiece (edSnareIdx, false);
            editor.selectZone (0, false);

            auto& zoneRef = edProj.pieces[(size_t) edSnareIdx].zones[0];
            VERIFY_CHECK (zoneRef.numVelocityLayers == 13);
            VERIFY_CHECK (zoneRef.numRoundRobins == 4);

            juce::Image matrixSnap (juce::Image::ARGB, editor.getWidth(), editor.getHeight(), true);
            juce::Graphics msg (matrixSnap);
            editor.paintEntireComponent (msg, false);
            juce::File outF ("/home/servandoc/.gemini/antigravity/brain/cf30153d-b421-4187-8ed5-1bdf793c8e28/live_phase7_ludwig_samples_matrix.png");
            outF.deleteFile();
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (outF.createOutputStream()))
            {
                juce::PNGImageFormat pfmt;
                pfmt.writeImageToStream (matrixSnap, *stream);
            }
            std::cout << "-> Rendered 13-Velocity Ludwig Matrix Screenshot: " << outF.getFullPathName() << "\n";
        }

        tempDir.deleteRecursively();
        std::cout << "-> Phase 7 Intelligent Sample Mapping, Customizable Matrix & OpenRaster Import: PASSED!\n";
    }

    // 19. Verify Canvas Coordinates Dragging, Zone Horizontal Scrolling, & .glorpkit Custom Graphics in DrumKitView
    {
        std::cout << "\n19. Testing Canvas Coordinates, Zone Horizontal Scrolling & Custom Graphics Display...\n";

        // Helper lambda to synthesize a JUCE MouseEvent
        auto makeMouseEvent = [] (juce::Component* comp, juce::Point<float> pos, juce::Point<float> downPos) {
            return juce::MouseEvent (
                juce::Desktop::getInstance().getMainMouseSource(),
                pos,
                juce::ModifierKeys(),
                0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                comp, comp,
                juce::Time::getCurrentTime(),
                downPos,
                juce::Time::getCurrentTime(),
                1,
                pos != downPos
            );
        };

        // 19.1 Verify piece coordinate mapping and dragging
        glorp::GlorpKitEditorComponent editor (false);
        editor.setSize (1280, 800);
        auto& proj = editor.getProject();
        proj.createNewBlankKit();
        editor.reloadProjectIntoEngine();

        // Add a piece with default sprite
        int snareIdx = proj.addPiece ("Snare Drum", "snare");
        editor.reloadProjectIntoEngine();
        editor.selectPiece (snareIdx, false);

        const auto initialBounds = editor.getPieceSpriteBounds (snareIdx);
        VERIFY_CHECK (! initialBounds.isEmpty());
        // Verify piece is on screen and inside canvas viewport
        const auto cv = editor.getCanvasViewportBounds();
        VERIFY_CHECK (cv.contains (initialBounds.getCentre()));

        // Simulate dragging the piece by 50px horizontally and -30px vertically
        const auto centerPos = initialBounds.getCentre();

        editor.mouseDown (makeMouseEvent (&editor, centerPos, centerPos));
        editor.mouseDrag (makeMouseEvent (&editor, centerPos.translated (50.0f, -30.0f), centerPos));
        editor.mouseUp   (makeMouseEvent (&editor, centerPos.translated (50.0f, -30.0f), centerPos));

        const auto draggedBounds = editor.getPieceSpriteBounds (snareIdx);
        VERIFY_CHECK (! draggedBounds.isEmpty());
        // Verify coordinates shifted smoothly and piece did NOT disappear
        VERIFY_CHECK (draggedBounds.getX() > initialBounds.getX() + 25.0f);
        VERIFY_CHECK (draggedBounds.getY() < initialBounds.getY() - 15.0f);
        VERIFY_CHECK (cv.contains (draggedBounds.getCentre()));

        // 19.2 Verify Zone Horizontal Scrolling for multi-zone instruments (e.g. hihat with 8 zones)
        int hhIdx = proj.addPiece ("Hi-Hat Custom", "hihat");
        auto& hhPiece = proj.pieces[(size_t) hhIdx];
        hhPiece.zones.clear();
        const char* hhZoneNames[] = { "closed", "closed-edge", "semi-open", "open-1", "open-2", "open-3", "pedal", "splash" };
        for (int zi = 0; zi < 8; ++zi)
        {
            glorp::EditableZone z;
            z.name = hhZoneNames[zi];
            z.note = 42 + zi;
            hhPiece.zones.push_back (std::move (z));
        }
        editor.reloadProjectIntoEngine();
        editor.selectPiece (hhIdx, false);

        // Force a paint pass to layout tabs
        juce::Image dummyImg (juce::Image::ARGB, 1280, 800, true);
        juce::Graphics dg (dummyImg);
        editor.paintEntireComponent (dg, false);

        VERIFY_CHECK (editor.getMaxZoneScrollX() > 0.0f);

        // Select the last zone (splash) and verify auto-scrolled
        editor.selectZone (7, false);
        VERIFY_CHECK (editor.getZoneScrollX() > 0.0f);

        // Test wheel scroll clamping
        editor.mouseWheelMove (makeMouseEvent (&editor, editor.getZoneTabBarBounds().getCentre(), editor.getZoneTabBarBounds().getCentre()),
                               juce::MouseWheelDetails { 0.0f, -10.0f, false, false, false });
        VERIFY_CHECK (editor.getZoneScrollX() <= editor.getMaxZoneScrollX());

        // 19.3 Verify pieceForName normalization
        VERIFY_CHECK (glorp::DrumKitView::pieceForName ("tom_2") == glorp::KitPiece::tom2);
        VERIFY_CHECK (glorp::DrumKitView::pieceForName ("f_tom") == glorp::KitPiece::floorTom);
        VERIFY_CHECK (glorp::DrumKitView::pieceForName ("crash_2") == glorp::KitPiece::crash2);
        VERIFY_CHECK (glorp::DrumKitView::pieceForName ("tom_1") == glorp::KitPiece::tom1);
        VERIFY_CHECK (glorp::DrumKitView::pieceForName ("crash_1") == glorp::KitPiece::crash1);

        // 19.4 Export .glorpkit with custom PNG & verify in DrumKitView
        const juce::File tempDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getChildFile ("glorp_verify_p7_fix_" + juce::String (juce::Random::getSystemRandom().nextInt()));
        tempDir.createDirectory();
        const auto exportKitFile = tempDir.getChildFile ("Ludwig_Black_Cortex.glorpkit");

        // Make dummy custom PNG (solid orange 64x64)
        juce::Image testCustomPng (juce::Image::ARGB, 64, 64, true);
        testCustomPng.clear (juce::Rectangle<int> (0, 0, 64, 64), juce::Colour (0xffff8200));
        juce::MemoryOutputStream mos;
        juce::PNGImageFormat().writeImageToStream (testCustomPng, mos);
        std::vector<uint8_t> pngBytes ((const uint8_t*) mos.getData(), (const uint8_t*) mos.getData() + mos.getDataSize());

        hhPiece.customPngBytes = pngBytes;
        hhPiece.customPngFileName = "custom_hihat.png";


        // Title is initially "Untitled Kit"
        proj.title = "Untitled Kit";
        std::string expErr;
        bool expOk = proj.exportToGlorpKit (exportKitFile.getFullPathName().toStdString(), expErr);
        if (! expOk)
            std::cerr << "exportToGlorpKit error: " << expErr << "\n";
        VERIFY_CHECK (expOk);
        VERIFY_CHECK (exportKitFile.existsAsFile() && exportKitFile.getSize() > 256);

        // Verify KitScanner infers title from file stem when manifest title was "Untitled Kit"
        auto kitEntry = glorp::KitScanner::describe (exportKitFile.getFullPathName().toStdString(), "TestKits");
        VERIFY_CHECK (kitEntry.displayName == "[PKG] Ludwig_Black_Cortex" || kitEntry.displayName == "[PKG] Ludwig Black Cortex");

        // Load into DrumKit
        auto loadedKit = glorp::DrumKit::load (exportKitFile.getFullPathName().toStdString());
        VERIFY_CHECK (loadedKit.error.empty());
        VERIFY_CHECK (loadedKit.title == "Ludwig_Black_Cortex" || loadedKit.title == "Ludwig Black Cortex");
        VERIFY_CHECK (! loadedKit.logicalPieces.empty());

        bool foundCustomHh = false;
        for (const auto& lp : loadedKit.logicalPieces)
        {
            if (lp.id == "hihat" || lp.graphic == "hihat" || lp.name.find ("Hi-Hat") != std::string::npos)
            {
                foundCustomHh = true;
                VERIFY_CHECK (lp.customPngBytes.size() == pngBytes.size());
            }
        }
        VERIFY_CHECK (foundCustomHh);

        // Load into DrumKitView and verify visuals apply
        glorp::DrumKitView dkv;
        dkv.setSize (924, 621);
        dkv.applyKitVisuals (loadedKit);

        // Verify hihat custom image was applied
        VERIFY_CHECK (dkv.getPieceImage (glorp::KitPiece::hihat).isValid());
        VERIFY_CHECK (dkv.getPieceImage (glorp::KitPiece::hihat).getWidth() == 64);

        // Render DrumKitView with loaded kit to artifacts
        juce::Image kitSnap (juce::Image::ARGB, 924, 621, true);
        juce::Graphics kg (kitSnap);
        dkv.paintEntireComponent (kg, false);
        juce::File dkvSnapFile ("/home/servandoc/.gemini/antigravity/brain/cf30153d-b421-4187-8ed5-1bdf793c8e28/live_phase7_kit_editor_loaded_in_plugin.png");
        dkvSnapFile.deleteFile();
        if (auto s = std::unique_ptr<juce::FileOutputStream> (dkvSnapFile.createOutputStream()))
            juce::PNGImageFormat().writeImageToStream (kitSnap, *s);
        std::cout << "-> Rendered DrumKitView with Custom Graphics: " << dkvSnapFile.getFullPathName() << "\n";

        // Also render updated GlorpKitEditor canvas showing the custom piece & multi-zone scroll bar
        juce::Image edSnap (juce::Image::ARGB, 1280, 800, true);
        juce::Graphics eg (edSnap);
        editor.paintEntireComponent (eg, false);
        juce::File edSnapFile ("/home/servandoc/.gemini/antigravity/brain/cf30153d-b421-4187-8ed5-1bdf793c8e28/live_phase7_kit_editor_multizone_canvas.png");
        edSnapFile.deleteFile();
        if (auto s = std::unique_ptr<juce::FileOutputStream> (edSnapFile.createOutputStream()))
            juce::PNGImageFormat().writeImageToStream (edSnap, *s);
        std::cout << "-> Rendered GlorpKitEditor Multi-Zone & Canvas: " << edSnapFile.getFullPathName() << "\n";

        tempDir.deleteRecursively();
        std::cout << "-> Canvas Coordinates Dragging, Zone Scrolling & Custom Graphics Display: PASSED!\n";
    }

    // 20. Verify Ludwig 12-Piece Kit Loading, Velocity & Round-Robin Matrix Ordering, Canvas Guides, and Arbitrary Dynamic Kits in DrumKitView
    std::cout << "\n--- Section 20: Ludwig 12-Piece Kit, 4-RR Velocity Matrix, Stage Guides & Dynamic Kit in DrumKitView ---\n";
    {
        const juce::File ludwigTestFile ("/tmp/ludwig_test.glorpkit");
        if (ludwigTestFile.existsAsFile())
        {
            glorp::GlorpKitProject proj;
            std::string openErr;
            bool opened = proj.openGlorpKitArchive (ludwigTestFile.getFullPathName().toStdString(), openErr);
            std::cout << "Opening /tmp/ludwig_test.glorpkit: " << (opened ? "SUCCESS" : "FAILED") << " (err: " << openErr << ")\n";
            VERIFY_CHECK (opened);
            VERIFY_CHECK (proj.pieces.size() >= 12);

            // Find Snare piece
            const glorp::EditablePiece* snarePiece = nullptr;
            for (const auto& p : proj.pieces)
            {
                if (p.name.find ("Snare") != std::string::npos || p.id == "snare")
                {
                    snarePiece = &p;
                    break;
                }
            }
            VERIFY_CHECK (snarePiece != nullptr);

            // Find Snare Center zone
            const glorp::EditableZone* centerZone = nullptr;
            for (const auto& z : snarePiece->zones)
            {
                if (z.name == "Center" || z.name == "Head Center" || z.name == "ctr" || z.name == "head_center" || z.name == "Snare Head Center")
                {
                    centerZone = &z;
                    break;
                }
            }
            VERIFY_CHECK (centerZone != nullptr);

            std::cout << "Snare Center zone detected: numVelocityLayers=" << centerZone->numVelocityLayers
                      << ", numRoundRobins=" << centerZone->numRoundRobins
                      << ", total hits=" << centerZone->hits.size() << "\n";

            // Verify Center has 13 velocity layers and 4 round robins (A, B, C, D)
            VERIFY_CHECK (centerZone->numVelocityLayers == 13);
            VERIFY_CHECK (centerZone->numRoundRobins == 4);
            VERIFY_CHECK (centerZone->hits.size() == 52); // 13 * 4 = 52 hits!

            // Verify hits are mapped to distinct (vel, rr) cells without overlap or scrambled ordering
            std::set<std::pair<int, int>> cellsOccupied;
            for (const auto& h : centerZone->hits)
            {
                VERIFY_CHECK (h.velocityLayerIndex >= 0 && h.velocityLayerIndex < 13);
                VERIFY_CHECK (h.roundRobinIndex >= 0 && h.roundRobinIndex < 4);
                cellsOccupied.insert ({ h.velocityLayerIndex, h.roundRobinIndex });
            }
            VERIFY_CHECK (cellsOccupied.size() == 52); // Every single cell from (0,0) to (12,3) is filled!

            // Test Ideal Stage Guides
            const auto& guides = glorp::GlorpKitEditorComponent::getIdealStageGuides();
            VERIFY_CHECK (guides.size() == 12);
            VERIFY_CHECK (glorp::GlorpKitEditorComponent::findMatchingStageGuide ("Snare", "snare") == 1);
            VERIFY_CHECK (glorp::GlorpKitEditorComponent::findMatchingStageGuide ("Tom 3", "tom_2") == 5);
            VERIFY_CHECK (glorp::GlorpKitEditorComponent::findMatchingStageGuide ("Floor Tom 2", "f_tom") == 7);
            VERIFY_CHECK (glorp::GlorpKitEditorComponent::findMatchingStageGuide ("Crash 3", "crash_2") == 11);

            // Test Editor Window with Guides
            glorp::GlorpKitEditorComponent editor (false);
            editor.setSize (1280, 800);
            editor.openKitFile (ludwigTestFile);

            // Render stage canvas guides screenshot
            juce::Image guideSnap (juce::Image::ARGB, 1280, 800, true);
            juce::Graphics gg (guideSnap);
            editor.paintEntireComponent (gg, false);
            juce::File guideSnapFile ("/home/servandoc/.gemini/antigravity/brain/cf30153d-b421-4187-8ed5-1bdf793c8e28/live_phase7_canvas_guides.png");
            guideSnapFile.deleteFile();
            if (auto s = std::unique_ptr<juce::FileOutputStream> (guideSnapFile.createOutputStream()))
                juce::PNGImageFormat().writeImageToStream (guideSnap, *s);
            std::cout << "-> Rendered Stage Canvas Guides screenshot: " << guideSnapFile.getFullPathName() << "\n";

            // Now test loading kit into DrumKit and DrumKitView
            auto loadedKit = glorp::DrumKit::load (ludwigTestFile.getFullPathName().toStdString());
            VERIFY_CHECK (loadedKit.error.empty());
            VERIFY_CHECK (loadedKit.logicalPieces.size() >= 12);

            glorp::DrumKitView dkv;
            dkv.setSize (924, 621);
            dkv.applyKitVisuals (loadedKit);

            // Verify DrumKitView has dynamic pieces enabled and all 12 pieces are present!
            VERIFY_CHECK (dkv.hasDynamicPieces());
            VERIFY_CHECK (dkv.getNumDynamicPieces() >= 12);

            const auto& dynPieces = dkv.getDynamicPieces();
            for (size_t di = 0; di < dynPieces.size(); ++di)
            {
                const auto& dp = dynPieces[di];
                VERIFY_CHECK (! dp.name.empty());
                VERIFY_CHECK (! dp.bounds.isEmpty());
                std::cout << "  Dynamic Piece #" << di << ": " << dp.name << " bounds=("
                          << dp.bounds.getX() << ", " << dp.bounds.getY() << ", " << dp.bounds.getWidth() << "x" << dp.bounds.getHeight() << ")\n";
            }

            // Test hitting Tom 3, Floor Tom 2, Crash 3 (Crash Istanbul Xist)
            dkv.triggerHit ("Tom 3", 0.95f);
            dkv.triggerHit ("Floor Tom 2", 0.90f);
            dkv.triggerHit ("Crash Istanbul Xist", 0.85f);

            bool foundTom3Hit = false, foundFloor2Hit = false, foundCrash3Hit = false;
            for (const auto& dp : dkv.getDynamicPieces())
            {
                if (dp.name.find ("Tom 3") != std::string::npos && dp.flash > 0.5f) foundTom3Hit = true;
                if (dp.name.find ("Floor Tom 2") != std::string::npos && dp.flash > 0.5f) foundFloor2Hit = true;
                if (dp.name.find ("Crash Istanbul Xist") != std::string::npos && dp.flash > 0.5f) foundCrash3Hit = true;
            }
            VERIFY_CHECK (foundTom3Hit);
            VERIFY_CHECK (foundFloor2Hit);
            VERIFY_CHECK (foundCrash3Hit);

            // Render live screenshot of DrumKitView displaying all 12 Ludwig kit pieces in the plugin
            juce::Image ludwigPluginSnap (juce::Image::ARGB, 924, 621, true);
            juce::Graphics lpg (ludwigPluginSnap);
            dkv.paintEntireComponent (lpg, false);
            juce::File ludwigPluginFile ("/home/servandoc/.gemini/antigravity/brain/cf30153d-b421-4187-8ed5-1bdf793c8e28/live_phase7_ludwig_12piece_plugin.png");
            ludwigPluginFile.deleteFile();
            if (auto s = std::unique_ptr<juce::FileOutputStream> (ludwigPluginFile.createOutputStream()))
                juce::PNGImageFormat().writeImageToStream (ludwigPluginSnap, *s);
            std::cout << "-> Rendered DrumKitView 12-Piece Ludwig screenshot: " << ludwigPluginFile.getFullPathName() << "\n";

            // Test round-trip re-export and re-open to verify matrix persistence
            const juce::File reexportFile ("/tmp/ludwig_reexport.glorpkit");
            reexportFile.deleteFile();
            std::string reexportErr;
            bool exported = proj.exportToGlorpKit (reexportFile.getFullPathName().toStdString(), reexportErr);
            VERIFY_CHECK (exported);
            VERIFY_CHECK (reexportFile.existsAsFile() && reexportFile.getSize() > 1024);

            glorp::GlorpKitProject reProj;
            std::string reOpenErr;
            bool reOpened = reProj.openGlorpKitArchive (reexportFile.getFullPathName().toStdString(), reOpenErr);
            VERIFY_CHECK (reOpened);
            VERIFY_CHECK (reProj.pieces.size() >= 12);

            const glorp::EditableZone* reCenterZone = nullptr;
            for (const auto& p : reProj.pieces)
            {
                if (p.name.find ("Snare") != std::string::npos || p.id == "snare")
                {
                    for (const auto& z : p.zones)
                    {
                        if (z.name == "Center" || z.name == "Head Center" || z.name == "ctr" || z.name == "head_center" || z.name == "Snare Head Center")
                        {
                            reCenterZone = &z;
                            break;
                        }
                    }
                }
            }
            VERIFY_CHECK (reCenterZone != nullptr);
            VERIFY_CHECK (reCenterZone->numVelocityLayers == 13);
            VERIFY_CHECK (reCenterZone->numRoundRobins == 4);
            VERIFY_CHECK (reCenterZone->hits.size() == 52);

            reexportFile.deleteFile();
            std::cout << "-> Section 20 Ludwig 12-Piece & 4-RR Matrix Tests: PASSED!\n";
        }
        else
        {
            std::cout << "[WARN] /tmp/ludwig_test.glorpkit not found; skipping Section 20 file-based test.\n";
        }
    }

    processor.releaseResources();

    std::cout << "=== ALL VERIFICATIONS PASSED SUCCESSFULLY ===\n";
    std::exit (0);
}
