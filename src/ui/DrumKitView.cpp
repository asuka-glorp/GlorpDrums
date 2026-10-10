#include "DrumKitView.h"

#include <cmath>

namespace glorp
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    /// Deterministic wobble for hand-drawn shells, so redraws do not shimmer.
    float hashJitter (int seed, float amount)
    {
        auto h = static_cast<unsigned> (seed * 374761393u + 1442695040u);
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (static_cast<float> (h & 0xFFFF) / 32767.5f - 1.0f) * amount;
    }

    juce::Point<float> polarPoint (juce::Point<float> centre, float angle, float radius)
    {
        return { centre.x + radius * std::sin (angle),
                 centre.y - radius * std::cos (angle) };
    }
}

//==============================================================================
DrumKitView::DrumKitView()
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);
    startTimerHz (60);
}

KitPiece DrumKitView::pieceForName (const juce::String& name)
{
    const auto n = name.toLowerCase();

    if (n.contains ("kick") || n.contains ("bdrum") || n.contains ("bd"))
        return KitPiece::kick;

    if (n.contains ("snare") || n.contains ("sd"))
        return KitPiece::snare;

    if (n.contains ("floor") || n.contains ("ftom"))
        return KitPiece::floorTom;

    if (n.contains ("tom1") || n.contains ("tom 1") || n.contains ("racktom1"))
        return KitPiece::tom1;

    if (n.contains ("tom2") || n.contains ("tom 2") || n.contains ("racktom2"))
        return KitPiece::tom2;

    if (n.contains ("tom3") || n.contains ("tom 3") || n.contains ("racktom3"))
        return KitPiece::tom3;

    if (n.contains ("ride") || n.contains ("bell"))
        return KitPiece::ride;

    if (n.contains ("crash") || n.contains ("china"))
        return KitPiece::crash;

    if (n.contains ("hihat") || n.contains ("hi-hat") || n.contains ("hh")
        || n.contains ("hatclosed") || n.contains ("hatopen"))
        return KitPiece::hihat;

    return KitPiece::count;
}

void DrumKitView::setInstrumentNames (const std::vector<juce::String>& names)
{
    instrumentNames = names;
    repaint();
}

//==============================================================================
void DrumKitView::layout()
{
    if (hasLayout)
        return;

    const auto b = getLocalBounds().toFloat();
    const auto w = b.getWidth();
    const auto h = b.getHeight();

    // Proportions taken from the mockup: kick front and centre, snare to its left,
    // toms arcing up and right, cymbals fanned above.
    auto set = [this] (KitPiece piece, juce::Rectangle<float> r, bool cymbal, juce::Point<float> anchor)
    {
        auto& p = pieces[static_cast<size_t> (piece)];
        p.bounds = r;
        p.anchor = anchor;
        p.isCymbal = cymbal;
    };

    const auto kickW = w * 0.30f;
    const auto kickH = h * 0.34f;

    set (KitPiece::kick,
         juce::Rectangle<float> (w * 0.36f, h * 0.60f, kickW, kickH),
         false,
         { w * 0.36f + kickW * 0.5f, h * 0.60f + kickH * 0.5f });

    set (KitPiece::snare,
         juce::Rectangle<float> (w * 0.17f, h * 0.46f, w * 0.17f, h * 0.14f),
         false,
         { w * 0.17f + w * 0.085f, h * 0.46f });

    set (KitPiece::tom1,
         juce::Rectangle<float> (w * 0.40f, h * 0.36f, w * 0.14f, h * 0.16f),
         false,
         { w * 0.47f, h * 0.36f });

    set (KitPiece::tom2,
         juce::Rectangle<float> (w * 0.52f, h * 0.30f, w * 0.13f, h * 0.15f),
         false,
         { w * 0.585f, h * 0.30f });

    set (KitPiece::tom3,
         juce::Rectangle<float> (w * 0.63f, h * 0.38f, w * 0.15f, h * 0.17f),
         false,
         { w * 0.705f, h * 0.38f });

    set (KitPiece::floorTom,
         juce::Rectangle<float> (w * 0.75f, h * 0.50f, w * 0.16f, h * 0.22f),
         false,
         { w * 0.75f + w * 0.08f, h * 0.50f });

    set (KitPiece::hihat,
         juce::Rectangle<float> (w * 0.06f, h * 0.30f, w * 0.20f, h * 0.07f),
         true,
         { w * 0.06f + w * 0.10f, h * 0.30f });

    set (KitPiece::ride,
         juce::Rectangle<float> (w * 0.60f, h * 0.13f, w * 0.30f, h * 0.09f),
         true,
         { w * 0.75f, h * 0.13f });

    set (KitPiece::crash,
         juce::Rectangle<float> (w * 0.28f, h * 0.16f, w * 0.26f, h * 0.08f),
         true,
         { w * 0.41f, h * 0.16f });

    for (auto& p : pieces)
    {
        p.wobble = 0.0f;
        p.wobbleVelocity = 0.0f;
        p.squash = 0.0f;
        p.flash = 0.0f;
    }

    hasLayout = true;
}

