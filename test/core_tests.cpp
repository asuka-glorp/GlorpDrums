/*
    Parser tests for the DrumGizmo format layer.

    Deliberately has no JUCE dependency: if this builds and passes, the kit
    loading logic is sound regardless of how the audio engine turns out.
*/

#include "DrumKit.h"
#include "KitScanner.h"
#include "PitchShifter.h"
#include "XmlLite.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using namespace glorp;

namespace
{
    int failures = 0;
    int checks = 0;

    void check (bool condition, const std::string& what)
    {
        ++checks;

        if (! condition)
        {
            ++failures;
            std::cerr << "  FAIL: " << what << "\n";
        }
    }

    void checkEq (const std::string& actual, const std::string& expected, const std::string& what)
    {
        ++checks;

        if (actual != expected)
        {
            ++failures;
            std::cerr << "  FAIL: " << what << " (expected \"" << expected << "\", got \"" << actual << "\")\n";
        }
    }

    void checkEq (int actual, int expected, const std::string& what)
    {
        ++checks;

        if (actual != expected)
        {
            ++failures;
            std::cerr << "  FAIL: " << what << " (expected " << expected << ", got " << actual << ")\n";
        }
    }

    void section (const std::string& name)
    {
        std::cout << "- " << name << "\n";
    }

    void writeFile (const fs::path& p, const std::string& contents)
    {
        fs::create_directories (p.parent_path());
        std::ofstream out (p, std::ios::binary);
        out << contents;
    }

    //==========================================================================
    void testXmlBasics()
    {
        section ("XmlLite");

        std::string err;
        auto root = parseXml ("<?xml version=\"1.0\"?>\n<a x='1' y=\"two\"><b/>text<c>3</c></a>", err);

        check (root != nullptr, "parses a simple document: " + err);
        checkEq (root->tag, "a", "root tag");
        checkEq (root->attribute ("x"), "1", "single-quoted attribute");
        checkEq (root->attribute ("y"), "two", "double-quoted attribute");
        checkEq (root->attribute ("missing", "dflt"), "dflt", "attribute fallback");
        checkEq (root->childText ("c"), "3", "child text");
        check (root->child ("b") != nullptr, "self-closing child");
        check (root->child ("nope") == nullptr, "missing child is null");
        checkEq (root->childrenNamed ("c").size(), 1u, "childrenNamed count");

        // Comments, CDATA-ish content, nested identical tags.
        auto nested = parseXml ("<!-- hi --><r><t><t>deep</t></t></r>", err);
        check (nested != nullptr, "parses comments");
        checkEq (nested->child ("t")->child ("t")->text, "deep", "nested same-name children");

        // Entities.
        auto ents = parseXml ("<r a=\"a&amp;b\">&lt;tag&gt; &#65;&#x42;</r>", err);
        checkEq (ents->attribute ("a"), "a&b", "amp entity");
        checkEq (ents->text, "<tag> AB", "lt/gt and numeric entities");

        // Malformed input must fail loudly rather than half-parse.
        check (parseXml ("<a><b></a>", err) == nullptr, "mismatched close tag fails");
        check (parseXml ("<a attr=unquoted/>", err) == nullptr, "unquoted attribute fails");
        check (parseXml ("", err) == nullptr, "empty document fails");
        check (parseXml ("<a", err) == nullptr, "truncated document fails");
        check (parseXml ("<a>&nope;</a>", err) == nullptr, "unknown entity fails");

        // Numeric attributes.
        auto nums = parseXml ("<r f=\"1.5\" i=\"42\" b=\"true\" bad=\"xyz\"/>", err);
        check (std::abs (nums->attributeFloat ("f", 0.0f) - 1.5f) < 0.0001f, "attributeFloat");
        checkEq (nums->attributeInt ("i", 0), 42, "attributeInt");
        check (nums->attributeBool ("b", false), "attributeBool true");
        check (std::abs (nums->attributeFloat ("bad", 9.0f) - 9.0f) < 0.0001f, "attributeFloat fallback on junk");
    }

