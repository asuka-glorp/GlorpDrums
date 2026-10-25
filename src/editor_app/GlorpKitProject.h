/*
    GlorpKitProject.h - Editable in-memory project model for the standalone GlorpKitEditor app.
    Supports creating new kits, opening & editing existing .glorpkit (v1.0 & v2.0) archives,
    importing DrumGizmo drumkit.xml folders, managing custom PNG assets, 2D canvas positions,
    Z-order layering, articulation zones, velocity layers, round-robins, and exporting
    uncompressed (ZIP_STORED) .glorpkit v2.0 packages.
*/

#pragma once

#include "../core/DrumKit.h"
#include "../core/GlorpKit.h"
#include "../core/GlorpKitWriter.h"

#include <cstdint>
#include <string>
#include <vector>

namespace glorp
{

struct EditableMicSample
{
    std::string mic = "close";
    std::string fileName;
    std::string sourceDiskPath;
    std::string sourceArchivePath;
    int64_t archiveOffset = -1;
    int64_t archiveLength = -1;
    int fileChannel = 0; // 0-based
};

struct EditableHit
{
    std::string name;
    float power = 1.0f;
    int velocityLayerIndex = -1;
    int roundRobinIndex = -1;
    std::vector<EditableMicSample> micSamples;
};

struct EditableZone
{
    std::string name = "head";
    std::string graphicId;
    int note = 36;
    int altNote = -1;
    int numVelocityLayers = 5;
    int numRoundRobins = 4;
    std::vector<GlorpChokeTarget> chokes;
    std::vector<EditableHit> hits;
};

struct EditablePiece
{
    std::string name;
    std::string id;
    std::string graphicId = "snare"; // e.g. "kick", "snare", "custom_snare"
    std::string customPngFileName;
    std::vector<uint8_t> customPngBytes;
    bool hasCustomLayout = false;
    int outputBus = 0;
    std::string chokeGroup = "none";

    float normX = 0.0f;
    float normY = 0.0f;
    float normW = 160.0f;
    int zOrder = 1;
    bool visible = true;

    std::vector<EditableZone> zones;
};

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
    int defaultMidiNote;
};

const DefaultPieceLayout* findDefaultPieceLayout (const std::string& idOrName);
std::string sanitizeId (const std::string& raw);

struct ParsedSampleInfo
{
    std::string rawPath;
    std::string fileName;
    std::string instrument;
    std::string zone;
    int roundRobin = 0;        // 0-indexed: 0=A, 1=B, etc.
    int velocityLevel = 1;     // 1-indexed (e.g. 1..13)
    bool hasExplicitVelocity = false;
    bool hasExplicitRoundRobin = false;
    bool hasExplicitZone = false;
};

ParsedSampleInfo parseSampleFilename (const std::string& filenameOrPath);

class GlorpKitProject
{
public:
    GlorpKitProject();

    // -- Kit-wide Metadata & Visual Settings ---------------------------------
    std::string title = "Custom Birch Studio";
    std::string author = "GlorpDrums User";
    std::string version = "2.0";
    std::string license = "CC-BY-4.0";
    std::string description = "Custom drumkit created in GlorpKit Studio";
    std::string hitGlowColor = "#ff8200"; // Kit-wide hit glow hex color
    int sampleRate = 48000;
    std::string loadedFilePath;

    std::vector<GlorpMic> mics;
    std::vector<EditablePiece> pieces;

    // -- Project Operations --------------------------------------------------
    void createNewDefaultKit();
    void createNewBlankKit();

    std::vector<EditablePiece>& getPieces() noexcept { return pieces; }
    const std::vector<EditablePiece>& getPieces() const noexcept { return pieces; }

    const std::string& getTitle() const noexcept { return title; }
    void setTitle (const std::string& t) { title = t; }

    const std::string& getHitGlowColor() const noexcept { return hitGlowColor; }
    void setHitGlowColor (const std::string& c) { hitGlowColor = c; }

    double getSampleRate() const noexcept { return (double) sampleRate; }
    void setSampleRate (double sr) noexcept { sampleRate = (int) sr; }

    void addChannel (const std::string& micName)
    {
        GlorpMic m;
        m.name = micName;
        m.title = micName;
        mics.push_back (std::move (m));
    }

