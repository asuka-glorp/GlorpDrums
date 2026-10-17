/*
    GlorpKit.h - Single-file drumkit container format (.glorpkit).

    A .glorpkit file is an uncompressed or compressed PKZIP archive containing:
      - drumkit.xml (or kit.xml) manifest
      - midimap.xml (or variant-specific midimap)
      - instrument XML files
      - WAV audio files (stored uncompressed for zero-copy memory mapping)

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

struct GlorpKitManifest
{
    std::string title;
    std::string description;
    std::string author;
    std::string version;
    int instrumentCount = 0;
    int declaredSampleRate = 48000;
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

    /// Locates the root manifest (e.g. drumkit.xml, kit.xml, or any *kit*.xml).
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
