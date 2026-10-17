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
#include <set>

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

    std::string parentDirectory (const std::string& path)
    {
        const auto slash = path.find_last_of ("/\\");
        if (slash == std::string::npos)
            return ".";
        return path.substr (0, slash);
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

    if (reader.isNativeGlorpKit())
    {
        std::vector<GlorpGraphic> graphics;
        if (reader.readNativeKitManifest (manifest, graphics))
        {
            // Count pieces
            manifest.instrumentCount = 0;
            for (const auto& name : reader.listEntries())
            {
                if (name.rfind ("instruments/", 0) == 0 && name.find ("/instrument.xml") != std::string::npos)
                    ++manifest.instrumentCount;
            }
            manifest.valid = true;
            return true;
        }
    }

    // Fallback: Legacy DrumGizmo manifest in archive
    const auto manifestPath = reader.findManifestPath();
    if (manifestPath.empty())
    {
        manifest.error = "Archive has no kit.xml or drumkit.xml manifest";
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
    archivePath_ = archivePath;
    entries_.clear();
    entryList_.clear();
    opened_ = false;

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
    // Search backward from end of file for EOCD signature (PK\x05\x06)
    stream.seekg (0, std::ios::end);
    const auto fileSize = static_cast<int64_t> (stream.tellg());
    if (fileSize < 22)
        return false;

    const int64_t searchLen = std::min<int64_t> (fileSize, 65557);
    const int64_t searchStart = fileSize - searchLen;
    stream.seekg (searchStart);

    std::vector<char> searchBuf (static_cast<size_t> (searchLen));
    if (! stream.read (searchBuf.data(), searchLen))
        return false;

    int64_t eocdOffset = -1;
    for (int64_t i = searchLen - 22; i >= 0; --i)
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
const GlorpKitEntry* GlorpKitReader::getEntry (const std::string& relativePath) const
{
    const auto norm = normalizePath (relativePath);
    auto it = entries_.find (norm);
    if (it != entries_.end())
        return &it->second;

    // Case-insensitive fallback
    const auto normLower = toLower (norm);
    for (const auto& [name, entry] : entries_)
    {
        if (toLower (name) == normLower)
            return &entry;
    }

    return nullptr;
}

bool GlorpKitReader::hasEntry (const std::string& relativePath) const
{
    return getEntry (relativePath) != nullptr;
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

bool GlorpKitReader::isNativeGlorpKit() const
{
    if (! hasEntry ("kit.xml"))
        return false;

    const auto xmlStr = readEntryString ("kit.xml");
    std::string err;
    auto root = parseXml (xmlStr, err);
    return root != nullptr && root->tag == "glorpkit";
}

bool GlorpKitReader::readNativeKitManifest (GlorpKitManifest& manifest, std::vector<GlorpGraphic>& graphics) const
{
    if (! hasEntry ("kit.xml"))
        return false;

    const auto xmlStr = readEntryString ("kit.xml");
    std::string err;
    auto root = parseXml (xmlStr, err);
    if (root == nullptr || root->tag != "glorpkit")
        return false;

    manifest.isNative = true;
    manifest.title = root->attribute ("name");
    manifest.author = root->attribute ("author");
    manifest.version = root->attribute ("version", "1.0");
    manifest.license = root->attribute ("license");
    manifest.declaredSampleRate = root->attributeInt ("samplerate", 48000);

    if (auto* d = root->child ("description"))
        manifest.description = d->text;

    if (auto* micsNode = root->child ("mics"))
    {
        for (auto* m : micsNode->childrenNamed ("mic"))
        {
            GlorpMic mic;
            mic.name = m->attribute ("name");
            mic.title = m->attribute ("title");
            mic.defaultLevel = m->attributeFloat ("default", 1.0f);
            manifest.mics.push_back (std::move (mic));
        }
    }

    if (auto* gNode = root->child ("graphics"))
    {
        for (auto* g : gNode->childrenNamed ("graphic"))
        {
            GlorpGraphic gr;
            gr.name = g->attribute ("name");
            gr.src = g->attribute ("src");
            graphics.push_back (std::move (gr));
        }
    }

    manifest.valid = true;
    return true;
}

bool GlorpKitReader::readNativeInstruments (std::vector<GlorpInstrumentSpec>& instruments) const
{
    std::set<std::string> instPaths;
    for (const auto& name : entryList_)
    {
        if (name.rfind ("instruments/", 0) == 0 && name.find ("/instrument.xml") != std::string::npos)
            instPaths.insert (name);
    }

    for (const auto& instXmlPath : instPaths)
    {
        const auto xmlStr = readEntryString (instXmlPath);
        std::string err;
        auto root = parseXml (xmlStr, err);
        if (root == nullptr || root->tag != "glorp-instrument")
            continue;

        const auto instDir = parentDirectory (instXmlPath);
        const auto pieceId = fs::path (instDir).filename().string();

        GlorpInstrumentSpec spec;
        spec.name = root->attribute ("name");
        spec.id = pieceId;
        spec.graphic = root->attribute ("graphic");
        spec.outputBus = root->attributeInt ("output-bus", 0);
        spec.chokeGroup = root->attribute ("choke-group", "none");

        // Parse zones
        for (auto* zn : root->childrenNamed ("zone"))
        {
            GlorpZoneSpec zone;
            zone.name = zn->attribute ("name");
            zone.graphic = zn->attribute ("graphic");
            if (zone.graphic.empty())
                zone.graphic = spec.graphic;
            zone.note = zn->attributeInt ("note", -1);
            zone.altNote = zn->attributeInt ("alt-note", -1);

            const auto zoneXmlPath = instDir + "/" + zone.name + "/zone.xml";
            const auto zoneXmlStr = readEntryString (zoneXmlPath);
            if (! zoneXmlStr.empty())
            {
                std::string zerr;
                auto zroot = parseXml (zoneXmlStr, zerr);
                if (zroot != nullptr && zroot->tag == "glorp-zone")
                {
                    zone.power = zroot->attributeFloat ("power", 1.0f);
                    zone.roundRobin = zroot->attributeInt ("round-robin", 1);

                    const auto zoneDir = instDir + "/" + zone.name;

                    for (auto* sn : zroot->childrenNamed ("sample"))
                    {
                        GlorpSampleRef sref;
                        sref.name = sn->attribute ("name");
                        const auto fileAttr = sn->attribute ("file");
                        sref.mic = sn->attribute ("mic", "close");
                        sref.power = sn->attributeFloat ("power", 1.0f);
                        sref.fileChannel = sn->attributeInt ("channel", 1) - 1;
                        if (sref.fileChannel < 0) sref.fileChannel = 0;

                        std::string fullSamplePath = zoneDir + "/" + fileAttr;
                        const auto* entry = getEntry (fullSamplePath);
                        if (entry == nullptr)
                            entry = getEntry (fileAttr);

                        if (entry != nullptr)
                        {
                            sref.file = entry->name;
                            sref.archiveOffset = entry->dataOffset;
                            sref.archiveLength = entry->uncompressedSize;
                            zone.samples.push_back (std::move (sref));
                        }
                    }

                    for (auto* cn : zroot->childrenNamed ("choke"))
                    {
                        GlorpChokeTarget ct;
                        ct.instrument = cn->attribute ("target");
                        ct.chokeTimeMs = cn->attributeFloat ("time", 40.0f);
                        zone.chokes.push_back (std::move (ct));
                    }
                }
            }

            spec.zones.push_back (std::move (zone));
        }

        instruments.push_back (std::move (spec));
    }

    return ! instruments.empty();
}

bool GlorpKitReader::readFactoryMixer (GlorpMixerSetting& mixer) const
{
    if (! hasEntry ("presets/factory-mixer.xml"))
        return false;

    const auto xmlStr = readEntryString ("presets/factory-mixer.xml");
    std::string err;
    auto root = parseXml (xmlStr, err);
    if (root == nullptr || root->tag != "glorp-mixer")
        return false;

    for (auto* mn : root->childrenNamed ("mic"))
    {
        const auto name = mn->attribute ("name");
        const auto level = mn->attributeFloat ("level", 1.0f);
        if (! name.empty())
            mixer.micLevels[name] = level;
    }

    for (auto* in : root->childrenNamed ("instrument"))
    {
        const auto name = in->attribute ("name");
        const auto zone = in->attribute ("zone");
        const auto level = in->attributeFloat ("level", 1.0f);
        const auto pan = in->attributeFloat ("pan", 0.0f);
        const auto key = zone.empty() ? name : (name + "." + zone);
        if (! key.empty())
        {
            mixer.instrumentLevels[key] = level;
            mixer.instrumentPans[key] = pan;
        }
    }

    return true;
}

std::string GlorpKitReader::findManifestPath() const
{
    // Try exact root manifests first
    for (const auto& candidate : { "kit.xml", "drumkit.xml", "Kit.xml", "Drumkit.xml" })
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