    //==========================================================================
    /// Builds a kit in the modern (0.9.17+) format and checks everything wires up.
    void testModernKit (const fs::path& root)
    {
        section ("drumkit.xml, modern format");

        writeFile (root / "TestKit" / "drumkit.xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<drumkit samplerate="48000" version="2.1.0">
  <metadata>
    <version>1.2.3</version>
    <title>Test Kit</title>
    <description>A kit for tests</description>
    <author>Nobody</author>
    <license>CC0</license>
  </metadata>
  <channels>
    <channel name="OHL"><title>Overhead left</title></channel>
    <channel name="OHR"/>
    <channel name="Snare_top"/>
    <channel name="Room"/>
  </channels>
  <instruments>
    <instrument name="Snare" file="Snare/Snare.xml">
      <channelmap in="OHL" out="OHL" main="true"/>
      <channelmap in="OHR" out="OHR" main="true"/>
      <channelmap in="Room" out="Room"/>
      <channelmap in="Unmapped" out="Room"/>
    </instrument>
    <instrument name="HihatClosed" group="hihat" file="Hihat/HihatClosed.xml">
      <chokes>
        <choke instrument="HihatOpen" choketime="50"/>
        <choke instrument="HihatHalfOpen"/>
      </chokes>
      <channelmap in="OHL" out="OHL" main="true"/>
      <channelmap in="OHR" out="OHR" main="true"/>
    </instrument>
    <instrument name="Broken" file="Nope/Missing.xml"/>
    <instrument name="NoSamples" file="Empty/Empty.xml"/>
    <instrument file="NoName/NoName.xml"/>
  </instruments>
</drumkit>)");

        writeFile (root / "TestKit" / "Snare" / "Snare.xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<instrument version="2.0">
  <metadata><title>Fat Snare</title></metadata>
  <samples>
    <sample name="Snare-1" power="0.9">
      <audiofile channel="OHL" file="samples/s1.wav" filechannel="1"/>
      <audiofile channel="OHR" file="samples/s1.wav" filechannel="2"/>
      <audiofile channel="Room" file="samples/s1.wav" filechannel="3"/>
    </sample>
    <sample name="Snare-2" power="1.0">
      <audiofile channel="OHL" file="samples/s2.wav" filechannel="1"/>
      <audiofile channel="OHR" file="samples/s2.wav" filechannel="2"/>
      <audiofile channel="Room" file="samples/s2.wav" filechannel="3"/>
    </sample>
    <sample name="Empty"/>
  </samples>
</instrument>)");

        writeFile (root / "TestKit" / "Hihat" / "HihatClosed.xml", R"(<?xml version="1.0" encoding="UTF-8"?>
<instrument version="2.0">
  <samples>
    <sample name="Hc-1">
      <audiofile channel="OHL" file="samples/hc.wav" filechannel="1"/>
      <audiofile channel="OHR" file="samples/hc.wav" filechannel="2"/>
    </sample>
  </samples>
</instrument>)");

        writeFile (root / "TestKit" / "Empty" / "Empty.xml", R"(<?xml version="1.0"?>
<instrument version="2.0"><samples/></instrument>)");

        writeFile (root / "TestKit" / "midimap.xml", R"(<?xml version="1.0"?>
<midimap>
  <map note="38" instr="Snare"/>
  <map note="42" instr="HihatClosed"/>
  <map note="999" instr="Snare"/>
  <map note="40" instr="DoesNotExist"/>
</midimap>)");

        const auto kit = DrumKit::load ((root / "TestKit" / "drumkit.xml").string());

        checkEq (kit.error, "", "kit loads without error");
        checkEq (kit.title, "Test Kit", "title");
        checkEq (kit.displayName(), "Test Kit", "display name");
        checkEq (kit.declaredSampleRate, 48000, "samplerate");
        checkEq (kit.formatVersion, "2.1.0", "format version");
        checkEq (kit.author, "Nobody", "author");
        checkEq (static_cast<int> (kit.channels.size()), 4, "channel count");
        checkEq (kit.channels[0].title, "Overhead left", "channel title");
        checkEq (kit.indexOfKitChannel ("Room"), 3, "kit channel lookup");

        // The three broken instruments must have been dropped with warnings.
        checkEq (static_cast<int> (kit.instruments.size()), 2, "usable instrument count");
        check (kit.instruments.size() == 2, "only sound instruments survive");

        const auto snareIdx = kit.indexOfInstrument ("Snare");
        check (snareIdx == 0, "Snare is first");

        if (snareIdx >= 0)
        {
            const auto& snare = kit.instruments[static_cast<size_t> (snareIdx)];

            checkEq (snare.displayName, "Fat Snare", "instrument title overrides prettified name");
            checkEq (static_cast<int> (snare.samples.size()), 2, "samples with no audiofiles are dropped");
            check (std::abs (snare.samples[0].power - 0.9f) < 0.001f, "sample power");
            checkEq (static_cast<int> (snare.samples[0].audioFiles.size()), 3, "audiofiles per sample");

            // filechannel is 1-based in XML, 0-based for us.
            checkEq (snare.samples[0].audioFiles[0].fileChannel, 0, "filechannel 1 -> 0");
            checkEq (snare.samples[0].audioFiles[2].fileChannel, 2, "filechannel 3 -> 2");
            checkEq (snare.samples[0].audioFiles[0].file, "samples/s1.wav", "audiofile path");
            checkEq (snare.baseDir, (root / "TestKit" / "Snare").string(), "instrument base dir");

            checkEq (static_cast<int> (snare.resolvedChannels.size()), 2, "resolved channels per sample");
            checkEq (static_cast<int> (snare.resolvedChannels[0].size()), 3, "resolved channel count");
            checkEq (snare.resolvedChannels[0][0].kitChannel, "OHL", "resolves through channelmap");
            check (snare.resolvedChannels[0][0].main, "main flag carried through");

            // 'Unmapped' is an instrument channel with no matching <audiofile>; must not crash.
            check (! snare.resolvedChannels[0].empty(), "still resolves after unmapped channelmap");
        }

        const auto hhIdx = kit.indexOfInstrument ("HihatClosed");

        if (hhIdx >= 0)
        {
            const auto& hh = kit.instruments[static_cast<size_t> (hhIdx)];

            checkEq (hh.group, "hihat", "group attribute");
            checkEq (static_cast<int> (hh.chokes.size()), 2, "choke count");
            check (std::abs (hh.chokes[0].chokeTimeMs - 50.0f) < 0.001f, "explicit choketime");
            check (! hh.chokes[1].hasChokeTime, "missing choketime flagged");
            check (std::abs (hh.chokes[1].chokeTimeMs - 100.0f) < 0.001f, "default choketime");
            checkEq (hh.index, 1, "instrument index assigned");
        }

        // Midimap: the two in-range notes with known instruments survive; note 999 is out
        // of MIDI range and note 40 names an instrument that failed to load.
        checkEq (static_cast<int> (kit.suggestedMap.size()), 2, "valid midimap entries only");
        check (kit.suggestedMap.count (38) == 1, "note 38 mapped");
        check (kit.suggestedMap.count (42) == 1, "note 42 mapped");

        bool sawWarning = false;
        for (auto& w : kit.warnings)
            if (w.find ("Missing.xml") != std::string::npos) sawWarning = true;

        check (sawWarning, "missing instrument xml produces a warning naming the file");
    }

