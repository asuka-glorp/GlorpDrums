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
    int velocityLayerIndex = 0;
    int roundRobinIndex = 0;
    std::vector<EditableMicSample> micSamples;
};

struct EditableZone
{
    std::string name = "head";
    std::string graphicId;
    int note = 36;
    int altNote = -1;
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
    int outputBus = 0;
    std::string chokeGroup = "none";

    float normX = 0.0f;
    float normY = 0.0f;
    float normW = 160.0f;
    int zOrder = 1;
    bool visible = true;

    std::vector<EditableZone> zones;
};

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
        p.normX = 0.50f;
        p.normY = 0.50f;
        p.normW = 150.0f;
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
        copy.normX = std::min (0.90f, copy.normX + 0.05f);
        copy.normY = std::min (0.90f, copy.normY + 0.04f);
        copy.zOrder = (int) pieces.size() + 1;
        pieces.push_back (std::move (copy));
        normalizeZOrders();
        return (int) pieces.size() - 1;
    }

    void removePiece (int idx)
    {
        if (idx >= 0 && idx < (int) pieces.size() && pieces.size() > 1)
        {
            pieces.erase (pieces.begin() + idx);
            normalizeZOrders();
        }
    }

    bool openGlorpKitArchive (const std::string& archivePath, std::string& outError);
    bool importDrumGizmoXml (const std::string& drumkitXmlPath, std::string& outError);
    bool exportToGlorpKit (const std::string& outputArchivePath, std::string& outError) const;

    /// Builds a playable DrumKit in memory so the editor's Sampler can audition hits live.
    DrumKit buildRuntimeDrumKit() const;
    int getSamplerIndexForZone (int pieceIndex, int zoneIndex) const;

    // -- Piece, Mic & Zone Management ----------------------------------------
    bool renamePiece (int pieceIndex, const std::string& newName, const std::string& newId = "");
    bool renameMic (int micIndex, const std::string& newName);
    bool renameZone (int pieceIndex, int zoneIndex, const std::string& newName);
    bool deleteZone (int pieceIndex, int zoneIndex, std::string& outError);
    bool deleteZone (int pieceIndex, int zoneIndex) { std::string err; return deleteZone (pieceIndex, zoneIndex, err); }

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
};

} // namespace glorp
