/*
    GlorpKitProject.cpp - Editable in-memory project model for GlorpKitEditor.
*/

#include "GlorpKitProject.h"
#include "../core/GlorpKitWriter.h"
#include "../core/SampleCache.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace glorp
{

namespace
{
    std::string escapeXml (const std::string& s)
    {
        std::string out;
        out.reserve (s.size() + 8);
        for (char c : s)
        {
            switch (c)
            {
                case '&':  out += "&amp;";  break;
                case '<':  out += "&lt;";   break;
                case '>':  out += "&gt;";   break;
                case '"':  out += "&quot;"; break;
                case '\'': out += "&apos;"; break;
                default:   out += c;        break;
            }
        }
        return out;
    }

    std::string sanitizeId (const std::string& raw)
    {
        std::string out;
        for (unsigned char c : raw)
        {
            if (std::isalnum (c))
                out += static_cast<char> (std::tolower (c));
            else if (c == '-' || c == '_')
                out += static_cast<char> (c);
        }
        return out.empty() ? "piece" : out;
    }

    struct DefaultPieceLayout
    {
        const char* id;
        const char* name;
        const char* graphic;
        int bus;
        const char* chokeGroup;
        float x;
        float y;
        float w;
        int z;
    };

    constexpr DefaultPieceLayout kDefaultPieces[] = {
        { "kick",   "Kick 22\" Maple",     "kick",   0, "none",   -155.2f, -329.7f, 345.0f, 1 },
        { "ftom",   "Floor Tom 16\"",      "ftom",   3, "none",    107.2f, -272.1f, 195.0f, 2 },
        { "tom2",   "Mid Tom 12\"",        "tom2",   3, "none",     15.9f, -408.0f, 155.0f, 3 },
        { "tom1",   "High Tom 10\"",       "tom1",   3, "none",   -122.6f, -420.7f, 138.0f, 4 },
        { "snare",  "Snare 14x6.5 Brass",  "snare",  1, "none",   -212.7f, -259.5f, 180.0f, 5 },
        { "hihat",  "Hi-Hat 14\" Custom",  "hihat",  2, "hihat",  -299.6f, -404.7f, 160.0f, 6 },
        { "ride",   "Ride Cymbal 20\"",    "ride",   5, "none",     77.7f, -507.0f, 210.0f, 7 },
        { "crash2", "Crash 2 (Right)",     "crash2", 4, "crash2",  187.3f, -420.8f, 205.0f, 8 },
        { "crash1", "Crash 1 (Left)",      "crash1", 4, "crash1", -235.0f, -502.9f, 190.0f, 9 },
    };

    const DefaultPieceLayout* findDefaultLayoutFor (const std::string& idOrName)
    {
        std::string low = sanitizeId (idOrName);
        for (const auto& d : kDefaultPieces)
            if (low.find (d.id) != std::string::npos)
                return &d;

        if (low.find ("floor") != std::string::npos || low.find ("tom3") != std::string::npos || low.find ("tom16") != std::string::npos)
            return &kDefaultPieces[1];
        return nullptr;
    }
} // namespace

//==============================================================================
GlorpKitProject::GlorpKitProject()
{
    createNewDefaultKit();
}

void GlorpKitProject::createNewDefaultKit()
{
    title = "Custom Birch Studio";
    author = "GlorpDrums User";
    version = "2.0";
    license = "CC-BY-4.0";
    description = "Custom drumkit created in GlorpKit Studio";
    hitGlowColor = "#ff8200";
    sampleRate = 48000;
    loadedFilePath.clear();

    mics = {
        { "close",    "Close Mic (Direct)", 1.0f },
        { "oh-left",  "Overhead Left",      0.8f },
        { "oh-right", "Overhead Right",     0.8f },
        { "room",     "Room Stereo",        0.6f },
        { "amb-left", "Ambience Far",       0.5f },
    };

    pieces.clear();

    for (const auto& d : kDefaultPieces)
    {
        EditablePiece p;
        p.id = d.id;
        p.name = d.name;
        p.graphicId = d.graphic;
        p.outputBus = d.bus;
        p.chokeGroup = d.chokeGroup;
        p.normX = d.x;
        p.normY = d.y;
        p.normW = d.w;
        p.zOrder = d.z;
        p.visible = true;

        auto addZone = [&](const char* zName, int midiNote)
        {
            EditableZone z;
            z.name = zName;
            z.graphicId = p.graphicId;
            z.note = midiNote;

            const float powers[5] = { 1.00f, 0.80f, 0.60f, 0.40f, 0.20f };
            for (int vl = 0; vl < 5; ++vl)
            {
                for (int rr = 0; rr < 3; ++rr)
                {
                    EditableHit h;
                    h.name = p.id + "_" + zName + "_v" + std::to_string (5 - vl) + "_rr" + std::to_string (rr + 1);
                    h.power = powers[vl] - (float) rr * 0.02f;
                    h.velocityLayerIndex = 4 - vl;
                    h.roundRobinIndex = rr;

                    const char* defaultMics[3] = { "close", "oh-left", "room" };
                    for (const char* mc : defaultMics)
                    {
                        EditableMicSample ms;
                        ms.mic = mc;
                        ms.fileName = h.name + "_" + mc + ".wav";
                        h.micSamples.push_back (std::move (ms));
                    }
                    z.hits.push_back (std::move (h));
                }
            }

            p.zones.push_back (std::move (z));
        };

        if (p.id == "kick")
        {
            addZone ("head", 36);
        }
        else if (p.id == "snare")
        {
            addZone ("head", 38);
            addZone ("rimshot", 40);
            addZone ("rimclick", 37);
        }
        else if (p.id == "hihat")
        {
            addZone ("closed", 42);
            addZone ("open", 46);
            addZone ("pedal", 44);
            addZone ("splash", 63);
        }
        else if (p.id == "tom1")
        {
            addZone ("head", 48);
            addZone ("rim", 50);
        }
        else if (p.id == "tom2")
        {
            addZone ("head", 47);
            addZone ("rim", 60);
        }
        else if (p.id == "ftom")
        {
            addZone ("head", 43);
            addZone ("rim", 58);
        }
        else if (p.id == "ride")
        {
            addZone ("bow", 51);
            addZone ("bell", 53);
            addZone ("edge", 59);
        }
        else if (p.id == "crash1")
        {
            addZone ("bow", 49);
            addZone ("edge", 55);
            addZone ("choke", 54);
        }
        else if (p.id == "crash2")
        {
            addZone ("bow", 57);
            addZone ("edge", 52);
            addZone ("choke", 58);
        }

        pieces.push_back (std::move (p));
    }

    normalizeZOrders();
}

//==============================================================================
void GlorpKitProject::normalizeZOrders()
{
    std::vector<size_t> order (pieces.size());
    for (size_t i = 0; i < pieces.size(); ++i)
        order[i] = i;

    std::stable_sort (order.begin(), order.end(), [&](size_t a, size_t b)
    {
        return pieces[a].zOrder < pieces[b].zOrder;
    });

    for (size_t rank = 0; rank < order.size(); ++rank)
        pieces[order[rank]].zOrder = static_cast<int> (rank) + 1;
}

bool GlorpKitProject::movePieceZOrder (int pieceIndex, int deltaZ)
{
    if (pieceIndex < 0 || pieceIndex >= static_cast<int> (pieces.size()) || deltaZ == 0)
        return false;

    normalizeZOrders();
    const int curZ = pieces[static_cast<size_t> (pieceIndex)].zOrder;
    const int targetZ = std::clamp (curZ + deltaZ, 1, static_cast<int> (pieces.size()));
    if (targetZ == curZ)
        return false;

    for (size_t i = 0; i < pieces.size(); ++i)
    {
        if (static_cast<int> (i) == pieceIndex)
            continue;
        if (pieces[i].zOrder == targetZ)
        {
            pieces[i].zOrder = curZ;
            break;
        }
    }
    pieces[static_cast<size_t> (pieceIndex)].zOrder = targetZ;
    normalizeZOrders();
    return true;
}

bool GlorpKitProject::bringPieceToFront (int pieceIndex)
{
    if (pieceIndex < 0 || pieceIndex >= static_cast<int> (pieces.size()))
        return false;

    pieces[static_cast<size_t> (pieceIndex)].zOrder = static_cast<int> (pieces.size()) + 10;
    normalizeZOrders();
    return true;
}

bool GlorpKitProject::sendPieceToBack (int pieceIndex)
{
    if (pieceIndex < 0 || pieceIndex >= static_cast<int> (pieces.size()))
        return false;

    pieces[static_cast<size_t> (pieceIndex)].zOrder = -10;
    normalizeZOrders();
    return true;
}

//==============================================================================
void GlorpKitProject::rebuildZoneLayerGrid (EditableZone& zone, int maxRoundRobins)
{
    if (zone.hits.empty())
        return;

    maxRoundRobins = std::max (1, maxRoundRobins);

    // Sort hits from highest power (Fortissimo) to lowest power (Ghost Note),
    // or ascending by power then grouped into velocity tiers of up to maxRoundRobins hits each.
    std::stable_sort (zone.hits.begin(), zone.hits.end(), [](const EditableHit& a, const EditableHit& b)
    {
        return a.power > b.power;
    });

    for (size_t i = 0; i < zone.hits.size(); ++i)
    {
        zone.hits[i].velocityLayerIndex = static_cast<int> (i) / maxRoundRobins;
        zone.hits[i].roundRobinIndex    = static_cast<int> (i) % maxRoundRobins;
    }
}

void GlorpKitProject::autoComputeZonePowers (int pieceIndex, int zoneIndex)
{
    if (pieceIndex < 0 || pieceIndex >= static_cast<int> (pieces.size()))
        return;
    auto& piece = pieces[static_cast<size_t> (pieceIndex)];
    if (zoneIndex < 0 || zoneIndex >= static_cast<int> (piece.zones.size()))
        return;
    auto& zone = piece.zones[static_cast<size_t> (zoneIndex)];
    if (zone.hits.empty())
        return;

    SampleCache cache;
    std::vector<float> energies (zone.hits.size(), 0.0f);
    float maxEnergy = 0.0f;

    for (size_t h = 0; h < zone.hits.size(); ++h)
    {
        const auto& hit = zone.hits[h];
        if (hit.micSamples.empty())
            continue;

        // Measure RMS + peak energy of the first available mic sample (preferring "close")
        const EditableMicSample* chosen = &hit.micSamples.front();
        for (const auto& ms : hit.micSamples)
            if (ms.mic == "close") { chosen = &ms; break; }

        std::string sampleUri;
        if (! chosen->sourceDiskPath.empty())
            sampleUri = chosen->sourceDiskPath;
        else if (! chosen->sourceArchivePath.empty() && chosen->archiveOffset >= 0)
            sampleUri = "glorpkit://" + chosen->sourceArchivePath + "#"
                      + std::to_string (chosen->archiveOffset) + ":" + std::to_string (chosen->archiveLength);

        if (! sampleUri.empty())
        {
            auto handle = cache.open (sampleUri);
            if (handle.valid && handle.numFrames > 0)
            {
                const int64_t framesToCheck = std::min<int64_t> (handle.numFrames, 24000);
                std::vector<float> buf (static_cast<size_t> (framesToCheck), 0.0f);
                handle.read (0, framesToCheck, chosen->fileChannel, buf.data());

                double sumSq = 0.0;
                float peak = 0.0f;
                for (float s : buf)
                {
                    sumSq += static_cast<double> (s * s);
                    peak = std::max (peak, std::abs (s));
                }
                const float rms = static_cast<float> (std::sqrt (sumSq / static_cast<double> (framesToCheck)));
                energies[h] = 0.65f * peak + 0.35f * rms;
                maxEnergy = std::max (maxEnergy, energies[h]);
            }
        }
    }

    if (maxEnergy > 1e-6f)
    {
        for (size_t h = 0; h < zone.hits.size(); ++h)
        {
            if (energies[h] > 0.0f)
                zone.hits[h].power = std::clamp (energies[h] / maxEnergy, 0.05f, 1.0f);
        }
    }
    else
    {
        // Fallback: distribute evenly from 1.0 down to 0.15
        const size_t N = zone.hits.size();
        for (size_t h = 0; h < N; ++h)
        {
            const float t = (N > 1) ? static_cast<float> (h) / static_cast<float> (N - 1) : 0.0f;
            zone.hits[h].power = 1.0f - t * 0.82f;
        }
    }

    rebuildZoneLayerGrid (zone, 3);
}

int GlorpKitProject::importAudioFilesIntoZone (int pieceIndex, int zoneIndex,
                                               const std::vector<std::string>& filePaths,
                                               const std::string& targetMic)
{
    if (pieceIndex < 0 || pieceIndex >= static_cast<int> (pieces.size()))
        return 0;
    auto& piece = pieces[static_cast<size_t> (pieceIndex)];
    if (zoneIndex < 0 || zoneIndex >= static_cast<int> (piece.zones.size()))
        return 0;
    auto& zone = piece.zones[static_cast<size_t> (zoneIndex)];

    int added = 0;
    for (const auto& p : filePaths)
    {
        std::error_code ec;
        if (! fs::is_regular_file (p, ec))
            continue;

        const fs::path fp (p);
        EditableHit hit;
        hit.name = fp.stem().string();
        hit.power = 1.0f;

        EditableMicSample ms;
        ms.mic = targetMic.empty() ? "close" : targetMic;
        ms.fileName = fp.filename().string();
        ms.sourceDiskPath = fp.string();
        ms.fileChannel = 0;

        hit.micSamples.push_back (std::move (ms));
        zone.hits.push_back (std::move (hit));
        ++added;
    }

    if (added > 0)
        autoComputeZonePowers (pieceIndex, zoneIndex);

    return added;
}

//==============================================================================
bool GlorpKitProject::openGlorpKitArchive (const std::string& archivePath, std::string& outError)
{
    GlorpKitReader reader;
    if (! reader.open (archivePath))
    {
        outError = "Could not open .glorpkit archive: " + archivePath;
        return false;
    }

    if (! reader.isNativeGlorpKit())
    {
        // Legacy DrumGizmo-in-ZIP archive: load via DrumKit::load and convert
        return importDrumGizmoXml (archivePath, outError);
    }

    GlorpKitManifest manifest;
    std::vector<GlorpGraphic> graphics;
    if (! reader.readNativeKitManifest (manifest, graphics))
    {
        outError = "Failed to parse kit.xml in " + archivePath;
        return false;
    }

    std::vector<GlorpInstrumentSpec> instSpecs;
    if (! reader.readNativeInstruments (instSpecs))
    {
        outError = "No valid instruments found in " + archivePath;
        return false;
    }

    title = manifest.title.empty() ? fs::path (archivePath).stem().string() : manifest.title;
    author = manifest.author.empty() ? "Unknown" : manifest.author;
    version = manifest.version.empty() ? "2.0" : manifest.version;
    license = manifest.license.empty() ? "Custom" : manifest.license;
    description = manifest.description;
    hitGlowColor = manifest.hitGlowColor.empty() ? "#ff8200" : manifest.hitGlowColor;
    sampleRate = manifest.declaredSampleRate;
    loadedFilePath = archivePath;

    mics = manifest.mics;
    if (mics.empty())
        mics.push_back ({ "close", "Close Mic (Direct)", 1.0f });

    std::map<std::string, std::string> graphicSrcMap;
    for (const auto& g : graphics)
        graphicSrcMap[g.name] = g.src;

    pieces.clear();
    int fallbackZ = 1;

    for (const auto& spec : instSpecs)
    {
        EditablePiece p;
        p.name = spec.name;
        p.id = spec.id;
        p.graphicId = spec.graphic.empty() ? spec.id : spec.graphic;
        p.outputBus = spec.outputBus;
        p.chokeGroup = spec.chokeGroup;

        if (spec.hasCustomLayout)
        {
            p.normX = spec.normX;
            p.normY = spec.normY;
            p.normW = spec.normW;
            p.zOrder = spec.zOrder > 0 ? spec.zOrder : fallbackZ++;
        }
        else if (const auto* def = findDefaultLayoutFor (spec.id.empty() ? spec.name : spec.id))
        {
            p.normX = def->x;
            p.normY = def->y;
            p.normW = def->w;
            p.zOrder = def->z;
        }
        else
        {
            p.normX = -100.0f + static_cast<float> (fallbackZ * 30);
            p.normY = -350.0f;
            p.normW = 160.0f;
            p.zOrder = fallbackZ++;
        }

        // Load custom PNG bytes if embedded in the archive
        std::string pngArcPath;
        auto git = graphicSrcMap.find (p.graphicId);
        if (git != graphicSrcMap.end() && git->second.rfind ("builtin:", 0) != 0)
            pngArcPath = git->second;
        else if (! p.graphicId.empty() && p.graphicId.rfind ("builtin:", 0) != 0)
            pngArcPath = "assets/" + p.graphicId + ".png";

        if (! pngArcPath.empty() && reader.hasEntry (pngArcPath))
        {
            if (reader.readEntryBytes (pngArcPath, p.customPngBytes))
                p.customPngFileName = fs::path (pngArcPath).filename().string();
        }

        for (const auto& zspec : spec.zones)
        {
            EditableZone z;
            z.name = zspec.name;
            z.graphicId = zspec.graphic.empty() ? p.graphicId : zspec.graphic;
            z.note = zspec.note;
            z.altNote = zspec.altNote;
            z.chokes = zspec.chokes;

            // Group GlorpSampleRef entries into EditableHit by hit name
            std::map<std::string, size_t> hitIdxMap;
            for (const auto& sref : zspec.samples)
            {
                const std::string hitKey = ! sref.name.empty() ? sref.name : fs::path (sref.file).stem().string();
                auto it = hitIdxMap.find (hitKey);
                if (it == hitIdxMap.end())
                {
                    hitIdxMap[hitKey] = z.hits.size();
                    EditableHit hit;
                    hit.name = hitKey;
                    hit.power = sref.power;

                    EditableMicSample ms;
                    ms.mic = sref.mic;
                    ms.fileName = fs::path (sref.file).filename().string();
                    ms.sourceArchivePath = archivePath;
                    ms.archiveOffset = sref.archiveOffset;
                    ms.archiveLength = sref.archiveLength;
                    ms.fileChannel = sref.fileChannel;

                    hit.micSamples.push_back (std::move (ms));
                    z.hits.push_back (std::move (hit));
                }
                else
                {
                    EditableMicSample ms;
                    ms.mic = sref.mic;
                    ms.fileName = fs::path (sref.file).filename().string();
                    ms.sourceArchivePath = archivePath;
                    ms.archiveOffset = sref.archiveOffset;
                    ms.archiveLength = sref.archiveLength;
                    ms.fileChannel = sref.fileChannel;
                    z.hits[it->second].micSamples.push_back (std::move (ms));
                }
            }

            rebuildZoneLayerGrid (z, std::max (1, zspec.roundRobin));
            p.zones.push_back (std::move (z));
        }

        pieces.push_back (std::move (p));
    }

    normalizeZOrders();
    return true;
}

//==============================================================================
bool GlorpKitProject::importDrumGizmoXml (const std::string& drumkitXmlPath, std::string& outError)
{
    DrumKit dk = DrumKit::load (drumkitXmlPath);
    if (! dk.error.empty() || dk.instruments.empty())
    {
        outError = dk.error.empty() ? "No valid instruments in DrumGizmo kit" : dk.error;
        return false;
    }

    title = dk.displayName();
    author = dk.author.empty() ? "DrumGizmo Kit" : dk.author;
    version = "2.0";
    license = dk.license.empty() ? "Custom" : dk.license;
    description = dk.description;
    hitGlowColor = dk.hitGlowColor.empty() ? "#ff8200" : dk.hitGlowColor;
    sampleRate = dk.declaredSampleRate;
    loadedFilePath = drumkitXmlPath;

    mics.clear();
    for (const auto& ch : dk.channels)
        mics.push_back ({ ch.name, ch.title.empty() ? ch.name : ch.title, 1.0f });
    if (mics.empty())
        mics.push_back ({ "close", "Close Mic", 1.0f });

    pieces.clear();
    std::map<std::string, size_t> pieceMap;

    for (const auto& inst : dk.instruments)
    {
        const std::string pName = inst.logicalName.empty() ? inst.displayName : inst.logicalName;
        const std::string pId = sanitizeId (pName);

        size_t pIdx = 0;
        auto it = pieceMap.find (pId);
        if (it == pieceMap.end())
        {
            pIdx = pieces.size();
            pieceMap[pId] = pIdx;

            EditablePiece ep;
            ep.name = pName;
            ep.id = pId;
            ep.graphicId = pId;
            ep.outputBus = inst.outputBus;
            ep.chokeGroup = inst.group.empty() ? "none" : inst.group;

            if (const auto* def = findDefaultLayoutFor (pId))
            {
                ep.graphicId = def->graphic;
                ep.normX = def->x;
                ep.normY = def->y;
                ep.normW = def->w;
                ep.zOrder = def->z;
            }
            else
            {
                ep.normX = -150.0f + static_cast<float> (pIdx * 35);
                ep.normY = -340.0f;
                ep.normW = 165.0f;
                ep.zOrder = static_cast<int> (pIdx) + 1;
            }
            pieces.push_back (std::move (ep));
        }
        else
        {
            pIdx = it->second;
        }

        EditableZone ez;
        ez.name = inst.zoneName.empty() ? "head" : inst.zoneName;
        ez.graphicId = pieces[pIdx].graphicId;
        ez.note = 36;
        for (const auto& [noteNum, mappedName] : dk.suggestedMap)
        {
            if (mappedName == inst.name || mappedName == inst.displayName)
            {
                ez.note = noteNum;
                break;
            }
        }

        for (const auto& sh : inst.samples)
        {
            EditableHit eh;
            eh.name = sh.name;
            eh.power = sh.power;

            for (const auto& af : sh.audioFiles)
            {
                EditableMicSample ms;
                ms.mic = af.channel;
                ms.fileName = fs::path (af.file).filename().string();
                ms.fileChannel = af.fileChannel;
                if (af.archiveOffset >= 0 && ! inst.archivePath.empty())
                {
                    ms.sourceArchivePath = inst.archivePath;
                    ms.archiveOffset = af.archiveOffset;
                    ms.archiveLength = af.archiveLength;
                }
                else
                {
                    ms.sourceDiskPath = inst.samplePath (af);
                }
                eh.micSamples.push_back (std::move (ms));
            }
            if (! eh.micSamples.empty())
                ez.hits.push_back (std::move (eh));
        }

        rebuildZoneLayerGrid (ez, 3);
        pieces[pIdx].zones.push_back (std::move (ez));
    }

    normalizeZOrders();
    return true;
}

//==============================================================================
bool GlorpKitProject::exportToGlorpKit (const std::string& outputArchivePath, std::string& outError) const
{
    // If exporting over the same archive file that is currently open as a slice source,
    // write to a temporary sibling file first and atomically rename on completion.
    const fs::path targetPath = fs::absolute (outputArchivePath);
    const fs::path tempPath   = targetPath.parent_path() / (targetPath.filename().string() + ".tmp_build");

    GlorpKitWriter writer;
    if (! writer.open (tempPath.string()))
    {
        outError = "Could not open output file for writing: " + tempPath.string();
        return false;
    }

    // 1. Build kit.xml (version="2.0")
    {
        std::ostringstream ss;
        ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        ss << "<glorpkit version=\"2.0\" name=\"" << escapeXml (title) << "\" "
           << "author=\"" << escapeXml (author) << "\" "
           << "license=\"" << escapeXml (license) << "\" "
           << "samplerate=\"" << sampleRate << "\" "
           << "hit-glow-color=\"" << escapeXml (hitGlowColor) << "\">\n";

        if (! description.empty())
            ss << "  <description>" << escapeXml (description) << "</description>\n";

        ss << "  <mics>\n";
        for (const auto& m : mics)
        {
            ss << "    <mic name=\"" << escapeXml (m.name) << "\" "
               << "title=\"" << escapeXml (m.title) << "\" "
               << "default=\"" << std::fixed << std::setprecision (2) << m.defaultLevel << "\"/>\n";
        }
        ss << "  </mics>\n";

        ss << "  <graphics>\n";
        std::set<std::string> writtenGraphics;
        for (const auto& p : pieces)
        {
            const std::string gName = p.graphicId.empty() ? p.id : p.graphicId;
            if (writtenGraphics.insert (gName).second)
            {
                const std::string srcAttr = ! p.customPngBytes.empty()
                    ? ("assets/" + gName + ".png")
                    : ("builtin:" + gName);
                ss << "    <graphic name=\"" << escapeXml (gName) << "\" src=\"" << escapeXml (srcAttr) << "\"/>\n";
            }
        }
        ss << "  </graphics>\n";
        ss << "</glorpkit>\n";

        if (! writer.addMemoryString ("kit.xml", ss.str()))
        {
            outError = "Failed to write kit.xml";
            return false;
        }
    }

    // 2. Write embedded custom PNG graphics (assets/*.png)
    {
        std::set<std::string> writtenPngs;
        for (const auto& p : pieces)
        {
            if (! p.customPngBytes.empty())
            {
                const std::string gName = p.graphicId.empty() ? p.id : p.graphicId;
                const std::string arcPng = "assets/" + gName + ".png";
                if (writtenPngs.insert (arcPng).second)
                {
                    if (! writer.addMemoryBytes (arcPng, p.customPngBytes.data(), p.customPngBytes.size()))
                    {
                        outError = "Failed to embed custom PNG asset: " + arcPng;
                        return false;
                    }
                }
            }
        }
    }

    // 3. Write presets/factory-mixer.xml
    {
        std::ostringstream ms;
        ms << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<glorp-mixer>\n";
        for (const auto& m : mics)
            ms << "  <mic name=\"" << escapeXml (m.name) << "\" level=\""
               << std::fixed << std::setprecision (2) << m.defaultLevel << "\"/>\n";

        for (const auto& p : pieces)
            for (const auto& z : p.zones)
                ms << "  <instrument name=\"" << escapeXml (p.name) << "\" zone=\""
                   << escapeXml (z.name) << "\" level=\"1.0\" pan=\"0.0\"/>\n";

        ms << "</glorp-mixer>\n";
        writer.addMemoryString ("presets/factory-mixer.xml", ms.str());
    }

    // 4. Write instruments/<id>/instrument.xml, <zone>/zone.xml, and samples/*.wav
    for (const auto& p : pieces)
    {
        const std::string pId = sanitizeId (p.id.empty() ? p.name : p.id);
        const std::string gName = p.graphicId.empty() ? pId : p.graphicId;

        std::ostringstream is;
        is << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        is << "<glorp-instrument name=\"" << escapeXml (p.name) << "\" "
           << "graphic=\"" << escapeXml (gName) << "\" "
           << "output-bus=\"" << p.outputBus << "\" "
           << "choke-group=\"" << escapeXml (p.chokeGroup) << "\" "
           << std::fixed << std::setprecision (4)
           << "norm-x=\"" << p.normX << "\" "
           << "norm-y=\"" << p.normY << "\" "
           << "norm-w=\"" << p.normW << "\" "
           << "z-order=\"" << p.zOrder << "\">\n";

        for (const auto& z : p.zones)
        {
            is << "  <zone name=\"" << escapeXml (z.name) << "\" "
               << "graphic=\"" << escapeXml (z.graphicId.empty() ? gName : z.graphicId) << "\"";
            if (z.note >= 0)
                is << " note=\"" << z.note << "\"";
            if (z.altNote >= 0)
                is << " alt-note=\"" << z.altNote << "\"";
            is << "/>\n";
        }
        is << "</glorp-instrument>\n";

        const std::string instDir = "instruments/" + pId;
        if (! writer.addMemoryString (instDir + "/instrument.xml", is.str()))
        {
            outError = "Failed to write " + instDir + "/instrument.xml";
            return false;
        }

        for (const auto& z : p.zones)
        {
            const std::string zoneDir = instDir + "/" + z.name;
            std::ostringstream zs;
            zs << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            zs << "<glorp-zone name=\"" << escapeXml (z.name) << "\" power=\"1.0\" round-robin=\"3\">\n";

            std::set<std::string> writtenWavsInZone;

            for (const auto& hit : z.hits)
            {
                for (const auto& ms : hit.micSamples)
                {
                    const std::string wavName = ms.fileName.empty() ? (hit.name + ".wav") : ms.fileName;
                    const std::string relWav = "samples/" + wavName;
                    const std::string fullArcWav = zoneDir + "/" + relWav;

                    zs << "  <sample name=\"" << escapeXml (hit.name) << "\" "
                       << "file=\"" << escapeXml (relWav) << "\" "
                       << "mic=\"" << escapeXml (ms.mic) << "\" "
                       << "power=\"" << std::fixed << std::setprecision (4) << hit.power << "\"";
                    if (ms.fileChannel > 0)
                        zs << " channel=\"" << (ms.fileChannel + 1) << "\"";
                    zs << "/>\n";

                    if (writtenWavsInZone.insert (wavName).second)
                    {
                        if (! ms.sourceDiskPath.empty() && fs::exists (ms.sourceDiskPath))
                        {
                            writer.addFileFromDisk (fullArcWav, ms.sourceDiskPath);
                        }
                        else if (! ms.sourceArchivePath.empty() && ms.archiveOffset >= 0 && ms.archiveLength > 0)
                        {
                            writer.addSliceFromArchive (fullArcWav, ms.sourceArchivePath, ms.archiveOffset, ms.archiveLength);
                        }
                    }
                }
            }

            for (const auto& c : z.chokes)
            {
                zs << "  <choke target=\"" << escapeXml (c.instrument) << "\" "
                   << "time=\"" << static_cast<int> (c.chokeTimeMs) << "\"/>\n";
            }

            zs << "</glorp-zone>\n";
            writer.addMemoryString (zoneDir + "/zone.xml", zs.str());
        }
    }

    if (! writer.finalize())
    {
        outError = "Failed to finalize .glorpkit ZIP_STORED central directory";
        return false;
    }

    std::error_code ec;
    fs::rename (tempPath, targetPath, ec);
    if (ec)
    {
        fs::copy_file (tempPath, targetPath, fs::copy_options::overwrite_existing, ec);
        fs::remove (tempPath, ec);
    }

    return true;
}

//==============================================================================
DrumKit GlorpKitProject::buildRuntimeDrumKit() const
{
    DrumKit kit;
    kit.isNativeGlorpKit = true;
    kit.title = title;
    kit.author = author;
    kit.version = version;
    kit.license = license;
    kit.description = description;
    kit.hitGlowColor = hitGlowColor;
    kit.declaredSampleRate = sampleRate;

    for (const auto& m : mics)
    {
        KitChannel kc;
        kc.name = m.name;
        kc.title = m.title;
        kc.source = m.name;
        kit.channels.push_back (std::move (kc));
    }

    if (kit.channels.empty())
        kit.channels.push_back ({ "close", "Close Mic", "close" });

    for (const auto& p : pieces)
    {
        DrumKit::LogicalPiece lp;
        lp.name = p.name;
        lp.id = p.id;
        lp.graphic = p.graphicId;
        lp.outputBus = p.outputBus;
        lp.chokeGroup = p.chokeGroup;
        lp.normX = p.normX;
        lp.normY = p.normY;
        lp.normW = p.normW;
        lp.zOrder = p.zOrder;
        lp.hasCustomLayout = true;
        lp.customPngBytes = p.customPngBytes;

        for (const auto& z : p.zones)
        {
            Instrument inst;
            inst.name = p.name + "." + z.name;
            inst.displayName = p.name + " " + z.name;
            inst.logicalName = p.name;
            inst.zoneName = z.name;
            inst.bleedPiece = pieceForInstrumentName (p.name);
            inst.group = p.chokeGroup;
            inst.outputBus = p.outputBus;
            inst.index = static_cast<int> (kit.instruments.size());

            for (const auto& hit : z.hits)
            {
                SampleHit sh;
                sh.name = hit.name;
                sh.power = hit.power;

                for (const auto& ms : hit.micSamples)
                {
                    AudioFileRef ref;
                    ref.channel = ms.mic;
                    ref.fileChannel = ms.fileChannel;
                    if (! ms.sourceDiskPath.empty())
                    {
                        ref.file = ms.sourceDiskPath;
                    }
                    else if (! ms.sourceArchivePath.empty() && ms.archiveOffset >= 0)
                    {
                        inst.archivePath = ms.sourceArchivePath;
                        ref.file = ms.fileName;
                        ref.archiveOffset = ms.archiveOffset;
                        ref.archiveLength = ms.archiveLength;
                    }
                    sh.audioFiles.push_back (std::move (ref));
                }

                if (! sh.audioFiles.empty())
                    inst.samples.push_back (std::move (sh));
            }

            // Build resolvedChannels directly
            inst.resolvedChannels.assign (inst.samples.size(), {});
            for (size_t s = 0; s < inst.samples.size(); ++s)
            {
                for (size_t a = 0; a < inst.samples[s].audioFiles.size(); ++a)
                {
                    const auto& af = inst.samples[s].audioFiles[a];
                    Instrument::ChannelSource cs;
                    cs.kitChannel = (kit.indexOfKitChannel (af.channel) >= 0) ? af.channel : kit.channels.front().name;
                    cs.audioFileIndex = static_cast<int> (a);
                    cs.main = (cs.kitChannel == "close" || cs.kitChannel.find ("oh") != std::string::npos);
                    cs.bleed = 1.0f;
                    inst.resolvedChannels[s].push_back (std::move (cs));
                }
            }

            if (z.note >= 0)
                kit.suggestedMap[z.note] = inst.displayName;
            if (z.altNote >= 0)
                kit.suggestedMap[z.altNote] = inst.displayName;

            DrumKit::ZoneItem zi;
            zi.name = z.name;
            zi.samplerIndex = inst.index;
            zi.note = z.note;
            zi.altNote = z.altNote;
            zi.graphic = z.graphicId;
            lp.zones.push_back (std::move (zi));

            kit.instruments.push_back (std::move (inst));
        }

        kit.logicalPieces.push_back (std::move (lp));
    }

    return kit;
}

//==============================================================================
int GlorpKitProject::getSamplerIndexForZone (int pieceIndex, int zoneIndex) const
{
    int count = 0;
    for (size_t p = 0; p < pieces.size(); ++p)
    {
        for (size_t z = 0; z < pieces[p].zones.size(); ++z)
        {
            if ((int) p == pieceIndex && (int) z == zoneIndex)
                return count;
            count++;
        }
    }
    return -1;
}

bool GlorpKitProject::renamePiece (int pieceIndex, const std::string& newName, const std::string& newId)
{
    if (pieceIndex < 0 || pieceIndex >= (int) pieces.size())
        return false;
    if (newName.empty())
        return false;

    const std::string oldName = pieces[(size_t) pieceIndex].name;
    pieces[(size_t) pieceIndex].name = newName;
    if (! newId.empty())
        pieces[(size_t) pieceIndex].id = newId;

    // Update choke targets referencing the old piece name
    for (auto& p : pieces)
        for (auto& z : p.zones)
            for (auto& c : z.chokes)
                if (c.instrument == oldName)
                    c.instrument = newName;

    return true;
}

bool GlorpKitProject::renameMic (int micIndex, const std::string& newName)
{
    if (micIndex < 0 || micIndex >= (int) mics.size())
        return false;
    if (newName.empty())
        return false;

    const std::string oldMic = mics[(size_t) micIndex].name;
    mics[(size_t) micIndex].name = newName;
    mics[(size_t) micIndex].title = newName;

    // Propagate mic rename to all sample layers across all pieces and zones
    for (auto& p : pieces)
        for (auto& z : p.zones)
            for (auto& h : z.hits)
                for (auto& ms : h.micSamples)
                    if (ms.mic == oldMic)
                        ms.mic = newName;

    return true;
}

bool GlorpKitProject::renameZone (int pieceIndex, int zoneIndex, const std::string& newName)
{
    if (pieceIndex < 0 || pieceIndex >= (int) pieces.size())
        return false;
    auto& p = pieces[(size_t) pieceIndex];
    if (zoneIndex < 0 || zoneIndex >= (int) p.zones.size())
        return false;
    if (newName.empty())
        return false;

    const std::string oldZone = p.zones[(size_t) zoneIndex].name;
    p.zones[(size_t) zoneIndex].name = newName;

    // Update choke targets referencing old zone on this piece
    for (auto& otherP : pieces)
        for (auto& otherZ : otherP.zones)
            for (auto& c : otherZ.chokes)
                if (c.instrument == p.name && c.zone == oldZone)
                    c.zone = newName;

    return true;
}

bool GlorpKitProject::deleteZone (int pieceIndex, int zoneIndex, std::string& outError)
{
    if (pieceIndex < 0 || pieceIndex >= (int) pieces.size())
    {
        outError = "Invalid piece index";
        return false;
    }
    auto& p = pieces[(size_t) pieceIndex];
    if (p.zones.size() <= 1)
    {
        outError = "Cannot delete the only articulation zone of a kit piece";
        return false;
    }
    if (zoneIndex < 0 || zoneIndex >= (int) p.zones.size())
    {
        outError = "Invalid zone index";
        return false;
    }

    p.zones.erase (p.zones.begin() + zoneIndex);
    return true;
}

} // namespace glorp