    //==========================================================================
    void testLegacyKit (const fs::path& root)
    {
        section ("drumkit.xml, legacy pre-0.9.17 format");

        writeFile (root / "LegacyKit" / "drumkit.xml", R"(<?xml version="1.0"?>
<drumkit name="Old kit" description="uses the old layout">
  <channels>
    <channel name="OHL"/>
    <channel name="OHR"/>
  </channels>
  <instruments>
    <instrument name="Kick" file="Kick/Kick.xml">
      <channelmap in="OHL-kick" out="OHL">
        <bleed>0.5</bleed>
      </channelmap>
      <channelmap in="OHR-kick" out="OHR">
        <bleed>0.5</bleed>
      </channelmap>
    </instrument>
  </instruments>
</drumkit>)");

        writeFile (root / "LegacyKit" / "Kick" / "Kick.xml", R"(<?xml version='1.0' encoding='UTF-8'?>
<instrument version="2.0" name="Kick">
  <samples>
    <sample name="Kick-1" power="0.0150941">
      <audiofile channel="OHL-kick" file="samples/1-Kick.wav" filechannel="1"/>
      <audiofile channel="OHR-kick" file="samples/1-Kick.wav" filechannel="2"/>
    </sample>
  </samples>
</instrument>)");

        const auto kit = DrumKit::load ((root / "LegacyKit" / "drumkit.xml").string());

        checkEq (kit.error, "", "legacy kit loads");
        checkEq (kit.title, "Old kit", "legacy kit carries name attribute as title");
        checkEq (kit.displayName(), "Old kit", "display name uses legacy kit name");
        checkEq (static_cast<int> (kit.instruments.size()), 1, "legacy instrument count");

        if (! kit.instruments.empty())
        {
            const auto& kick = kit.instruments[0];

            checkEq (kick.name, "Kick", "legacy instrument name");
            checkEq (kick.displayName, "Kick", "display name prettified from name");
            checkEq (static_cast<int> (kick.channelMaps.size()), 2, "legacy channelmap count");
            check (std::abs (kick.channelMaps[0].bleed - 0.5f) < 0.001f, "bleed parsed from child element");

            // The instrument channel is 'OHL-kick' but the kit channel is 'OHL'.
            checkEq (static_cast<int> (kick.resolvedChannels[0].size()), 2, "legacy channelmap resolves");
            checkEq (kick.resolvedChannels[0][0].kitChannel, "OHL", "legacy in -> out rename honoured");
        }

        // Test fallback to folder name when name attribute is omitted.
        writeFile (root / "NoNameKit" / "drumkit.xml", R"(<?xml version="1.0"?>
<drumkit description="uses the old layout without name">
  <channels><channel name="OHL"/></channels>
  <instruments><instrument name="Kick" file="Kick/Kick.xml"/></instruments>
</drumkit>)");
        writeFile (root / "NoNameKit" / "Kick" / "Kick.xml", R"(<?xml version='1.0' encoding='UTF-8'?>
<instrument version="2.0" name="Kick">
  <samples><sample name="k"><audiofile channel="OHL" file="k.wav" filechannel="1"/></sample></samples>
</instrument>)");

        const auto noNameKit = DrumKit::load ((root / "NoNameKit" / "drumkit.xml").string());
        checkEq (noNameKit.error, "", "nameless legacy kit loads");
        checkEq (noNameKit.title, "", "nameless legacy kit has empty title");
        checkEq (noNameKit.displayName(), "NoNameKit", "display name falls back to folder name");
    }

