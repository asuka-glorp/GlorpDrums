/*
    GlorpKit.cpp - Single-file drumkit container format (.glorpkit).
*/

#include "GlorpKit.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace glorp
{

namespace
{
    uint16_t readU16LE (const char* p) noexcept
    {
        const auto* u = reinterpret_cast<const uint8_t*> (p);
        return static_cast<uint16_t> (u[0] | (static_cast<uint16_t> (u[1]) << 8));
    }

    uint32_t readU32LE (const char* p) noexcept
    {
        const auto* u = reinterpret_cast<const uint8_t*> (p);
        return static_cast<uint32_t> (u[0])
             | (static_cast<uint32_t> (u[1]) << 8)
             | (static_cast<uint32_t> (u[2]) << 16)
             | (static_cast<uint32_t> (u[3]) << 24);
    }

    std::string normalizePath (std::string p)
    {
        std::replace (p.begin(), p.end(), '\\', '/');

        // Strip leading "./" or "/"
        while (p.rfind ("./", 0) == 0)
            p = p.substr (2);
        while (! p.empty() && p.front() == '/')
            p = p.substr (1);

        return p;
    }

    std::string toLower (std::string s)
    {
        std::transform (s.begin(), s.end(), s.begin(),
                        [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
        return s;
    }
}

//==============================================================================
bool GlorpKitReader::isGlorpKitFile (const std::string& path)
{
    if (path.empty())
        return false;

    const fs::path p (path);
    auto ext = toLower (p.extension().string());

    if (ext == ".glorpkit" || ext == ".zip")
        return true;

    // Check PKZIP magic header (0x04034B50)
    std::ifstream f (path, std::ios::binary);
    if (! f.is_open())
        return false;

    char magic[4] = {};
    if (f.read (magic, 4) && std::memcmp (magic, "PK\x03\x04", 4) == 0)
        return true;

    return false;
}

std::string GlorpKitReader::getCacheDirectoryFor (const std::string& archivePath)
{
    const auto stem = fs::path (archivePath).stem().string();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName().toStdString();
    return home + "/.cache/GlorpDrums/kits/" + stem;
}

bool GlorpKitReader::quickReadManifest (const std::string& archivePath, GlorpKitManifest& manifest)
{
    GlorpKitReader reader;
    if (! reader.open (archivePath))
    {
        manifest.error = "Could not open archive: " + archivePath;
        return false;
    }

    const auto manifestPath = reader.findManifestPath();
    if (manifestPath.empty())
    {
        manifest.error = "Archive has no drumkit.xml or kit.xml manifest";
        return false;
    }

    const auto xmlStr = reader.readEntryString (manifestPath);
    if (xmlStr.empty())
    {
        manifest.error = "Empty manifest in archive";
        return false;
    }

    std::string err;
    auto root = parseXml (xmlStr, err);
    if (root == nullptr)
    {
        manifest.error = "Failed to parse XML: " + err;
        return false;
    }

    manifest.title = root->attribute ("name");
    manifest.declaredSampleRate = root->attributeInt ("samplerate", 48000);

    if (auto* meta = root->child ("metadata"))
    {
        if (auto* t = meta->child ("title"))       manifest.title = t->text;
        if (auto* d = meta->child ("description")) manifest.description = d->text;
        if (auto* a = meta->child ("author"))      manifest.author = a->text;
        if (auto* v = meta->child ("version"))     manifest.version = v->text;
    }

    if (manifest.title.empty())
        manifest.title = fs::path (archivePath).stem().string();

    if (auto* insts = root->child ("instruments"))
        manifest.instrumentCount = static_cast<int> (insts->childrenNamed ("instrument").size());

    manifest.valid = true;
    return true;
}

//==============================================================================
bool GlorpKitReader::open (const std::string& archivePath)
{
    opened_ = false;
    entries_.clear();
    entryList_.clear();
    archivePath_ = archivePath;

    std::ifstream stream (archivePath, std::ios::binary);
    if (! stream.is_open())
        return false;

    if (! parseZipHeaders (stream))
        return false;

    opened_ = true;
    return true;
}

bool GlorpKitReader::parseZipHeaders (std::ifstream& stream)
{
    stream.seekg (0, std::ios::end);
    const auto fileSize = static_cast<int64_t> (stream.tellg());

    if (fileSize < 22)
        return false;

    // Search for End of Central Directory record (EOCD signature: 0x06054B50)
    // EOCD is at least 22 bytes and at most 65535 + 22 bytes from file end.
    const auto maxSearch = std::min<int64_t> (fileSize, 65535 + 22);
    const auto searchStart = fileSize - maxSearch;

    std::vector<char> searchBuf (static_cast<size_t> (maxSearch));
    stream.seekg (searchStart);
    stream.read (searchBuf.data(), maxSearch);

    int64_t eocdOffset = -1;
    for (int64_t i = maxSearch - 22; i >= 0; --i)
    {
        if (std::memcmp (searchBuf.data() + i, "PK\x05\x06", 4) == 0)
        {
            eocdOffset = searchStart + i;
            break;
        }
    }

    if (eocdOffset < 0)
        return false;

    stream.seekg (eocdOffset);
    char eocd[22];
    if (! stream.read (eocd, 22))
        return false;

    const auto totalEntries = readU16LE (eocd + 10);
    const auto centralDirSize = readU32LE (eocd + 12);
    const auto centralDirOffset = readU32LE (eocd + 16);

    (void) centralDirSize;

    // Read central directory entries
    stream.seekg (centralDirOffset);

    for (int i = 0; i < totalEntries; ++i)
    {
        char header[46];
        if (! stream.read (header, 46))
            break;

        if (std::memcmp (header, "PK\x01\x02", 4) != 0)
            break;

        const auto method = readU16LE (header + 10);
        const auto compressedSize = readU32LE (header + 20);
        const auto uncompressedSize = readU32LE (header + 24);
        const auto filenameLen = readU16LE (header + 28);
        const auto extraLen = readU16LE (header + 30);
        const auto commentLen = readU16LE (header + 32);
        const auto localHeaderOffset = readU32LE (header + 42);

        std::string filename (static_cast<size_t> (filenameLen), '\0');
        stream.read (&filename[0], filenameLen);

        // Skip extra field and comment
        stream.seekg (static_cast<std::streamoff> (extraLen + commentLen), std::ios::cur);

        // Directories end with '/'
        if (! filename.empty() && (filename.back() == '/' || filename.back() == '\\'))
            continue;

        GlorpKitEntry entry;
        entry.name = normalizePath (filename);
        entry.uncompressedSize = uncompressedSize;
        entry.compressedSize = compressedSize;
        entry.isCompressed = (method != 0);

        // Calculate actual data offset by reading local header
        const auto curPos = stream.tellg();
        stream.seekg (localHeaderOffset);

        char localHeader[30];
        if (stream.read (localHeader, 30) && std::memcmp (localHeader, "PK\x03\x04", 4) == 0)
        {
            const auto localFileLen = readU16LE (localHeader + 26);
            const auto localExtraLen = readU16LE (localHeader + 28);
            entry.dataOffset = localHeaderOffset + 30 + localFileLen + localExtraLen;
        }
        else
        {
            entry.dataOffset = localHeaderOffset + 30 + filenameLen;
        }

        stream.seekg (curPos);

        entries_[entry.name] = entry;
        entryList_.push_back (entry.name);
    }

    return ! entries_.empty();
}

//==============================================================================
bool GlorpKitReader::hasEntry (const std::string& relativePath) const
{
    return getEntry (relativePath) != nullptr;
}

const GlorpKitEntry* GlorpKitReader::getEntry (const std::string& relativePath) const
{
    const auto norm = normalizePath (relativePath);
    const auto it = entries_.find (norm);
    if (it != entries_.end())
        return &it->second;

    // Case-insensitive fallback
    const auto lower = toLower (norm);
    for (const auto& pair : entries_)
    {
        if (toLower (pair.first) == lower)
            return &pair.second;
    }

    // Basename fallback (e.g. "samples/kick.wav" -> matches "kick.wav")
    for (const auto& pair : entries_)
    {
        if (fs::path (pair.first).filename().string() == fs::path (norm).filename().string())
            return &pair.second;
    }

    return nullptr;
}

std::string GlorpKitReader::readEntryString (const std::string& relativePath) const
{
    const auto* entry = getEntry (relativePath);
    if (entry == nullptr)
        return {};

    if (! entry->isCompressed)
    {
        std::ifstream stream (archivePath_, std::ios::binary);
        if (! stream.is_open())
            return {};

        stream.seekg (entry->dataOffset);
        std::string content (static_cast<size_t> (entry->uncompressedSize), '\0');
        stream.read (&content[0], entry->uncompressedSize);
        return content;
    }

    // Fallback: decompression via juce::ZipFile
    const juce::File archiveFile { archivePath_ };
    juce::ZipFile zip (archiveFile);
    const auto* ze = zip.getEntry (juce::String (entry->name), true);
    if (ze == nullptr)
        return {};

    std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*ze));
    if (in == nullptr)
        return {};

    return in->readEntireStreamAsString().toStdString();
}

std::string GlorpKitReader::findManifestPath() const
{
    // Try exact root manifests first
    for (const auto& candidate : { "drumkit.xml", "kit.xml", "Drumkit.xml", "Kit.xml" })
        if (hasEntry (candidate))
            return candidate;

    // Any top-level XML containing "kit" or "drum"
    for (const auto& name : entryList_)
    {
        if (fs::path (name).extension() == ".xml" && ! name.empty() && name.find ('/') == std::string::npos)
        {
            const auto low = toLower (name);
            if (low.find ("kit") != std::string::npos || low.find ("drum") != std::string::npos)
                return name;
        }
    }

    return {};
}

std::string GlorpKitReader::findMidimapPath() const
{
    for (const auto& candidate : { "midimap.xml", "Midimap.xml" })
        if (hasEntry (candidate))
            return candidate;

    for (const auto& name : entryList_)
    {
        if (fs::path (name).extension() == ".xml")
        {
            const auto low = toLower (name);
            if (low.find ("midimap") != std::string::npos)
                return name;
        }
    }

    return {};
}

std::vector<std::string> GlorpKitReader::listEntries() const
{
    return entryList_;
}

bool GlorpKitReader::extractToDirectory (const std::string& targetDirectory) const
{
    const juce::File archiveFile { archivePath_ };
    juce::ZipFile zip (archiveFile);
    const auto result = zip.uncompressTo (juce::File { targetDirectory }, true);
    return result.wasOk();
}

} // namespace glorp
