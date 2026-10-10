/*
    DrumKitView.h - the procedural hand-drawn comic drum kit view.
*/

#pragma once

#include "Theme.h"

#include <array>
#include <functional>
#include <vector>

namespace glorp
{

enum class KitPiece
{
    kick, snare, tom1, tom2, tom3, floorTom, hihat, ride, crash, count
};

class DrumKitView : public juce::Component,
                    private juce::Timer
{
public:
    DrumKitView();

    void setInstrumentNames (const std::vector<juce::String>& names);

    /// Fires an impact on the piece that best matches instrumentName.
    void triggerHit (const juce::String& instrumentName, float velocity);
    void triggerHitForPiece (KitPiece piece, float velocity);

    /// Anchor point in this component's coordinates for callout leader lines.
    juce::Point<float> anchorFor (KitPiece piece) const;

    /// Which piece an instrument name most likely refers to.
    static KitPiece pieceForName (const juce::String& name);

    /// Fired when a piece is clicked to trigger sound and selection.
    std::function<void (KitPiece, float velocity)> onPieceHit;
    std::function<void (KitPiece)> onPieceClicked;

    KitPiece pieceAt (juce::Point<float> pos) const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct Piece
    {
        juce::Rectangle<float> bounds;
        juce::Point<float> anchor;
        float wobble = 0.0f;
        float wobbleVelocity = 0.0f;
        float squash = 0.0f;
        float flash = 0.0f;
        bool isCymbal = false;
    };

    void drawFloorShadow (juce::Graphics& g, float cx, float cy, float w, float h);
    void drawKick (juce::Graphics&, const juce::Rectangle<float>&, float pulse);
    void drawTom (juce::Graphics&, const juce::Rectangle<float>&, bool rackMounted, float angle, float pulse);
    void drawSnare (juce::Graphics&, const juce::Rectangle<float>&, float pulse);
    void drawCymbal (juce::Graphics&, const juce::Rectangle<float>&, float wobble, float tiltAngle);
    void drawHiHat (juce::Graphics&, const juce::Rectangle<float>&, float wobble);
    void drawCymbalStand (juce::Graphics&, juce::Point<float> base, juce::Point<float> top, bool isBoom);
    void drawSnareStand (juce::Graphics&, juce::Point<float> base, juce::Point<float> basket);
    void drawImpact (juce::Graphics&, const juce::Rectangle<float>&, float amount);
    void drawCrossHatch (juce::Graphics& g, const juce::Rectangle<float>& r, float density);

    void layout();

    std::array<Piece, static_cast<size_t> (KitPiece::count)> pieces;
    std::vector<juce::String> instrumentNames;

    KitPiece hoveredPiece = KitPiece::count;
    Theme theme = Theme::sketch();
    bool hasLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitView)
};

} // namespace glorp