    //==========================================================================
    void testIdentityMapping (const fs::path& root)
    {
        section ("instrument with no channelmap (identity fallback)");

        writeFile (root / "BareKit" / "drumkit.xml", R"(<?xml version="1.0"?>
<drumkit version="2.1.0">
  <channels><channel name="Tom1"/></channels>
  <instruments>
    <instrument name="Tom1" file="Tom1/Tom1.xml"/>
  </instruments>
</drumkit>)");

        writeFile (root / "BareKit" / "Tom1" / "Tom1.xml", R"(<?xml version="1.0"?>
<instrument version="2.0">
  <samples>
    <sample name="t">
      <audiofile channel="Tom1" file="t.wav" filechannel="1"/>
      <audiofile channel="Ghost" file="t.wav" filechannel="2"/>
    </sample>
  </samples>
</instrument>)");

        const auto kit = DrumKit::load ((root / "BareKit" / "drumkit.xml").string());

        checkEq (kit.error, "", "bare kit loads");
        checkEq (static_cast<int> (kit.instruments.size()), 1, "bare instrument loads");

        if (! kit.instruments.empty())
        {
            // 'Tom1' exists as a kit channel so identity mapping applies; 'Ghost' does not.
            checkEq (static_cast<int> (kit.instruments[0].resolvedChannels[0].size()), 1, "only known channels resolve");
            checkEq (kit.instruments[0].resolvedChannels[0][0].kitChannel, "Tom1", "identity mapping");
        }
    }

    //==========================================================================
    void testFailureModes (const fs::path& root)
    {
        section ("failure modes");

        const auto missing = DrumKit::load ((root / "Nope" / "drumkit.xml").string());
        check (! missing.error.empty(), "missing file reports an error");
        check (missing.instruments.empty(), "missing file yields no instruments");

        writeFile (root / "Broken" / "drumkit.xml", "<notadrumkit/>");
        check (! DrumKit::load ((root / "Broken" / "drumkit.xml").string()).error.empty(), "wrong root tag errors");

        writeFile (root / "Broken2" / "drumkit.xml", "<drumkit version=\"1\"></drumkit>");
        check (! DrumKit::load ((root / "Broken2" / "drumkit.xml").string()).error.empty(), "no instruments node errors");
    }

    //==========================================================================
    /// Writes a minimal but *loadable-looking* kit directory at `dir`.
    void writeMinimalKit (const fs::path& dir, const std::string& title, bool withSampleFile = true)
    {
        writeFile (dir / "drumkit.xml",
                   "<drumkit version=\"2.1.0\"><metadata><title>" + title + "</title></metadata>"
                   "<channels><channel name=\"OH\"/></channels>"
                   "<instruments><instrument name=\"Kick\" file=\"Kick/Kick.xml\"/></instruments></drumkit>");

        writeFile (dir / "Kick" / "Kick.xml",
                   "<instrument version=\"2.0\"><samples><sample name=\"k\">"
                   "<audiofile channel=\"OH\" file=\"samples/k.wav\" filechannel=\"1\"/>"
                   "</sample></samples></instrument>");

        if (withSampleFile)
            writeFile (dir / "Kick" / "samples" / "k.wav", "not really a wav, but it exists");
    }