    int addPiece (const std::string& pieceName, const std::string& graphic = "snare")
    {
        EditablePiece p;
        p.name = pieceName;
        p.id = "piece_" + std::to_string (pieces.size() + 1);
        p.graphicId = graphic;
        p.normX = -90.0f;
        p.normY = -280.0f;
        p.normW = 180.0f;
        p.zOrder = (int) pieces.size() + 1;
        EditableZone z;
        z.name = "head";
        z.note = 60 + (int) pieces.size();
        EditableHit h;
        h.name = p.id + "_head_v1";
        h.power = 0.85f;
        h.velocityLayerIndex = 4;
        h.roundRobinIndex = 0;
        EditableMicSample ms;
        ms.mic = "close";
        ms.fileName = h.name + "_close.wav";
        h.micSamples.push_back (std::move (ms));
        z.hits.push_back (std::move (h));
        p.zones.push_back (std::move (z));
        pieces.push_back (std::move (p));
        normalizeZOrders();
        return (int) pieces.size() - 1;
    }

    int duplicatePiece (int idx)
    {
        if (idx < 0 || idx >= (int) pieces.size()) return -1;
        EditablePiece copy = pieces[(size_t) idx];
        copy.name += " Copy";
        copy.id   += "_copy";
        copy.normX += 20.0f;
        copy.normY += 15.0f;
        copy.zOrder = (int) pieces.size() + 1;
        pieces.push_back (std::move (copy));
        normalizeZOrders();
        return (int) pieces.size() - 1;
    }

    bool deletePiece (int idx)
    {
        if (idx >= 0 && idx < (int) pieces.size())
        {
            pieces.erase (pieces.begin() + idx);
            normalizeZOrders();
            return true;
        }
        return false;
    }

    void removePiece (int idx)
    {
        deletePiece (idx);
    }

    int addPieceWithAutoLayout (const std::string& nameOrStem,
                                const std::string& graphic = "",
                                const std::vector<uint8_t>& pngBytes = {});

    bool openGlorpKitArchive (const std::string& archivePath, std::string& outError);
    bool importDrumGizmoXml (const std::string& drumkitXmlPath, std::string& outError);
    bool exportToGlorpKit (const std::string& outputArchivePath, std::string& outError);
    bool relinkSamplesFromDirectory (const std::string& directoryPath);

    /// Builds a playable DrumKit in memory so the editor's Sampler can audition hits live.
    DrumKit buildRuntimeDrumKit() const;
    int getSamplerIndexForZone (int pieceIndex, int zoneIndex) const;

    // -- Piece, Mic & Zone Management ----------------------------------------
    bool renamePiece (int pieceIndex, const std::string& newName, const std::string& newId = "");
    bool renameMic (int micIndex, const std::string& newName);
    bool deleteMic (int micIndex, std::string& outError);
    bool deleteMic (int micIndex) { std::string err; return deleteMic (micIndex, err); }
    bool renameZone (int pieceIndex, int zoneIndex, const std::string& newName);
    bool deleteZone (int pieceIndex, int zoneIndex, std::string& outError);
    bool deleteZone (int pieceIndex, int zoneIndex) { std::string err; return deleteZone (pieceIndex, zoneIndex, err); }

    // -- High-level Mic Routing ----------------------------------------------
    bool setPieceMicrophone (int pieceIndex, const std::string& micName, const std::string& oldMicToReplace = "");
    bool setZoneMicrophone (int pieceIndex, int zoneIndex, const std::string& micName, const std::string& oldMicToReplace = "");
    std::string getPieceDominantMicrophone (int pieceIndex) const;
    std::string getZoneDominantMicrophone (int pieceIndex, int zoneIndex) const;

    // -- Z-Order & Piece Helpers ---------------------------------------------
    void normalizeZOrders();
    bool movePieceZOrder (int pieceIndex, int deltaZ);
    bool bringPieceToFront (int pieceIndex);
    bool sendPieceToBack (int pieceIndex);

    /// Automatically calculates sample hit power values from audio RMS/Peak energy
    /// and organizes the zone's hits into sorted velocity layers and round-robins.
    void autoComputeZonePowers (int pieceIndex, int zoneIndex);

    /// Recomputes velocityLayerIndex and roundRobinIndex for a zone based on hit powers.
    static void rebuildZoneLayerGrid (EditableZone& zone, int maxRoundRobins = 3);

    /// Imports one or more WAV/FLAC/OGG files from disk into a piece's zone.
    int importAudioFilesIntoZone (int pieceIndex, int zoneIndex,
                                  const std::vector<std::string>& filePaths,
                                  const std::string& targetMic = "close");

    /// Moves a sample hit to a specific velocity layer and round robin column.
    bool moveSampleToCell (int pieceIndex, int zoneIndex, int hitIndex, int newVelocityLayer, int newRoundRobin);

    /// Swaps the matrix cells of two sample hits.
    bool swapSampleCells (int pieceIndex, int zoneIndex, int hitAIndex, int hitBIndex);

    /// Sets the velocity layer and round robin dimensions of a zone.
    void setZoneGridDimensions (int pieceIndex, int zoneIndex, int numVelLayers, int numRoundRobins);
};

} // namespace glorp
