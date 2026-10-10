#include "DrumKitView.h"

#include <cmath>

namespace glorp
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

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
    setInterceptsMouseClicks (true, true);
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

    if (n.contains ("tom1") || n.contains ("tom 1") || n.contains ("racktom1") || n.contains ("hightom"))
        return KitPiece::tom1;

    if (n.contains ("tom2") || n.contains ("tom 2") || n.contains ("racktom2") || n.contains ("midtom"))
        return KitPiece::tom2;

    if (n.contains ("tom3") || n.contains ("tom 3") || n.contains ("lowtom"))
        return KitPiece::tom3;

    if (n.contains ("ride") || n.contains ("bell"))
        return KitPiece::ride;

    if (n.contains ("crash") || n.contains ("china") || n.contains ("splash"))
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

    auto set = [this] (KitPiece piece, juce::Rectangle<float> r, bool cymbal, juce::Point<float> anchor)
    {
        auto& p = pieces[static_cast<size_t> (piece)];
        p.bounds = r;
        p.anchor = anchor;
        p.isCymbal = cymbal;
    };

    // Layout matching the mockup in Image 2:
    // Bass Drum in center, slightly angled.
    const auto kickW = w * 0.25f;
    const auto kickH = h * 0.32f;
    const auto kickX = w * 0.43f;
    const auto kickY = h * 0.44f;
    set (KitPiece::kick,
         juce::Rectangle<float> (kickX, kickY, kickW, kickH),
         false,
         { kickX + kickW * 0.45f, kickY + kickH * 0.55f });

    // Snare Drum (to the left of the kick, mounted on stand)
    const auto snareW = w * 0.14f;
    const auto snareH = h * 0.13f;
    const auto snareX = w * 0.30f;
    const auto snareY = h * 0.43f;
    set (KitPiece::snare,
         juce::Rectangle<float> (snareX, snareY, snareW, snareH),
         false,
         { snareX + snareW * 0.5f, snareY + snareH * 0.35f });

    // Rack Tom 1 (High Tom - mounted on top left of kick)
    const auto tom1W = w * 0.11f;
    const auto tom1H = h * 0.13f;
    const auto tom1X = w * 0.39f;
    const auto tom1Y = h * 0.34f;
    set (KitPiece::tom1,
         juce::Rectangle<float> (tom1X, tom1Y, tom1W, tom1H),
         false,
         { tom1X + tom1W * 0.5f, tom1Y + tom1H * 0.4f });

    // Rack Tom 2 (Mid Tom - mounted on top right of kick)
    const auto tom2W = w * 0.11f;
    const auto tom2H = h * 0.13f;
    const auto tom2X = w * 0.52f;
    const auto tom2Y = h * 0.35f;
    set (KitPiece::tom2,
         juce::Rectangle<float> (tom2X, tom2Y, tom2W, tom2H),
         false,
         { tom2X + tom2W * 0.5f, tom2Y + tom2H * 0.4f });

    // Rack Tom 3 (Low Tom / auxiliary tom)
    set (KitPiece::tom3,
         juce::Rectangle<float> (w * 0.58f, h * 0.37f, w * 0.11f, h * 0.13f),
         false,
         { w * 0.63f, h * 0.42f });

    // Floor Tom (to the right of the kick on 3 legs)
    const auto ftomW = w * 0.15f;
    const auto ftomH = h * 0.18f;
    const auto ftomX = w * 0.63f;
    const auto ftomY = h * 0.44f;
    set (KitPiece::floorTom,
         juce::Rectangle<float> (ftomX, ftomY, ftomW, ftomH),
         false,
         { ftomX + ftomW * 0.5f, ftomY + ftomH * 0.35f });

    // Crash Cymbal (tilted high left on boom stand)
    const auto crashW = w * 0.18f;
    const auto crashH = h * 0.08f;
    const auto crashX = w * 0.25f;
    const auto crashY = h * 0.26f;
    set (KitPiece::crash,
         juce::Rectangle<float> (crashX, crashY, crashW, crashH),
         true,
         { crashX + crashW * 0.5f, crashY + crashH * 0.5f });

    // Hi-Hat Cymbals (to the right of kick / above snare on pedal stand)
    const auto hhW = w * 0.14f;
    const auto hhH = h * 0.055f;
    const auto hhX = w * 0.66f;
    const auto hhY = h * 0.37f;
    set (KitPiece::hihat,
         juce::Rectangle<float> (hhX, hhY, hhW, hhH),
         true,
         { hhX + hhW * 0.5f, hhY + hhH * 0.5f });

    // Ride Cymbal (tilted high right on boom stand)
    const auto rideW = w * 0.20f;
    const auto rideH = h * 0.085f;
    const auto rideX = w * 0.61f;
    const auto rideY = h * 0.24f;
    set (KitPiece::ride,
         juce::Rectangle<float> (rideX, rideY, rideW, rideH),
         true,
         { rideX + rideW * 0.5f, rideY + rideH * 0.5f });

    hasLayout = true;
}