void DrumKitView::resized()
{
    hasLayout = false;
    repaint();
}

juce::Point<float> DrumKitView::anchorFor (KitPiece piece) const
{
    const auto index = static_cast<size_t> (piece);

    if (index >= pieces.size() || piece == KitPiece::count)
        return { -1000.0f, -1000.0f };

    return pieces[index].anchor;
}

void DrumKitView::triggerHit (const juce::String& instrumentName, float velocity)
{
    const auto piece = pieceForName (instrumentName);

    if (piece == KitPiece::count)
        return;

    auto& p = pieces[static_cast<size_t> (piece)];
    const auto v = juce::jlimit (0.0f, 1.0f, velocity);

    p.flash = std::max (p.flash, 0.4f + 0.6f * v);

    if (p.isCymbal)
    {
        // Cymbals ring: kick a damped pendulum rather than animating a canned
        // loop, so the wobble speed varies naturally with velocity.
        p.wobbleVelocity += (0.4f + 3.0f * v);
    }
    else
    {
        p.squash = std::max (p.squash, 0.35f + 0.65f * v);
    }

    repaint();
}

void DrumKitView::timerCallback()
{
    if (! hasLayout)
        return;

    bool anyMoving = false;

    for (auto& p : pieces)
    {
        if (std::abs (p.wobble) > 0.0005f || std::abs (p.wobbleVelocity) > 0.0005f)
        {
            // Damped pendulum: gravity pulls the cymbal back to level.
            p.wobbleVelocity -= p.wobble * 34.0f * (1.0f / 60.0f) * 8.0f;
            p.wobbleVelocity *= 0.982f;
            p.wobble += p.wobbleVelocity * (1.0f / 60.0f);

            if (std::abs (p.wobble) < 0.0005f && std::abs (p.wobbleVelocity) < 0.0005f)
            {
                p.wobble = 0.0f;
                p.wobbleVelocity = 0.0f;
            }

            anyMoving = true;
        }

        if (p.squash > 0.001f)
        {
            p.squash *= 0.86f;
            anyMoving = true;
        }

        if (p.flash > 0.001f)
        {
            p.flash *= 0.82f;
            anyMoving = true;
        }
    }

    if (anyMoving)
        repaint();
}

//==============================================================================
void DrumKitView::drawStand (juce::Graphics& g, juce::Point<float> footCentre,
                             juce::Point<float> top, float spread)
{
    g.setColour (theme.ink.withAlpha (0.85f));

    // Three legs.
    for (int i = -1; i <= 1; ++i)
    {
        const auto foot = footCentre + juce::Point<float> (i * spread, 0.0f);
        g.drawLine (top.x, top.y, foot.x, foot.y, 1.7f);
    }

    // Centre column and a little brace.
    g.drawLine (top.x, top.y, footCentre.x, footCentre.y, 2.4f);

    g.setColour (theme.ink.withAlpha (0.5f));
    g.drawLine (footCentre.x - spread, footCentre.y, footCentre.x + spread, footCentre.y, 1.3f);
}

void DrumKitView::drawKick (juce::Graphics& g, const juce::Rectangle<float>& r)
{
    const auto squashY = 1.0f - 0.06f;
    auto body = r.withHeight (r.getHeight() * squashY);
    body = body.withY (r.getCentreY() - body.getHeight() * 0.5f);

    // Shell: a cylinder seen slightly from the side.
    juce::Path shell;
    const auto ellipseH = body.getHeight() * 0.26f;

    shell.startNewSubPath (body.getX(), body.getY() + ellipseH * 0.5f);
    shell.lineTo (body.getX(), body.getBottom() - ellipseH * 0.5f);
    shell.addArc (body.getX(), body.getBottom() - ellipseH,
                  body.getWidth(), ellipseH,
                  0.0f, juce::MathConstants<float>::pi, false);
    shell.lineTo (body.getRight(), body.getY() + ellipseH * 0.5f);
    shell.closeSubPath();

    g.setColour (theme.shell);
    g.fillPath (shell);
    g.setColour (theme.ink);
    g.strokePath (shell, juce::PathStrokeType (2.0f));

    // Front head.
    juce::Path head;
    head.addEllipse (juce::Rectangle<float> (body.getWidth() * 0.86f, body.getHeight() * 0.80f)
                        .withCentre (body.getCentre()));

    g.setColour (theme.head);
    g.fillPath (head);
    g.setColour (theme.ink);
    g.strokePath (head, juce::PathStrokeType (2.0f));

    // Port hole, like the mockup's bass drum.
    g.setColour (theme.ink.withAlpha (0.8f));
    g.fillEllipse (body.getCentreX() + body.getWidth() * 0.16f,
                   body.getCentreY() + body.getHeight() * 0.12f,
                   body.getWidth() * 0.10f, body.getWidth() * 0.10f);

    // Shading on the right-hand edge.
    g.setColour (theme.shellShade.withAlpha (0.7f));
    g.fillRect (body.getRight() - body.getWidth() * 0.12f, body.getY() + ellipseH * 0.5f,
                body.getWidth() * 0.12f, body.getHeight() - ellipseH);
}