    void testKitScanner (const fs::path& root)
    {
        section ("KitScanner");

        const auto kitsRoot = root / "kits";

        // Top level, nested one level, and nested two levels.
        writeMinimalKit (kitsRoot / "Alpha", "Alpha Kit");
        writeMinimalKit (kitsRoot / "Nested" / "Beta", "Beta Kit");
        writeMinimalKit (kitsRoot / "Nested" / "Deeper" / "Gamma", "Gamma Kit");

        // Uppercase filename variant should still be found.
        writeMinimalKit (kitsRoot / "OddCase", "Odd Case Kit");
        fs::rename (kitsRoot / "OddCase" / "drumkit.xml", kitsRoot / "OddCase" / "DRUMKIT.XML");

        // Not a kit: wrong root element.
        writeFile (kitsRoot / "NotAKit" / "drumkit.xml", "<settings/>");

        // A kit with no usable instruments should be flagged invalid.
        writeFile (kitsRoot / "EmptyKit" / "drumkit.xml",
                   "<drumkit version=\"2.1.0\"><channels/><instruments/></drumkit>");

        // Junk directories must be skipped, and must not hide a kit behind them.
        writeMinimalKit (kitsRoot / ".git" / "ShouldNotAppear", "Junk Kit");

        const auto found = KitScanner::findKitsUnder (kitsRoot.string());

        // 4 loadable kits + NotAKit + EmptyKit. The one under .git must not appear.
        checkEq (static_cast<int> (found.size()), 6, "recursive kit discovery including nested and odd-cased");

        auto entries = KitScanner::scan ({ kitsRoot.string() });

        int valid = 0;
        int invalid = 0;

        for (auto& e : entries)
        {
            if (e.valid) ++valid; else ++invalid;

            if (! e.valid && e.error.empty())
            {
                ++failures;
                std::cerr << "  FAIL: invalid kit '" << e.drumkitPath << "' gave no error text\n";
            }
        }

        checkEq (valid, 4, "valid kits discovered");
        checkEq (invalid, 2, "invalid kits discovered");

        // Titles come from metadata; valid entries sort before invalid ones.
        check (entries.size() == 6, "total entries");
        check (entries.front().valid, "valid kits sort first");
        checkEq (entries.front().displayName, "Alpha Kit", "title from metadata");

        bool sawOddCase = false;
        for (auto& e : entries)
            if (e.folderName == "OddCase") sawOddCase = true;

        check (sawOddCase, "uppercase DRUMKIT.XML is discovered");

        // A second root overlapping the first must not duplicate kits.
        const auto overlap = KitScanner::scan ({ kitsRoot.string(), (kitsRoot / "Nested").string() });
        checkEq (static_cast<int> (overlap.size()), static_cast<int> (entries.size()), "overlapping roots do not duplicate");

        // Path resolution.
        check (! KitScanner::resolvePath ("~").empty(), "~ resolves");
        check (KitScanner::resolvePath ("~someuser/x").empty(), "~user is rejected");
        checkEq (KitScanner::resolvePath ("/a/b/../c"), "/a/c", "path is normalised");
        check (KitScanner::findKitsUnder ("/definitely/not/here").empty(), "missing root yields nothing");

        // GLORP_KITS_PATH drives the default search.
        ::setenv ("GLORP_KITS_PATH", kitsRoot.string().c_str(), 1);
        auto defaults = KitScanner::defaultSearchPaths();
        ::unsetenv ("GLORP_KITS_PATH");

        check (! defaults.empty(), "env var contributes a search root");
        check (! defaults.empty() && defaults.front() == kitsRoot.string(), "env var root is searched first");

        // Full load of a discovered kit should succeed end to end.
        DrumKit kit;
        bool ok = false;

        for (auto& e : entries)
        {
            if (e.folderName != "Alpha") continue;
            ok = KitScanner::loadKit (e.drumkitPath, kit);
            break;
        }

        check (ok, "loadKit on a scanned entry succeeds");
        checkEq (static_cast<int> (kit.instruments.size()), 1, "scanned kit has one instrument");

        if (! kit.instruments.empty())
        {
            const auto resolved = kit.instruments[0].samplePath (kit.instruments[0].samples[0].audioFiles[0]);
            check (fs::exists (resolved), "resolved sample path exists on disk");
            check (fs::path (resolved).is_absolute(), "resolved sample path is absolute");
        }
    }
}

