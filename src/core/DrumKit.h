/*
    DrumKit.h - in-memory model of a DrumGizmo drumkit.

    DrumGizmo's on-disk format has three moving parts (see
    https://www.drumgizmo.org/wiki/doku.php?id=documentation:file_formats):

      drumkit.xml    the manifest: kit metadata, the kit's output "channels"
                     (which are really microphone/bleed positions), and the list
                     of instruments with their channel mappings, choke groups
                     and sample file locations.
      <instr>.xml    one per instrument: the samples that make up the instrument,
                     each split across the instrument's own channels.
      midimap.xml    optional; note -> instrument name. We treat it as a
                     suggested starting mapping, never as gospel, since the user
                     can remap everything.

    Both the current format (0.9.17+) and the legacy pre-0.9.17 layout are
    accepted, because plenty of kits in the wild are still on the old one.
*/

#pragma once

#include "XmlLite.h"
#include "BleedMatrix.h"

#include <map>
#include <string>
#include <vector>

namespace glorp
{

//==============================================================================
/// One <audiofile> element: which channel of which wav file feeds an instrument channel.
struct AudioFileRef
{
    std::string channel;      ///< instrument-side channel name (matches `in` of a channelmap)
    std::string file;         ///< path relative to the instrument XML's directory
    int fileChannel = 0;      ///< zero-based channel index inside that wav file
    int64_t archiveOffset = -1; ///< byte offset in .glorpkit archive (or -1 if plain file)
    int64_t archiveLength = -1; ///< byte length in .glorpkit archive
};

/** One round of an instrument's samples, e.g. "Snare-1" of "Snare-1..5".
    The `power` attribute is DrumGizmo's relative hit strength, used to weight
    round-robin "AUTO" selection.
*/
struct SampleHit
{
    std::string name;
    float power = 1.0f;
    std::vector<AudioFileRef> audioFiles;
};

/// A kit channel, i.e. a mic/bleed position declared by the kit (OH-L, Snare_top, Room, ...).
struct KitChannel
{
    std::string name;
    std::string title;
    std::string source;   ///< older kits call the microphone attribute "microphone"
};

/** Ties one instrument-side channel to one kit channel.
    `main` marks the channels that survive the kit's "main bleed" fader.
*/
struct ChannelMap
{
    std::string in;
    std::string out;
    bool main = false;
    float bleed = 1.0f;
};

/// An instrument that chokes another when triggered (hi-hat open -> closed).
struct ChokeSpec
{
    std::string instrument;
    float chokeTimeMs = 100.0f;
    bool hasChokeTime = false;
};

struct Instrument
{
    std::string name;          ///< semantic name used by midimap.xml
    std::string displayName;   ///< human title; falls back to `name`
    std::string group;         ///< instruments sharing a group choke each other

    std::string xmlPath;       ///< absolute path of the instrument XML
    std::string baseDir;       ///< directory that `file` attributes are relative to

    std::vector<ChannelMap> channelMaps;
    std::vector<ChokeSpec> chokes;
    std::vector<SampleHit> samples;

    /// Kit channel name -> indices into the matching SampleHit::audioFiles.
    /// Built once at load so the audio thread never has to search by name.
    /// Outer vector is over samples, inner over audio files, so the hot loop can
    /// iterate channel-major without any lookups.
    struct ChannelSource
    {
        std::string kitChannel;   ///< which kit channel this feeds
        int audioFileIndex = 0;   ///< index into SampleHit::audioFiles
        bool main = false;
        float bleed = 1.0f;       ///< bleed attribute from channelmap
    };
    std::vector<std::vector<ChannelSource>> resolvedChannels;

    int index = -1;             ///< position in the kit, i.e. which output bus it uses
    std::string archivePath;    ///< set when loaded from a .glorpkit archive

    std::string logicalName;    ///< Logical piece name (e.g. "Snare")
    std::string zoneName;       ///< Zone name (e.g. "head", "rimshot")
    BleedPiece bleedPiece = BleedPiece::snare; ///< Bleed piece classification
    int outputBus = 0;          ///< Target output bus

    /// Absolute path of an audio file, resolved against the instrument's directory.
    /// DrumGizmo writes these relative to the instrument XML, so every consumer needs
    /// this rather than the raw string.
    std::string samplePath (const AudioFileRef& ref) const;

    bool isValid() const noexcept { return ! samples.empty(); }
};

struct DrumKit
{
    // -- identity ---------------------------------------------------------
    std::string xmlPath;
    std::string title;
    std::string description;
    std::string author;
    std::string email;
    std::string website;
    std::string license;
    std::string version;        ///< kit metadata version
    std::string formatVersion;  ///< drumkit `version` attribute

    int declaredSampleRate = 48000;
    std::string hitGlowColor = "#ff8200";

    std::string imagePath;
    std::vector<std::pair<std::string, std::string>> imageClickMap; ///< colour hex -> instrument name

    // -- contents ---------------------------------------------------------
    std::vector<KitChannel> channels;
    std::vector<Instrument> instruments;

    /// Note -> instrument name, as suggested by the kit's midimap.xml (if any).
    std::map<int, std::string> suggestedMap;

    bool isArchive = false;
    std::string archivePath;

    // -- native GlorpKit model --------------------------------------------
    struct ZoneItem
    {
        std::string name;
        int samplerIndex = -1;
        int note = -1;
        int altNote = -1;
        std::string graphic;
    };

    struct LogicalPiece
    {
        std::string name;
        std::string id;
        std::string graphic;
        int outputBus = 0;
        std::string chokeGroup;
        float normX = 0.0f;
        float normY = 0.0f;
        float normW = 160.0f;
        int zOrder = 0;
        bool hasCustomLayout = false;
        std::vector<uint8_t> customPngBytes;
        std::vector<ZoneItem> zones;
    };

    bool isNativeGlorpKit = false;
    std::vector<LogicalPiece> logicalPieces;
    std::map<std::string, float> factoryMicLevels;
    std::map<std::string, float> factoryInstrumentLevels;
    std::map<std::string, float> factoryInstrumentPans;

    // -- diagnostics ------------------------------------------------------
    std::string error;          ///< non-empty if the kit failed to load properly
    std::vector<std::string> warnings;

    /// Human-readable name for the kit dropdown: "<title>" or the folder name.
    std::string displayName() const;

    /// Loads a kit from a path to its drumkit.xml or a .glorpkit archive. Always returns a DrumKit;
    /// check `error` and the emptiness of `instruments` to know whether it worked.
    static DrumKit load (const std::string& drumkitPath);

    /// Index of the instrument with this semantic name, or -1.
    int indexOfInstrument (const std::string& name) const noexcept;

    /// Resolves a kit channel name to its index in `channels`, or -1.
    int indexOfKitChannel (const std::string& name) const noexcept;

private:
    static DrumKit loadFromArchive (const std::string& archivePath);
    static DrumKit loadNativeGlorpKit (class GlorpKitReader& reader, const std::string& archivePath);
    static DrumKit loadLegacyArchive (class GlorpKitReader& reader, const std::string& archivePath);

    bool parseInstrument (Instrument& instrument, const XmlNode& node,
                          const std::string& kitDir, std::string& error);
    bool loadInstrumentSamples (Instrument& instrument, std::string& error);
    bool resolveChannels (Instrument& instrument, std::string& error);
};

} // namespace glorp