void DrumKitView::drawTom (juce::Graphics& g, const juce::Rectangle<float>& r, bool rackMounted)
{
    const auto squash = 1.0f - 0.05f;
    auto body = r.withHeight (r.getHeight() * squash);
    body = body.withY (r.getCentreY() - body.getHeight() * 0.5f);

    const auto ellipseH = body.getHeight() * 0.30f;

    juce::Path shell;
    shell.startNewSubPath (body.getX(), body.getY() + ellipseH * 0.5f);
    shell.lineTo (body.getX(), body.getBottom() - ellipseH * 0.5f);
    shell.addArc (body.getX(), body.getBottom() - ellipseH,
                  body.getWidth(), ellipseH,
                  0.0f, juce::MathConstants<float>::pi, false);
    shell.lineTo (body.getRight(), body.getY() + ellipseH * 0.5f);
    shell.closeSubPath();

    g.setColour (theme.shell);
    g.fillPath (shell);
    g.setColour (theme.ink);
    g.strokePath (shell, juce::PathStrokeType (1.9f));

    // Top head.
    juce::Path top;
    top.addEllipse (juce::Rectangle<float> (body.getX(), body.getY(),
                                             body.getWidth(), ellipseH));

    g.setColour (theme.head);
    g.fillPath (top);
    g.setColour (theme.ink);
    g.strokePath (top, juce::PathStrokeType (1.9f));

    // Lugs.
    g.setColour (theme.ink.withAlpha (0.75f));

    for (int i = 0; i < 3; ++i)
    {
        const auto x = body.getX() + body.getWidth() * (0.2f + 0.3f * i);
        g.drawLine (x, body.getY() + ellipseH * 1.1f, x, body.getBottom() - ellipseH * 0.9f, 1.2f);
    }

    // A rack tom hangs off a mount; a floor tom stands on legs.
    if (rackMounted)
        g.drawLine (body.getCentreX(), body.getY() + ellipseH,
                    body.getCentreX(), body.getY() - r.getHeight() * 0.35f, 2.0f);
    else
        for (int i = -1; i <= 1; ++i)
            g.drawLine (body.getCentreX(), body.getBottom() - ellipseH * 0.3f,
                        body.getCentreX() + i * body.getWidth() * 0.36f,
                        body.getBottom() + r.getHeight() * 0.30f, 1.6f);
}

void DrumKitView::drawSnare (juce::Graphics& g, const juce::Rectangle<float>& r)
{
    drawTom (g, r, false);

    // Snares have wires under the bottom head and a throw-off on the side.
    const auto body = r;
    g.setColour (theme.ink.withAlpha (0.55f));

    for (int i = 0; i < 5; ++i)
    {
        const auto x = body.getX() + body.getWidth() * (0.2f + 0.15f * i);
        g.drawLine (x, body.getBottom() - body.getHeight() * 0.18f,
                    x, body.getBottom() - body.getHeight() * 0.04f, 1.0f);
    }
}

void DrumKitView::drawCymbal (juce::Graphics& g, const juce::Rectangle<float>& r, float wobble)
{
    // A cymbal is an ellipse seen at a shallow angle; the wobble tilts it.
    const auto centre = r.getCentre();
    const auto rx = r.getWidth() * 0.5f;
    const auto ry = r.getHeight() * 0.5f;

    juce::AffineTransform transform =
        juce::AffineTransform::rotation (wobble, centre.x, centre.y)
            .scaled (1.0f + wobble * 0.15f, 1.0f - std::abs (wobble) * 0.35f, centre.x, centre.y);

    juce::Path cymbal;
    cymbal.addEllipse (r);
    cymbal.applyTransform (transform);

    g.setColour (theme.cymbal);
    g.fillPath (cymbal);
    g.setColour (theme.ink);
    g.strokePath (cymbal, juce::PathStrokeType (2.0f));

    // Bell in the middle.
    juce::Path bell;
    bell.addEllipse (r.reduced (rx * 0.82f, ry * 0.55f));
    bell.applyTransform (transform);

    g.setColour (theme.cymbal.withAlpha (0.9f));
    g.fillPath (bell);
    g.strokePath (bell, juce::PathStrokeType (1.4f));
}