//==============================================================================
// PitchShifter
//==============================================================================

namespace ts
{
    constexpr double sampleRate = 48000.0;
    constexpr double pi = 3.14159265358979323846;

    /// Runs a whole mono buffer through the shifter in realistic block sizes.
    std::vector<float> runShift (const std::vector<float>& input, float semitones,
                                 int outExtra = 0, std::vector<float>* perBlock = nullptr)
    {
        PitchShifter shifter;
        shifter.prepare (sampleRate, 512);
        shifter.setSemitones (semitones);

        std::vector<float> output;
        constexpr int block = 512;

        std::vector<float> inBlock (block, 0.0f);
        std::vector<float> outBlock (block, 0.0f);

        size_t consumed = 0;

        for (;;)
        {
            const int n = static_cast<int> (std::min (static_cast<size_t> (block), input.size() - consumed));

            std::fill (inBlock.begin(), inBlock.end(), 0.0f);
            std::copy (input.begin() + static_cast<long> (consumed),
                       input.begin() + static_cast<long> (consumed + n), inBlock.begin());

            std::fill (outBlock.begin(), outBlock.end(), 0.0f);
            shifter.processMono (inBlock.data(), n, outBlock.data(), block);

            output.insert (output.end(), outBlock.begin(), outBlock.end());
            consumed += static_cast<size_t> (n);

            if (n == 0)
                break;
        }

        if (outExtra > 0)
        {
            std::vector<float> tail (static_cast<size_t> (outExtra), 0.0f);
            std::fill (inBlock.begin(), inBlock.end(), 0.0f);
            shifter.processMono (inBlock.data(), 0, tail.data(), outExtra);
            output.insert (output.end(), tail.begin(), tail.end());
        }

        if (perBlock != nullptr)
            *perBlock = output;

        return output;
    }

    std::vector<float> sine (double frequency, double seconds, double amplitude = 0.5)
    {
        std::vector<float> out (static_cast<size_t> (seconds * sampleRate));

        for (size_t i = 0; i < out.size(); ++i)
            out[i] = static_cast<float> (amplitude * std::sin (2.0 * pi * frequency * static_cast<double> (i) / sampleRate));

        return out;
    }

    /// A percussive click: sharp attack, exponential decay. Closer to a real hit
    /// than a steady sine, which is the only fair way to test the transient path.
    std::vector<float> click (double frequency, double seconds, double decaySeconds = 0.25)
    {
        std::vector<float> out (static_cast<size_t> (seconds * sampleRate));

        for (size_t i = 0; i < out.size(); ++i)
        {
            const double t = static_cast<double> (i) / sampleRate;
            const double env = std::exp (-t / decaySeconds);
            out[i] = static_cast<float> (0.8 * env * std::sin (2.0 * pi * frequency * t));
        }

        return out;
    }

    /// Frequency of the strongest bin, by Goertzel over a frequency sweep.
    /// Crude, but it resolves a couple of cents at these bin widths, which is all
    /// the tolerance we need.
    double dominantFrequency (const std::vector<float>& signal, size_t from, size_t to,
                              double fMin, double fMax)
    {
        double bestFreq = 0.0;
        double bestPower = -1.0;

        constexpr int steps = 400;

        for (int s = 0; s <= steps; ++s)
        {
            const double f = fMin + (fMax - fMin) * s / steps;
            const double w = 2.0 * pi * f / sampleRate;
            const double coeff = 2.0 * std::cos (w);

            double sPrev = 0.0, sPrev2 = 0.0;

            for (size_t i = from; i < to && i < signal.size(); ++i)
            {
                const double s = signal[i] + coeff * sPrev - sPrev2;
                sPrev2 = sPrev;
                sPrev = s;
            }

            const double power = sPrev2 * sPrev2 + sPrev * sPrev - coeff * sPrev * sPrev2;

            if (power > bestPower)
            {
                bestPower = power;
                bestFreq = f;
            }
        }

        return bestFreq;
    }

    double rms (const std::vector<float>& signal, size_t from, size_t to)
    {
        double total = 0.0;
        size_t count = 0;

        for (size_t i = from; i < to && i < signal.size(); ++i)
        {
            total += static_cast<double> (signal[i]) * signal[i];
            ++count;
        }

        return count > 0 ? std::sqrt (total / count) : 0.0;
    }
}

