/*
    DrumKitView.h - the kit itself, drawn procedurally.

    There is no drum artwork to ship, so the kit is built out of juce::Paths at
    paint time: kick, snare, two rack toms, a floor tom, hi-hat, ride and crash,
    plus stands. Each piece exposes an anchor point so a CalloutBox can draw its
    dashed leader line to the right spot, and an impact animation so hitting it
    does something.

    Animations are driven from hits drained off the sampler's lock-free ring, so
    the audio thread never touches a Component.
*/

#pragma once

#include "Theme.h"

#include <array>
#include <functional>
#include <vector>

namespace glorp
{

/// Identifies a piece of the drawn kit. Chosen to match DrumGizmo's usual
/// instrument names so a callout can be matched to its shape.
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

    /// Fires an impact on the piece that best matches `instrumentName`.
    void triggerHit (const juce::String& instrumentName, float velocity);

    /// Centre of a piece, in this component's coordinates. Invalid pieces return
    /// a point far off screen.
    juce::Point<float> anchorFor (KitPiece piece) const;

    /// Which piece an instrument name most likely refers to.
    static KitPiece pieceForName (const juce::String& name);

    std::function<void (KitPiece)> onPieceClicked;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Piece
    {
        juce::Rectangle<float> bounds;
        juce::Point<float> anchor;
        float wobble = 0.0f;       ///< cymbal swing, radians
        float wobbleVelocity = 0.0f;
        float squash = 0.0f;       ///< drum head scale pulse
        float flash = 0.0f;        ///< impact strokes
        bool isCymbal = false;
    };

    void drawKick (juce::Graphics&, const juce::Rectangle<float>&);
    void drawTom (juce::Graphics&, const juce::Rectangle<float>&, bool);
    void drawSnare (juce::Graphics&, const juce::Rectangle<float>&);
    void drawCymbal (juce::Graphics&, const juce::Rectangle<float>&, float wobble);
    void drawStand (juce::Graphics&, juce::Point<float> footCentre, juce::Point<float> top, float spread);
    void drawImpact (juce::Graphics&, const juce::Rectangle<float>&, float amount);
    void layout();

    std::array<Piece, static_cast<size_t> (KitPiece::count)> pieces;
    std::vector<juce::String> instrumentNames;

    Theme theme = Theme::sketch();
    bool hasLayout = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumKitView)
};

} // namespace glorp
