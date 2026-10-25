/*
    GlorpKit.h - Single-file drumkit container format (.glorpkit).

    A .glorpkit file is an uncompressed or compressed PKZIP archive containing:
      - kit.xml (or legacy drumkit.xml) manifest
      - instruments/<piece>/instrument.xml
      - instruments/<piece>/<zone>/zone.xml
      - instruments/<piece>/<zone>/samples/ (WAV audio files)
      - presets/factory-mixer.xml

    When stored uncompressed (ZIP_STORED, method 0), SampleCache memory-maps
    exact byte ranges directly from the .glorpkit file without temporary files.
*/

#pragma once

#include "XmlLite.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace glorp
{

struct GlorpKitEntry
{
    std::string name;
    int64_t dataOffset = 0;
    int64_t uncompressedSize = 0;
    int64_t compressedSize = 0;
    bool isCompressed = false;
};

struct GlorpMic
{
    std::string name;
    std::string title;
    float defaultLevel = 1.0f;
};

struct GlorpGraphic
{
    std::string name;
    std::string src;
};

struct GlorpSampleRef
{
    std::string name;
    std::string file;
    std::string mic;
    int fileChannel = 0; // 0-based
    float power = 1.0f;
    int velocityLayer = 0; // 1-based (0 if not explicitly tagged)
    int roundRobin = 0;    // 1-based (0 if not explicitly tagged)
    int64_t archiveOffset = -1;
    int64_t archiveLength = -1;
};

struct GlorpChokeTarget
{
    std::string instrument;
    std::string zone;
    float chokeTimeMs = 40.0f;
};

struct GlorpZoneSpec
{
    std::string name;
    std::string graphic;
    int note = -1;
    int altNote = -1;
    float power = 1.0f;
    int roundRobin = 1;
    int velocityLayers = 0;
    std::vector<GlorpSampleRef> samples;
    std::vector<GlorpChokeTarget> chokes;
};

struct GlorpInstrumentSpec
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
    std::vector<GlorpZoneSpec> zones;
};

struct GlorpMixerSetting
{
    std::map<std::string, float> micLevels;
    std::map<std::string, float> instrumentLevels;
    std::map<std::string, float> instrumentPans;
};

struct GlorpKitManifest
{
    std::string title;
    std::string description;
    std::string author;
    std::string version;
    std::string license;
    int instrumentCount = 0;
    int declaredSampleRate = 48000;
    bool isNative = false;
    std::string hitGlowColor = "#ff8200";
    std::vector<GlorpMic> mics;
    bool valid = false;
    std::string error;
};

class GlorpKitReader
{
public:
    GlorpKitReader() = default;
    ~GlorpKitReader() = default;

    /// Checks if a file path has a .glorpkit or .zip extension or zip magic header.
    static bool isGlorpKitFile (const std::string& path);

    /// Fast extraction of kit title, instrument count and metadata without extracting audio.
    static bool quickReadManifest (const std::string& archivePath, GlorpKitManifest& manifest);

    /// Opens the .glorpkit archive and parses its central directory table.
    bool open (const std::string& archivePath);

    bool isOpen() const noexcept { return opened_; }
    const std::string& getArchivePath() const noexcept { return archivePath_; }

    /// Look up an entry by relative path inside the archive.
    const GlorpKitEntry* getEntry (const std::string& relativePath) const;
    bool hasEntry (const std::string& relativePath) const;

    /// Reads an XML or text entry into a std::string.
    std::string readEntryString (const std::string& relativePath) const;

    /// Reads binary bytes of an entry (e.g. embedded PNG asset or audio slice).
    bool readEntryBytes (const std::string& relativePath, std::vector<uint8_t>& outBytes) const;

    /// Returns true if the archive has a native GlorpKit kit.xml (<glorpkit> root).
    bool isNativeGlorpKit() const;

    /// Reads native kit.xml metadata, mics, and graphics.
    bool readNativeKitManifest (GlorpKitManifest& manifest, std::vector<GlorpGraphic>& graphics) const;

    /// Reads all instruments in instruments/*/instrument.xml and their zones.
    bool readNativeInstruments (std::vector<GlorpInstrumentSpec>& instruments) const;

    /// Reads presets/factory-mixer.xml if present.
    bool readFactoryMixer (GlorpMixerSetting& mixer) const;

    /// Locates the root manifest (e.g. kit.xml, drumkit.xml, or any *kit*.xml).
    std::string findManifestPath() const;

    /// Locates the midimap (e.g. midimap.xml or Midimap_*.xml).
    std::string findMidimapPath() const;

    /// Lists all file paths inside the archive.
    std::vector<std::string> listEntries() const;

    /// Extracts all entries to targetDirectory (used when archive is compressed).
    bool extractToDirectory (const std::string& targetDirectory) const;

    /// Returns cache directory path for extracted archives: ~/.cache/GlorpDrums/kits/<name>
    static std::string getCacheDirectoryFor (const std::string& archivePath);

private:
    std::string archivePath_;
    bool opened_ = false;
    std::map<std::string, GlorpKitEntry> entries_;
    std::vector<std::string> entryList_;

    bool parseZipHeaders (std::ifstream& stream);
};

} // namespace glorp
