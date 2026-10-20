/*
    BleedMatrix.cpp - Implementation of BleedMatrix.
*/

#include "BleedMatrix.h"

namespace glorp
{

BleedPiece pieceForInstrumentName (const std::string& name) noexcept
{
    juce::String lower = juce::String (name).toLowerCase()
                             .replace (" ", "")
                             .replace ("_", "")
                             .replace ("-", "")
                             .trim();

    if (lower.contains ("kick") || lower.contains ("kdrum") || lower.contains ("bassdrum"))
        return BleedPiece::kick;

    if (lower.contains ("snare") || lower.contains ("rimshot") || lower.contains ("rimclick") || lower.contains ("stick"))
        return BleedPiece::snare;

    if (lower.contains ("floortom") || lower.contains ("ftom") || lower.contains ("lotom") || lower.contains ("lowtom"))
        return BleedPiece::floorTom;

    if (lower.contains ("tom1") || lower.contains ("hitom") || lower.contains ("rack1") || lower.contains ("racktom1"))
        return BleedPiece::tom1;

    if (lower.contains ("tom2") || lower.contains ("midtom") || lower.contains ("rack2") || lower.contains ("racktom2"))
        return BleedPiece::tom2;

    if (lower.contains ("tom") || lower.contains ("rack"))
        return BleedPiece::tom1;

    if (lower.contains ("hihat") || lower.contains ("hh") || lower.contains ("pedal") || lower.contains ("chick") || lower.contains ("splash"))
        return BleedPiece::hihat;

    if (lower.contains ("ride"))
        return BleedPiece::ride;

    if (lower.contains ("crash2") || lower.contains ("china"))
        return BleedPiece::crash2;

    if (lower.contains ("crash") || lower.contains ("cymbal"))
        return BleedPiece::crash1;

    return BleedPiece::snare;
}

//==============================================================================
BleedMatrix::BleedMatrix()
{
    resetToNatural();
}

void BleedMatrix::setCellGain (int pieceIdx, int micIdx, float gain) noexcept
{
    if (pieceIdx >= 0 && pieceIdx < maxPieces && micIdx >= 0 && micIdx < maxMics)
        cells[static_cast<size_t> (pieceIdx)][static_cast<size_t> (micIdx)].gain = std::clamp (gain, 0.0f, 2.0f);
}

float BleedMatrix::getCellGain (int pieceIdx, int micIdx) const noexcept
{
    if (pieceIdx >= 0 && pieceIdx < maxPieces && micIdx >= 0 && micIdx < maxMics)
        return cells[static_cast<size_t> (pieceIdx)][static_cast<size_t> (micIdx)].gain;

    return 1.0f;
}

void BleedMatrix::setCellMute (int pieceIdx, int micIdx, bool muted) noexcept
{
    if (pieceIdx >= 0 && pieceIdx < maxPieces && micIdx >= 0 && micIdx < maxMics)
        cells[static_cast<size_t> (pieceIdx)][static_cast<size_t> (micIdx)].muted = muted;
}

bool BleedMatrix::isCellMuted (int pieceIdx, int micIdx) const noexcept
{
    if (pieceIdx >= 0 && pieceIdx < maxPieces && micIdx >= 0 && micIdx < maxMics)
        return cells[static_cast<size_t> (pieceIdx)][static_cast<size_t> (micIdx)].muted;

    return false;
}

float BleedMatrix::getEffectiveGain (int pieceIdx, int micIdx) const noexcept
{
    if (mode == Mode::disabled)
        return 0.0f;

    if (pieceIdx < 0 || pieceIdx >= maxPieces || micIdx < 0 || micIdx >= maxMics)
        return 1.0f * masterBleed;

    const auto& cell = cells[static_cast<size_t> (pieceIdx)][static_cast<size_t> (micIdx)];
    if (cell.muted)
        return 0.0f;

    return cell.gain * masterBleed;
}

void BleedMatrix::setMasterBleed (float level) noexcept
{
    masterBleed = std::clamp (level, 0.0f, 2.5f);
}

void BleedMatrix::setSympatheticResonance (float level) noexcept
{
    sympatheticResonance = std::clamp (level, 0.0f, 1.0f);
}

void BleedMatrix::setMode (Mode m) noexcept
{
    mode = m;
}

//==============================================================================
void BleedMatrix::resetToNatural()
{
    masterBleed = 1.0f;
    sympatheticResonance = 0.35f;
    mode = Mode::hybridAcoustic;

    for (size_t p = 0; p < maxPieces; ++p)
    {
        for (size_t m = 0; m < maxMics; ++m)
        {
            cells[p][m].gain = 1.0f;
            cells[p][m].muted = false;
        }
    }
}

void BleedMatrix::resetToClean()
{
    masterBleed = 0.0f;
    sympatheticResonance = 0.0f;
    mode = Mode::disabled;

    for (size_t p = 0; p < maxPieces; ++p)
        for (size_t m = 0; m < maxMics; ++m)
            cells[p][m].muted = true;
}

void BleedMatrix::resetToLive()
{
    masterBleed = 1.35f;
    sympatheticResonance = 0.60f;
    mode = Mode::hybridAcoustic;

    for (size_t p = 0; p < maxPieces; ++p)
    {
        for (size_t m = 0; m < maxMics; ++m)
        {
            cells[p][m].gain = 1.15f;
            cells[p][m].muted = false;
        }
    }
}

void BleedMatrix::resetToOverheadsOnly (const std::vector<std::string>& micNames)
{
    masterBleed = 1.0f;
    sympatheticResonance = 0.15f;
    mode = Mode::recordedOnly;

    for (size_t p = 0; p < maxPieces; ++p)
    {
        for (size_t m = 0; m < maxMics; ++m)
        {
            bool isOhOrRoom = false;
            if (m < micNames.size())
            {
                juce::String mn = juce::String (micNames[m]).toLowerCase();
                if (mn.contains ("oh") || mn.contains ("overhead") || mn.contains ("room") || mn.contains ("amb"))
                    isOhOrRoom = true;
            }

            cells[p][m].gain = isOhOrRoom ? 1.0f : 0.0f;
            cells[p][m].muted = ! isOhOrRoom;
        }
    }
}

//==============================================================================
void BleedMatrix::writeXml (juce::XmlElement& xml) const
{
    auto* bMat = xml.createNewChildElement ("bleed-matrix");
    bMat->setAttribute ("masterBleed", static_cast<double> (masterBleed));
    bMat->setAttribute ("sympatheticResonance", static_cast<double> (sympatheticResonance));
    bMat->setAttribute ("mode", static_cast<int> (mode));

    for (int p = 0; p < maxPieces; ++p)
    {
        for (int m = 0; m < maxMics; ++m)
        {
            const auto& c = cells[static_cast<size_t> (p)][static_cast<size_t> (m)];
            // Only serialize non-default cells to keep XML compact
            if (std::abs (c.gain - 1.0f) > 0.001f || c.muted)
            {
                auto* cellElem = bMat->createNewChildElement ("cell");
                cellElem->setAttribute ("piece", p);
                cellElem->setAttribute ("mic", m);
                cellElem->setAttribute ("gain", static_cast<double> (c.gain));
                cellElem->setAttribute ("muted", c.muted);
            }
        }
    }
}

void BleedMatrix::readXml (const juce::XmlElement& xml)
{
    auto* bMat = xml.getChildByName ("bleed-matrix");
    if (! bMat)
        return;

    masterBleed = static_cast<float> (bMat->getDoubleAttribute ("masterBleed", 1.0));
    sympatheticResonance = static_cast<float> (bMat->getDoubleAttribute ("sympatheticResonance", 0.35));
    mode = static_cast<Mode> (bMat->getIntAttribute ("mode", static_cast<int> (Mode::hybridAcoustic)));

    for (auto* cellElem : bMat->getChildIterator())
    {
        if (cellElem->hasTagName ("cell"))
        {
            const int p = cellElem->getIntAttribute ("piece", -1);
            const int m = cellElem->getIntAttribute ("mic", -1);
            if (p >= 0 && p < maxPieces && m >= 0 && m < maxMics)
            {
                cells[static_cast<size_t> (p)][static_cast<size_t> (m)].gain =
                    static_cast<float> (cellElem->getDoubleAttribute ("gain", 1.0));
                cells[static_cast<size_t> (p)][static_cast<size_t> (m)].muted =
                    cellElem->getBoolAttribute ("muted", false);
            }
        }
    }
}

} // namespace glorp