void testPitchShifter()
{
    using namespace ts;
    section ("PitchShifter");

    // -- bypass ----------------------------------------------------------
    {
        const auto input = sine (220.0, 0.1);
        auto out = runShift (input, 0.0f);

        check (out.size() >= input.size(), "bypass produces at least the input length");

        bool identical = true;
        for (size_t i = 0; i < input.size(); ++i)
            if (out[i] != input[i]) { identical = false; break; }

        check (identical, "zero shift is a bit-exact bypass");

        PitchShifter shifter;
        shifter.prepare (sampleRate, 512);
        shifter.setSemitones (0.0f);
        checkEq (shifter.latencySamples(), 0, "bypass reports no latency");
    }

    // -- pitch accuracy ---------------------------------------------------
    {
        // Measured well past the dry attack region and before the tail dies out.
        const double f0 = 220.0;
        const auto input = sine (f0, 0.5);

        for (float semitones : { -12.0f, -5.0f, -1.0f, 1.0f, 3.0f, 7.0f, 12.0f })
        {
            const auto out = runShift (input, semitones, 4096);

            const size_t from = static_cast<size_t> (0.05 * sampleRate);
            const size_t to = from + 16384;

            const double expected = f0 * std::pow (2.0, semitones / 12.0);

            // Search a tight window around the expected result.
            const double measured = dominantFrequency (out, from, to,
                                                       expected * 0.97, expected * 1.03);

            const double cents = 1200.0 * std::log2 (measured / expected);

            check (std::abs (cents) < 25.0,
                   "shift of " + std::to_string (semitones) + " semitones lands within 25 cents (got "
                       + std::to_string (cents) + ")");
        }
    }

    // -- duration is preserved -------------------------------------------
    {
        const auto input = click (80.0, 0.5, 0.3);
        auto out = runShift (input, 7.0f, 8192);

        PitchShifter shifter;
        shifter.prepare (sampleRate, 512);
        shifter.setSemitones (7.0f);

        const auto latency = static_cast<size_t> (shifter.latencySamples());

        // Find where the tail drops below the noise floor.
        auto lastAudible = out.size();

        for (size_t i = out.size(); i-- > 0;)
        {
            if (std::abs (out[i]) > 1.0e-3f) { lastAudible = i; break; }
        }

        const double stretchedBy = (static_cast<double> (lastAudible) + 1.0 - static_cast<double> (latency))
                                 / static_cast<double> (input.size());

        check (std::abs (stretchedBy - 1.0) < 0.05,
               "duration preserved under a 7 semitone shift (ratio "
                   + std::to_string (stretchedBy) + ")");
    }

    // -- the attack is passed through untouched ---------------------------
    {
        const auto input = click (120.0, 0.3, 0.2);
        auto out = runShift (input, 5.0f);

        PitchShifter shifter;
        shifter.prepare (sampleRate, 512);
        shifter.setSemitones (5.0f);

        // The dry attack is the input delayed by exactly the reported latency.
        const auto latency = static_cast<size_t> (shifter.latencySamples());
        const auto dryLength = std::min<size_t> (400, static_cast<size_t> (shifter.getDryAttackSamples()));

        size_t worst = 0;

        for (size_t i = 0; i < dryLength && i + latency < input.size() && i < out.size(); ++i)
            worst = std::max (worst, static_cast<size_t> (std::abs (out[i + latency] - input[i]) * 1.0e6f));

        check (worst == 0, "the dry attack region is the input delayed, bit for bit");
        check (dryLength > 200, "the dry attack covers a meaningful slice of the hit");

        // And it should actually be the loudest part, i.e. we are not bypassing
        // the loud transient by accident.
        const double attackRms = rms (out, latency, latency + 200);
        const double bodyRms = rms (out, latency + 8000, latency + 16000);
        check (attackRms > bodyRms, "attack region is louder than the sustained body");
    }

    // -- level sanity -----------------------------------------------------
    {
        const auto input = click (100.0, 0.5, 0.25);

        for (float semitones : { -12.0f, 0.0f, 4.0f, 12.0f })
        {
            const auto out = runShift (input, semitones, 4096);

            float peak = 0.0f;

            for (float v : out)
                peak = std::max (peak, std::abs (v));

            check (peak < 1.6f, "output does not run away at " + std::to_string (semitones)
                                 + " semitones (peak " + std::to_string (peak) + ")");

            const double inRms = rms (input, 0, input.size());
            const double outRms = rms (out, 3000, 20000);

            check (outRms < inRms * 2.0 && outRms > inRms * 0.25,
                   "level stays in the same ballpark at " + std::to_string (semitones) + " semitones");
        }
    }

    // -- clamping and configuration ---------------------------------------
    {
        PitchShifter shifter;
        shifter.prepare (sampleRate, 512);

        shifter.setSemitones (100.0f);
        check (std::abs (shifter.getSemitones() - PitchShifter::maxSemitones) < 0.001f, "semitones clamped at the top");

        shifter.setSemitones (-100.0f);
        check (std::abs (shifter.getSemitones() + PitchShifter::maxSemitones) < 0.001f, "semitones clamped at the bottom");

        shifter.setSemitones (0.0f);
        check (shifter.getRatio() == 1.0f, "ratio is 1 at zero shift");

        shifter.setSemitones (12.0f);
        check (std::abs (shifter.getRatio() - 2.0f) < 0.001f, "ratio is 2 at +12 semitones");

        check (shifter.getFrameSize() > 0 && shifter.getHopSize() > 0, "frame geometry is sane");

        // Reset must clear state without disturbing configuration.
        shifter.setSemitones (6.0f);
        shifter.reset();
        check (std::abs (shifter.getSemitones() - 6.0f) < 0.001f, "reset preserves semitones");
        check (shifter.latencySamples() == shifter.getFrameSize(), "latency equals the WSOLA window");
    }

    // -- silence in, silence out ------------------------------------------
    {
        const std::vector<float> silence (4096, 0.0f);
        auto out = runShift (silence, 6.0f, 2048);

        float peak = 0.0f;
        for (float v : out) peak = std::max (peak, std::abs (v));

        check (peak < 1.0e-6f, "silence stays silent (no instability)");
    }
}

