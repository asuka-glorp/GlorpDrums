/*
    BleedMatrix.h - Multi-mic acoustic leakage and bleed matrix for GlorpDrums.
*/

#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace glorp
{

enum class BleedPiece
{
    kick = 0,
    snare,
    tom1,
    tom2,
    floorTom,
    hihat,
    ride,
    crash1,
    crash2,
    count
};

inline const char* bleedPieceIdentifier (BleedPiece piece) noexcept
{
    switch (piece)
    {
        case BleedPiece::kick:     return "kick";
        case BleedPiece::snare:    return "snare";
        case BleedPiece::tom1:     return "tom1";
        case BleedPiece::tom2:     return "tom2";
        case BleedPiece::floorTom: return "floorTom";
        case BleedPiece::hihat:    return "hihat";
        case BleedPiece::ride:     return "ride";
        case BleedPiece::crash1:   return "crash1";
        case BleedPiece::crash2:   return "crash2";
        case BleedPiece::count:
        default:                   return "unknown";
    }
}

inline const char* bleedPieceDisplayName (BleedPiece piece) noexcept
{
    switch (piece)
    {
        case BleedPiece::kick:     return "Kick";
        case BleedPiece::snare:    return "Snare";
        case BleedPiece::tom1:     return "Tom 1";
        case BleedPiece::tom2:     return "Tom 2";
        case BleedPiece::floorTom: return "Floor Tom";
        case BleedPiece::hihat:    return "Hi-Hat";
        case BleedPiece::ride:     return "Ride";
        case BleedPiece::crash1:   return "Crash 1";
        case BleedPiece::crash2:   return "Crash 2";
        case BleedPiece::count:
        default:                   return "Piece";
    }
}

BleedPiece pieceForInstrumentName (const std::string& name) noexcept;

struct BleedCell
{
    float gain = 1.0f;     ///< Linear gain factor (1.0 = 0 dB, 0.0 = -inf, 2.0 = +6 dB)
    bool muted = false;    ///< Explicit cell mute
};

class BleedMatrix
{
public:
    static constexpr int maxPieces = static_cast<int> (BleedPiece::count);
    static constexpr int maxMics = 32;

    enum class Mode
    {
        recordedOnly = 0,    ///< Only adjust recorded multi-mic bleed channels
        hybridAcoustic = 1,  ///< Adjust recorded bleed + simulate acoustic crosstalk for dry mics
        disabled = 2         ///< Mute all bleed across the entire drum kit
    };

    BleedMatrix();

    void setCellGain (int pieceIdx, int micIdx, float gain) noexcept;
    float getCellGain (int pieceIdx, int micIdx) const noexcept;

    void setCellMute (int pieceIdx, int micIdx, bool muted) noexcept;
    bool isCellMuted (int pieceIdx, int micIdx) const noexcept;

    /// Returns the active linear gain for the given piece into the destination mic.
    /// Returns 0.0f if the matrix mode is disabled or if the cell is muted.
    float getEffectiveGain (int pieceIdx, int micIdx) const noexcept;

    void setMasterBleed (float level) noexcept;
    float getMasterBleed() const noexcept { return masterBleed; }

    void setSympatheticResonance (float level) noexcept;
    float getSympatheticResonance() const noexcept { return sympatheticResonance; }

    void setMode (Mode m) noexcept;
    Mode getMode() const noexcept { return mode; }

    void resetToNatural();
    void resetToClean();
    void resetToLive();
    void resetToOverheadsOnly (const std::vector<std::string>& micNames);

    void writeXml (juce::XmlElement& xml) const;
    void readXml (const juce::XmlElement& xml);

private:
    std::array<std::array<BleedCell, maxMics>, maxPieces> cells;
    float masterBleed = 1.0f;
    float sympatheticResonance = 0.35f;
    Mode mode = Mode::hybridAcoustic;
};

} // namespace glorp