void DrumKitView::drawImpact (juce::Graphics& g, const juce::Rectangle<float>& r, float amount)
{
    if (amount <= 0.01f)
        return;

    g.setColour (theme.ink.withAlpha (juce::jlimit (0.0f, 0.9f, amount)));

    const auto centre = r.getCentre();
    const auto radius = juce::jmax (r.getWidth(), r.getHeight()) * (0.62f + (1.0f - amount) * 0.30f);

    // Radial strokes that pop outward and fade, as in the mockup.
    constexpr int numStrokes = 7;

    for (int i = 0; i < numStrokes; ++i)
    {
        const auto angle = static_cast<float> (pi * (0.15 + 0.70 * i / static_cast<double> (numStrokes - 1)));
        const auto inner = polarPoint (centre, angle, radius * 0.92f);
        const auto outer = polarPoint (centre, angle, radius * (1.05f + amount * 0.16f));
        g.drawLine (juce::Line<float> (inner, outer), 1.8f);
    }
}

//==============================================================================
void DrumKitView::paint (juce::Graphics& g)
{
    layout();

    // Stands first, so the shells sit in front of them.
    const auto& kick = pieces[static_cast<size_t> (KitPiece::kick)].bounds;
    drawStand (g, { kick.getCentreX(), kick.getBottom() + 4.0f },
               { kick.getCentreX(), kick.getCentreY() }, kick.getWidth() * 0.30f);

    const auto& hihat = pieces[static_cast<size_t> (KitPiece::hihat)];
    drawStand (g, { hihat.bounds.getCentreX(), getHeight() * 0.78f }, hihat.anchor, hihat.bounds.getWidth() * 0.22f);

    const auto& ride = pieces[static_cast<size_t> (KitPiece::ride)];
    drawStand (g, { ride.bounds.getCentreX(), getHeight() * 0.80f }, ride.anchor, ride.bounds.getWidth() * 0.16f);

    const auto& crash = pieces[static_cast<size_t> (KitPiece::crash)];
    drawStand (g, { crash.bounds.getCentreX() - crash.bounds.getWidth() * 0.3f, getHeight() * 0.78f },
               crash.anchor, crash.bounds.getWidth() * 0.16f);

    // Drums.
    for (auto piece : { KitPiece::kick, KitPiece::snare, KitPiece::tom1,
                        KitPiece::tom2, KitPiece::tom3, KitPiece::floorTom })
    {
        auto& p = pieces[static_cast<size_t> (piece)];

        const auto scale = 1.0f + p.squash * 0.10f;
        auto r = p.bounds;

        if (std::abs (scale - 1.0f) > 0.001f)
            r = r.reduced (-r.getWidth() * (scale - 1.0f) * 0.5f,
                           -r.getHeight() * (scale - 1.0f) * 0.5f);

        switch (piece)
        {
            case KitPiece::kick:    drawKick (g, r); break;
            case KitPiece::snare:   drawSnare (g, r); break;
            case KitPiece::floorTom: drawTom (g, r, false); break;
            default:                drawTom (g, r, true); break;
        }

        drawImpact (g, r, p.flash);
    }

    // Cymbals on top of everything.
    for (auto piece : { KitPiece::hihat, KitPiece::ride, KitPiece::crash })
    {
        auto& p = pieces[static_cast<size_t> (piece)];
        drawCymbal (g, p.bounds, p.wobble);
        drawImpact (g, p.bounds, p.flash * 0.8f);
    }
}

void DrumKitView::mouseUp (const juce::MouseEvent& e)
{
    for (auto piece : { KitPiece::kick, KitPiece::snare, KitPiece::tom1, KitPiece::tom2,
                        KitPiece::tom3, KitPiece::floorTom, KitPiece::hihat,
                        KitPiece::ride, KitPiece::crash })
    {
        const auto& p = pieces[static_cast<size_t> (piece)];

        if (p.bounds.expanded (4.0f).contains (e.position) && onPieceClicked != nullptr)
        {
            onPieceClicked (piece);
            return;
        }
    }
}

} // namespace glorp