void DrumKitView::resized()
{
    hasLayout = false;
    repaint();
}

juce::Point<float> DrumKitView::anchorFor (KitPiece piece) const
{
    const auto idx = static_cast<size_t> (piece);
    if (idx < pieces.size())
        return pieces[idx].anchor;

    return { -100.0f, -100.0f };
}

void DrumKitView::triggerHit (const juce::String& instrumentName, float velocity)
{
    const auto piece = pieceForName (instrumentName);
    if (piece != KitPiece::count)
        triggerHitForPiece (piece, velocity);
}

void DrumKitView::triggerHitForPiece (KitPiece piece, float velocity)
{
    const auto idx = static_cast<size_t> (piece);
    if (idx >= pieces.size())
        return;

    auto& p = pieces[idx];
    p.flash = std::clamp (velocity, 0.4f, 1.0f);
    p.squash = std::clamp (velocity * 0.22f, 0.05f, 0.25f);

    if (p.isCymbal)
    {
        p.wobbleVelocity += (velocity * 0.16f);
        p.wobble = std::clamp (p.wobble + p.wobbleVelocity, -0.25f, 0.25f);
    }

    repaint();
}

void DrumKitView::timerCallback()
{
    bool needsRepaint = false;

    for (auto& p : pieces)
    {
        if (p.flash > 0.01f)
        {
            p.flash *= 0.82f;
            needsRepaint = true;
        }
        else
        {
            p.flash = 0.0f;
        }

        if (p.squash > 0.005f)
        {
            p.squash *= 0.78f;
            needsRepaint = true;
        }
        else
        {
            p.squash = 0.0f;
        }

        if (p.isCymbal && (std::abs (p.wobble) > 0.002f || std::abs (p.wobbleVelocity) > 0.002f))
        {
            constexpr float springK = 0.28f;
            constexpr float damping = 0.84f;

            p.wobbleVelocity = (p.wobbleVelocity - p.wobble * springK) * damping;
            p.wobble += p.wobbleVelocity;
            needsRepaint = true;
        }
    }

    if (needsRepaint)
        repaint();
}

KitPiece DrumKitView::pieceAt (juce::Point<float> pos) const
{
    // Check in reverse order so foreground pieces take priority
    const KitPiece order[] = {
        KitPiece::kick, KitPiece::tom1, KitPiece::tom2,
        KitPiece::snare, KitPiece::floorTom, KitPiece::hihat,
        KitPiece::crash, KitPiece::ride
    };

    for (auto piece : order)
    {
        const auto& p = pieces[static_cast<size_t> (piece)];
        if (p.bounds.expanded (4.0f).contains (pos))
            return piece;
    }

    return KitPiece::count;
}

void DrumKitView::mouseDown (const juce::MouseEvent& e)
{
    const auto piece = pieceAt (e.position);
    if (piece != KitPiece::count)
    {
        const float vel = 0.85f;
        triggerHitForPiece (piece, vel);

        if (onPieceHit != nullptr)
            onPieceHit (piece, vel);

        if (onPieceClicked != nullptr)
            onPieceClicked (piece);
    }
}