void testDemoKit()
{
    section ("GlorpDemoKit verification");

    const fs::path demoKitPath = fs::current_path() / "test" / "kits" / "GlorpDemoKit" / "drumkit.xml";

    if (! fs::exists (demoKitPath))
    {
        std::cout << "  (Skipping GlorpDemoKit check - file not found at " << demoKitPath << ")\n";
        return;
    }

    const auto kit = DrumKit::load (demoKitPath.string());

    checkEq (kit.error, "", "demo kit loads without error");
    checkEq (kit.title, "Glorp Demo Kit", "demo kit title");
    checkEq (static_cast<int> (kit.instruments.size()), 23, "23 instruments in demo kit");
    checkEq (static_cast<int> (kit.channels.size()), 2, "2 channels in demo kit (OHL, OHR)");
    checkEq (static_cast<int> (kit.suggestedMap.size()), 24, "24 notes mapped in midimap.xml");

    // Verify HihatClosed chokes HihatOpen
    const auto hhIdx = kit.indexOfInstrument ("HihatClosed");
    check (hhIdx >= 0, "HihatClosed exists");
    if (hhIdx >= 0)
    {
        const auto& hh = kit.instruments[static_cast<size_t> (hhIdx)];
        checkEq (hh.group, "hihat", "HihatClosed group is hihat");
        check (hh.chokes.size() >= 1, "HihatClosed has chokes");
        if (! hh.chokes.empty())
            checkEq (hh.chokes[0].instrument, "HihatOpen", "HihatClosed chokes HihatOpen");
    }

    // Verify all samples have valid audio files on disk
    for (const auto& inst : kit.instruments)
    {
        check (inst.samples.size() >= 2, inst.name + " has at least 2 sample hits");
        for (const auto& sample : inst.samples)
        {
            check (! sample.audioFiles.empty(), inst.name + " " + sample.name + " has audio files");
            for (const auto& ref : sample.audioFiles)
            {
                const auto absPath = inst.samplePath (ref);
                check (fs::exists (absPath), "sample file exists: " + absPath);
            }
        }
    }
}

int main (int argc, char** argv)
{
    const fs::path work = argc > 1 ? fs::path (argv[1])
                                   : fs::temp_directory_path() / "glorpdrums_parser_test";

    std::error_code ec;
    fs::remove_all (work, ec);
    fs::create_directories (work);

    std::cout << "GlorpDrums parser tests\n";

    if (getenv("GLORP_ONLY_PS") != nullptr)
    {
        std::cout << "GlorpDrums parser tests\n";
        testPitchShifter();
        std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
        return failures == 0 ? 0 : 1;
    }

    testXmlBasics();
    testModernKit (work);
    testLegacyKit (work);
    testIdentityMapping (work);
    testFailureModes (work);
    testKitScanner (work);
    testPitchShifter();
    testDemoKit();

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";

    if (failures > 0)
        std::cout << failures << " FAILURES\n";

    return failures == 0 ? 0 : 1;
}