void DrumKitView::mouseMove (const juce::MouseEvent& e)
{
    const auto piece = pieceAt (e.position);
    if (piece != hoveredPiece)
    {
        hoveredPiece = piece;
        setMouseCursor (piece != KitPiece::count ? juce::MouseCursor::PointingHandCursor
                                                 : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void DrumKitView::mouseExit (const juce::MouseEvent&)
{
    if (hoveredPiece != KitPiece::count)
    {
        hoveredPiece = KitPiece::count;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
}

//==============================================================================
// Hand-drawn graphic primitives
//==============================================================================
void DrumKitView::drawFloorShadow (juce::Graphics& g, float cx, float cy, float w, float h)
{
    g.setColour (theme.ink.withAlpha (0.45f));

    const int numLines = 18;
    for (int i = 0; i < numLines; ++i)
    {
        const float t = static_cast<float> (i) / static_cast<float> (numLines - 1);
        const float y = cy - h * 0.5f + t * h;
        const float span = w * (0.4f + 0.6f * std::sin (t * 3.14159f));
        const float x1 = cx - span * 0.5f;
        const float x2 = cx + span * 0.5f;

        g.drawLine (x1, y, x2, y, 1.2f);
    }
}

void DrumKitView::drawCrossHatch (juce::Graphics& g, const juce::Rectangle<float>& r, float density)
{
    g.setColour (theme.ink.withAlpha (0.35f));
    const float step = juce::jmax (3.0f, 8.0f - density * 4.0f);

    for (float x = r.getX(); x < r.getRight() + r.getHeight(); x += step)
    {
        float x1 = x;
        float y1 = r.getY();
        float x2 = x - r.getHeight();
        float y2 = r.getBottom();

        if (x1 > r.getRight())
        {
            y1 = r.getY() + (x1 - r.getRight());
            x1 = r.getRight();
        }
        if (x2 < r.getX())
        {
            y2 = r.getBottom() - (r.getX() - x2);
            x2 = r.getX();
        }

        if (x1 >= r.getX() && x2 <= r.getRight() && y1 <= r.getBottom() && y2 >= r.getY())
            g.drawLine (x1, y1, x2, y2, 0.9f);
    }
}

void DrumKitView::drawKick (juce::Graphics& g, const juce::Rectangle<float>& r, float pulse)
{
    const auto centre = r.getCentre();
    const float radius = juce::jmin (r.getWidth(), r.getHeight()) * 0.46f * (1.0f + pulse * 0.08f);

    // 1. Bass drum 3D shell side (wooden cylinder depth)
    const float shellDepth = radius * 0.42f;
    juce::Path shellBody;
    shellBody.startNewSubPath (centre.x, centre.y - radius);
    shellBody.lineTo (centre.x + shellDepth, centre.y - radius * 0.88f);
    shellBody.lineTo (centre.x + shellDepth, centre.y + radius * 0.88f);
    shellBody.lineTo (centre.x, centre.y + radius);
    shellBody.closeSubPath();

    g.setColour (theme.shell);
    g.fillPath (shellBody);
    drawCrossHatch (g, juce::Rectangle<float> (centre.x, centre.y - radius * 0.9f, shellDepth, radius * 1.8f), 0.7f);
    g.setColour (theme.ink);
    g.strokePath (shellBody, juce::PathStrokeType (1.8f));

    // Horizontal wooden hoop stripes on the shell
    for (int i = 1; i <= 3; ++i)
    {
        const float t = i * 0.25f;
        g.drawLine (centre.x + shellDepth * t, centre.y - radius * (1.0f - t * 0.12f),
                    centre.x + shellDepth * t, centre.y + radius * (1.0f - t * 0.12f), 1.0f);
    }

    // 2. Bass drum front resonant head (circular face)
    juce::Path frontHead;
    frontHead.addEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    g.setColour (theme.head);
    g.fillPath (frontHead);
    g.setColour (theme.ink);
    g.strokePath (frontHead, juce::PathStrokeType (2.4f));

    // Outer wooden hoop rim ring
    juce::Path hoopRing;
    hoopRing.addEllipse (centre.x - radius * 0.95f, centre.y - radius * 0.95f, radius * 1.90f, radius * 1.90f);
    g.strokePath (hoopRing, juce::PathStrokeType (1.3f));

    // 3. Tuning lugs / claw hooks around the circumference (8 lugs)
    constexpr int numLugs = 8;
    for (int i = 0; i < numLugs; ++i)
    {
        const float angle = static_cast<float> (i * 2.0 * pi / numLugs);
        const auto p1 = polarPoint (centre, angle, radius * 0.93f);
        const auto p2 = polarPoint (centre, angle, radius * 1.04f);
        g.drawLine (juce::Line<float> (p1, p2), 2.2f);
        g.fillEllipse (p2.x - 2.5f, p2.y - 2.5f, 5.0f, 5.0f);
    }

    // 4. Black microphone port hole in lower right quadrant
    const float portRadius = radius * 0.24f;
    const auto portCentre = juce::Point<float> (centre.x + radius * 0.40f, centre.y + radius * 0.38f);
    g.setColour (theme.ink);
    g.fillEllipse (portCentre.x - portRadius, portCentre.y - portRadius, portRadius * 2.0f, portRadius * 2.0f);
    g.setColour (theme.panel);
    g.drawEllipse (portCentre.x - portRadius * 0.88f, portCentre.y - portRadius * 0.88f,
                   portRadius * 1.76f, portRadius * 1.76f, 1.2f);

    // 5. Bass drum spurs / legs extending outwards
    g.setColour (theme.ink);
    // Left spur
    const auto spurL1 = polarPoint (centre, 3.8f, radius * 0.95f);
    const auto spurL2 = juce::Point<float> (centre.x - radius * 1.35f, centre.y + radius * 1.05f);
    g.drawLine (juce::Line<float> (spurL1, spurL2), 3.0f);
    g.fillEllipse (spurL2.x - 4.0f, spurL2.y - 3.0f, 8.0f, 6.0f); // Rubber foot

    // Right spur
    const auto spurR1 = polarPoint (centre, 2.4f, radius * 0.95f);
    const auto spurR2 = juce::Point<float> (centre.x + radius * 1.35f, centre.y + radius * 1.05f);
    g.drawLine (juce::Line<float> (spurR1, spurR2), 3.0f);
    g.fillEllipse (spurR2.x - 4.0f, spurR2.y - 3.0f, 8.0f, 6.0f);
}

void DrumKitView::drawTom (juce::Graphics& g, const juce::Rectangle<float>& r, bool rackMounted, float angle, float pulse)
{
    const auto centre = r.getCentre();
    const float w = r.getWidth() * (1.0f + pulse * 0.08f);
    const float h = r.getHeight() * (1.0f + pulse * 0.08f);
    const float ellipseH = h * 0.32f;

    juce::AffineTransform rot = juce::AffineTransform::rotation (angle, centre.x, centre.y);

    // Shell cylinder
    juce::Path shell;
    shell.startNewSubPath (centre.x - w * 0.5f, centre.y - h * 0.5f + ellipseH * 0.5f);
    shell.lineTo (centre.x - w * 0.5f, centre.y + h * 0.5f - ellipseH * 0.5f);
    shell.addArc (centre.x - w * 0.5f, centre.y + h * 0.5f - ellipseH, w, ellipseH, 0.0f, static_cast<float> (pi), false);
    shell.lineTo (centre.x + w * 0.5f, centre.y - h * 0.5f + ellipseH * 0.5f);
    shell.closeSubPath();
    shell.applyTransform (rot);

    g.setColour (theme.shell);
    g.fillPath (shell);

    // Shell side cross-hatching
    drawCrossHatch (g, juce::Rectangle<float> (centre.x + w * 0.1f, centre.y - h * 0.2f, w * 0.4f, h * 0.6f), 0.6f);

    g.setColour (theme.ink);
    g.strokePath (shell, juce::PathStrokeType (1.8f));

    // Top batter head ellipse
    juce::Path topHead;
    topHead.addEllipse (centre.x - w * 0.5f, centre.y - h * 0.5f, w, ellipseH);
    topHead.applyTransform (rot);

    g.setColour (theme.head);
    g.fillPath (topHead);
    g.setColour (theme.ink);
    g.strokePath (topHead, juce::PathStrokeType (2.0f));

    // Hoops & Tension Rods / Lugs
    for (int i = 0; i < 4; ++i)
    {
        const float lx = centre.x - w * 0.40f + i * (w * 0.80f / 3.0f);
        juce::Path lug;
        lug.startNewSubPath (lx, centre.y - h * 0.5f + ellipseH * 0.8f);
        lug.lineTo (lx, centre.y + h * 0.5f - ellipseH * 0.6f);
        lug.applyTransform (rot);
        g.strokePath (lug, juce::PathStrokeType (1.4f));

        // Chrome lug casing
        juce::Path casing;
        casing.addRectangle (lx - 2.5f, centre.y - 4.0f, 5.0f, 8.0f);
        casing.applyTransform (rot);
        g.fillPath (casing);
    }

    // Mounting hardware
    if (rackMounted)
    {
        // L-arm mounting bracket connecting to bass drum
        juce::Path arm;
        arm.startNewSubPath (centre.x, centre.y + h * 0.2f);
        arm.lineTo (centre.x, centre.y + h * 0.65f);
        arm.applyTransform (rot);
        g.strokePath (arm, juce::PathStrokeType (2.6f));
    }
    else
    {
        // Floor tom 3 legs
        for (int i = -1; i <= 1; ++i)
        {
            const float lx = centre.x + i * w * 0.38f;
            g.drawLine (lx, centre.y + h * 0.35f, lx + i * w * 0.22f, centre.y + h * 0.85f, 2.0f);
            g.fillEllipse (lx + i * w * 0.22f - 3.0f, centre.y + h * 0.85f - 2.0f, 6.0f, 5.0f);
        }
    }
}

void DrumKitView::drawSnare (juce::Graphics& g, const juce::Rectangle<float>& r, float pulse)
{
    const auto centre = r.getCentre();
    const float w = r.getWidth() * (1.0f + pulse * 0.08f);
    const float h = r.getHeight() * (1.0f + pulse * 0.08f);
    const float ellipseH = h * 0.36f;

    // Snare stand basket arms holding drum
    drawSnareStand (g, { centre.x, centre.y + h * 1.6f }, { centre.x, centre.y + h * 0.4f });

    // Snare drum cylinder
    juce::Path shell;
    shell.startNewSubPath (centre.x - w * 0.5f, centre.y - h * 0.5f + ellipseH * 0.5f);
    shell.lineTo (centre.x - w * 0.5f, centre.y + h * 0.5f - ellipseH * 0.5f);
    shell.addArc (centre.x - w * 0.5f, centre.y + h * 0.5f - ellipseH, w, ellipseH, 0.0f, static_cast<float> (pi), false);
    shell.lineTo (centre.x + w * 0.5f, centre.y - h * 0.5f + ellipseH * 0.5f);
    shell.closeSubPath();

    g.setColour (theme.shell);
    g.fillPath (shell);
    drawCrossHatch (g, juce::Rectangle<float> (centre.x, centre.y - h * 0.2f, w * 0.48f, h * 0.5f), 0.6f);
    g.setColour (theme.ink);
    g.strokePath (shell, juce::PathStrokeType (1.8f));

    // Top batter head
    juce::Path topHead;
    topHead.addEllipse (centre.x - w * 0.5f, centre.y - h * 0.5f, w, ellipseH);
    g.setColour (theme.head);
    g.fillPath (topHead);
    g.setColour (theme.ink);
    g.strokePath (topHead, juce::PathStrokeType (2.0f));

    // Lugs & horizontal shell ribs
    g.drawLine (centre.x - w * 0.48f, centre.y, centre.x + w * 0.48f, centre.y, 1.0f);
    for (int i = 0; i < 5; ++i)
    {
        const float lx = centre.x - w * 0.40f + i * (w * 0.80f / 4.0f);
        g.drawLine (lx, centre.y - h * 0.5f + ellipseH * 0.8f, lx, centre.y + h * 0.5f - ellipseH * 0.5f, 1.4f);
        g.fillRect (lx - 2.0f, centre.y - 3.0f, 4.0f, 6.0f);
    }
}

void DrumKitView::drawSnareStand (juce::Graphics& g, juce::Point<float> base, juce::Point<float> basket)
{
    g.setColour (theme.ink);

    // Basket arms gripping snare
    g.drawLine (basket.x - 28.0f, basket.y - 10.0f, basket.x, basket.y, 2.2f);
    g.drawLine (basket.x + 28.0f, basket.y - 10.0f, basket.x, basket.y, 2.2f);
    g.drawLine (basket.x, basket.y - 14.0f, basket.x, basket.y, 2.2f);

    // Center vertical column
    g.drawLine (basket.x, basket.y, base.x, base.y - 12.0f, 2.6f);
    // Adjustment collar
    g.fillRect (basket.x - 4.0f, basket.y + 12.0f, 8.0f, 5.0f);

    // Tripod feet
    const float spread = 36.0f;
    g.drawLine (base.x, base.y - 12.0f, base.x - spread, base.y, 2.0f);
    g.drawLine (base.x, base.y - 12.0f, base.x + spread, base.y, 2.0f);
    g.drawLine (base.x, base.y - 12.0f, base.x, base.y + 4.0f, 2.0f);
    g.fillEllipse (base.x - spread - 3.0f, base.y - 2.0f, 6.0f, 4.0f);
    g.fillEllipse (base.x + spread - 3.0f, base.y - 2.0f, 6.0f, 4.0f);
}

void DrumKitView::drawCymbalStand (juce::Graphics& g, juce::Point<float> base, juce::Point<float> top, bool isBoom)
{
    g.setColour (theme.ink);

    if (isBoom)
    {
        // Joint position
        const auto joint = juce::Point<float> (top.x + 18.0f, top.y + 36.0f);
        // Vertical column from base to boom joint
        g.drawLine (base.x, base.y - 10.0f, joint.x, joint.y, 2.4f);
        // Boom joint & counterweight
        g.fillEllipse (joint.x - 4.0f, joint.y - 4.0f, 8.0f, 8.0f);
        g.drawLine (joint.x, joint.y, joint.x + 14.0f, joint.y + 16.0f, 3.2f); // Counterweight
        // Boom arm extending to cymbal top
        g.drawLine (joint.x, joint.y, top.x, top.y, 2.2f);
    }
    else
    {
        // Straight stand
        g.drawLine (base.x, base.y - 10.0f, top.x, top.y, 2.4f);
    }

    // Tripod feet
    const float spread = 34.0f;
    g.drawLine (base.x, base.y - 10.0f, base.x - spread, base.y, 1.9f);
    g.drawLine (base.x, base.y - 10.0f, base.x + spread, base.y, 1.9f);
    g.fillEllipse (base.x - spread - 3.0f, base.y - 2.0f, 6.0f, 4.0f);
    g.fillEllipse (base.x + spread - 3.0f, base.y - 2.0f, 6.0f, 4.0f);
}

void DrumKitView::drawCymbal (juce::Graphics& g, const juce::Rectangle<float>& r, float wobble, float tiltAngle)
{
    const auto centre = r.getCentre();
    const float totalAngle = tiltAngle + wobble;

    juce::AffineTransform transform =
        juce::AffineTransform::rotation (totalAngle, centre.x, centre.y)
            .scaled (1.0f + wobble * 0.12f, 1.0f - std::abs (wobble) * 0.25f, centre.x, centre.y);

    // Main cymbal body
    juce::Path cymbal;
    cymbal.addEllipse (r);
    cymbal.applyTransform (transform);

    g.setColour (theme.cymbal);
    g.fillPath (cymbal);
    g.setColour (theme.ink);
    g.strokePath (cymbal, juce::PathStrokeType (2.2f));

    // Center bell
    juce::Path bell;
    bell.addEllipse (r.reduced (r.getWidth() * 0.40f, r.getHeight() * 0.38f));
    bell.applyTransform (transform);
    g.fillPath (bell);

    // Lathe groove rings
    for (float f : { 0.65f, 0.82f })
    {
        juce::Path groove;
        groove.addEllipse (r.reduced (r.getWidth() * (1.0f - f) * 0.5f, r.getHeight() * (1.0f - f) * 0.5f));
        groove.applyTransform (transform);
        g.setColour (theme.ink.withAlpha (0.4f));
        g.strokePath (groove, juce::PathStrokeType (1.0f));
    }
}

void DrumKitView::drawHiHat (juce::Graphics& g, const juce::Rectangle<float>& r, float wobble)
{
    const auto centre = r.getCentre();
    const float footY = centre.y + 130.0f;

    // Hi-hat stand vertical tube
    g.setColour (theme.ink);
    g.drawLine (centre.x, centre.y - 18.0f, centre.x, footY, 2.6f);
    // Clutch holding top cymbal
    g.fillRect (centre.x - 3.0f, centre.y - 14.0f, 6.0f, 10.0f);

    // Pedal at the bottom
    juce::Path pedal;
    pedal.startNewSubPath (centre.x - 14.0f, footY - 2.0f);
    pedal.lineTo (centre.x + 14.0f, footY - 2.0f);
    pedal.lineTo (centre.x + 8.0f, footY - 14.0f);
    pedal.lineTo (centre.x - 8.0f, footY - 14.0f);
    pedal.closeSubPath();
    g.strokePath (pedal, juce::PathStrokeType (2.0f));

    // Tripod feet
    const float spread = 30.0f;
    g.drawLine (centre.x, footY - 16.0f, centre.x - spread, footY - 4.0f, 2.0f);
    g.drawLine (centre.x, footY - 16.0f, centre.x + spread, footY - 4.0f, 2.0f);

    // Bottom Cymbal
    juce::Path botCymbal;
    botCymbal.addEllipse (r.getX(), r.getY() + 3.0f, r.getWidth(), r.getHeight());
    g.setColour (theme.cymbal);
    g.fillPath (botCymbal);
    g.setColour (theme.ink);
    g.strokePath (botCymbal, juce::PathStrokeType (1.8f));

    // Top Cymbal (wobbles with hit)
    juce::AffineTransform topTrans = juce::AffineTransform::rotation (wobble, centre.x, centre.y);
    juce::Path topCymbal;
    topCymbal.addEllipse (r);
    topCymbal.applyTransform (topTrans);

    g.setColour (theme.cymbal);
    g.fillPath (topCymbal);
    g.setColour (theme.ink);
    g.strokePath (topCymbal, juce::PathStrokeType (2.0f));

    // Bell
    juce::Path bell;
    bell.addEllipse (r.reduced (r.getWidth() * 0.42f, r.getHeight() * 0.35f));
    bell.applyTransform (topTrans);
    g.fillPath (bell);
}

void DrumKitView::drawImpact (juce::Graphics& g, const juce::Rectangle<float>& r, float amount)
{
    if (amount <= 0.02f)
        return;

    g.setColour (theme.ink.withAlpha (amount * 0.95f));

    const auto centre = r.getCentre();
    const auto radius = juce::jmax (r.getWidth(), r.getHeight()) * (0.60f + (1.0f - amount) * 0.28f);

    // Comic action burst strokes (\ | /)
    constexpr int numStrokes = 8;
    for (int i = 0; i < numStrokes; ++i)
    {
        const auto angle = static_cast<float> (pi * (0.10 + 0.80 * i / static_cast<double> (numStrokes - 1)));
        const auto inner = polarPoint (centre, angle, radius * 0.90f);
        const auto outer = polarPoint (centre, angle, radius * (1.10f + amount * 0.22f));
        g.drawLine (juce::Line<float> (inner, outer), 2.2f);
    }
}

//==============================================================================
void DrumKitView::paint (juce::Graphics& g)
{
    layout();

    const auto b = getLocalBounds().toFloat();
    const auto& kick = pieces[static_cast<size_t> (KitPiece::kick)].bounds;
    const auto& hihat = pieces[static_cast<size_t> (KitPiece::hihat)].bounds;
    const auto& ride = pieces[static_cast<size_t> (KitPiece::ride)].bounds;
    const auto& crash = pieces[static_cast<size_t> (KitPiece::crash)].bounds;

    // 1. Floor shadow beneath the entire kit
    drawFloorShadow (g, kick.getCentreX() + 15.0f, kick.getBottom() + 12.0f, b.getWidth() * 0.60f, 28.0f);

    // 2. Cymbal stands in background
    drawCymbalStand (g, { crash.getCentreX() - 10.0f, kick.getBottom() + 8.0f }, crash.getCentre(), true);
    drawCymbalStand (g, { ride.getCentreX() + 20.0f, kick.getBottom() + 8.0f }, ride.getCentre(), true);

    // 3. Central Tom Mount post on bass drum
    g.setColour (theme.ink);
    g.drawLine (kick.getCentreX(), kick.getY() + 14.0f, kick.getCentreX(), kick.getY() - 16.0f, 3.4f);

    // 4. Drums: Toms, Snare, Floor Tom
    auto& pTom1 = pieces[static_cast<size_t> (KitPiece::tom1)];
    drawTom (g, pTom1.bounds, true, -0.10f, pTom1.squash);
    drawImpact (g, pTom1.bounds, pTom1.flash);

    auto& pTom2 = pieces[static_cast<size_t> (KitPiece::tom2)];
    drawTom (g, pTom2.bounds, true, 0.12f, pTom2.squash);
    drawImpact (g, pTom2.bounds, pTom2.flash);

    auto& pSnare = pieces[static_cast<size_t> (KitPiece::snare)];
    drawSnare (g, pSnare.bounds, pSnare.squash);
    drawImpact (g, pSnare.bounds, pSnare.flash);

    auto& pFloorTom = pieces[static_cast<size_t> (KitPiece::floorTom)];
    drawTom (g, pFloorTom.bounds, false, 0.05f, pFloorTom.squash);
    drawImpact (g, pFloorTom.bounds, pFloorTom.flash);

    // 5. Bass Drum in foreground
    auto& pKick = pieces[static_cast<size_t> (KitPiece::kick)];
    drawKick (g, pKick.bounds, pKick.squash);
    drawImpact (g, pKick.bounds, pKick.flash);

    // 6. Hi-Hat Stand and cymbals
    auto& pHH = pieces[static_cast<size_t> (KitPiece::hihat)];
    drawHiHat (g, pHH.bounds, pHH.wobble);
    drawImpact (g, pHH.bounds, pHH.flash);

    // 7. Crash and Ride Cymbals on top
    auto& pCrash = pieces[static_cast<size_t> (KitPiece::crash)];
    drawCymbal (g, pCrash.bounds, pCrash.wobble, -0.22f);
    drawImpact (g, pCrash.bounds, pCrash.flash);

    auto& pRide = pieces[static_cast<size_t> (KitPiece::ride)];
    drawCymbal (g, pRide.bounds, pRide.wobble, -0.16f);
    drawImpact (g, pRide.bounds, pRide.flash);

    // 8. Hover indicator if hovering over any piece
    if (hoveredPiece != KitPiece::count)
    {
        const auto& hp = pieces[static_cast<size_t> (hoveredPiece)];
        g.setColour (theme.accent.withAlpha (0.45f));
        g.drawEllipse (hp.bounds.expanded (3.0f), 2.0f);
    }
}

} // namespace glorp
