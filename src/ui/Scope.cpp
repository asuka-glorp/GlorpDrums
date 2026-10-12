#include "Scope.h"

#include <algorithm>

namespace glorp
{

Scope::Scope()
{
    setOpaque (false);
    startTimerHz (30);
}

Scope::~Scope()
{
    stopTimer();
}

void Scope::timerCallback()
{
    repaint();
}

ScopeLane Scope::laneForPieceAndName (KitPiece piece, const juce::String& name)
{
    const auto lower = name.toLowerCase();

    if (lower.contains ("cowbell") || lower.contains ("tamb") || lower.contains ("shaker")
        || lower.contains ("perc") || lower.contains ("aux") || lower.contains ("clap")
        || lower.contains ("conga") || lower.contains ("bongo"))
        return ScopeLane::etc;

    switch (piece)
    {
        case KitPiece::crash1:
        case KitPiece::crash2:
        case KitPiece::ride:
            return ScopeLane::cymbal;

        case KitPiece::hihat:
            return ScopeLane::hihat;

        case KitPiece::tom1:
        case KitPiece::tom2:
        case KitPiece::floorTom:
            return ScopeLane::tom;

        case KitPiece::snare:
            return ScopeLane::snare;

        case KitPiece::kick:
            return ScopeLane::kick;

        default:
            break;
    }

    if (lower.contains ("crash") || lower.contains ("ride") || lower.contains ("cym")
        || lower.contains ("china") || lower.contains ("splash"))
        return ScopeLane::cymbal;

    if (lower.contains ("hat") || lower.contains ("hh"))
        return ScopeLane::hihat;

    if (lower.contains ("tom"))
        return ScopeLane::tom;

    if (lower.contains ("snare") || lower.contains ("snr"))
        return ScopeLane::snare;

    if (lower.contains ("kick") || lower.contains ("bd") || lower.contains ("kdrum"))
        return ScopeLane::kick;

    return ScopeLane::etc;
}

juce::Colour Scope::colorForInstrument (KitPiece piece, const juce::String& name)
{
    const auto lower = name.toLowerCase();

    // 1. Cymbals (with distinct shades)
    if (piece == KitPiece::crash1 || lower.contains ("crash1") || lower.contains ("crash 1"))
        return juce::Colour (0xff84cc16); // Electric Lime
    if (piece == KitPiece::crash2 || lower.contains ("crash2") || lower.contains ("crash 2"))
        return juce::Colour (0xff10b981); // Vibrant Mint Green
    if (piece == KitPiece::ride || lower.contains ("ride"))
        return juce::Colour (0xffeab308); // Golden Brass Yellow
    if (lower.contains ("china") || lower.contains ("splash"))
        return juce::Colour (0xff06b6d4); // Aqua Cyan

    // 2. Hi-Hat
    if (piece == KitPiece::hihat || lower.contains ("hat") || lower.contains ("hihat"))
        return juce::Colour (0xff00b4d8); // Crisp Cyan Sizzle

    // 3. Toms (with distinct shades)
    if (piece == KitPiece::tom1 || lower.contains ("tom1") || lower.contains ("hightom") || lower.contains ("rack1"))
        return juce::Colour (0xffa855f7); // Bright Violet / Lavender
    if (piece == KitPiece::tom2 || lower.contains ("tom2") || lower.contains ("midtom") || lower.contains ("rack2"))
        return juce::Colour (0xff7e22ce); // Deep Purple
    if (piece == KitPiece::floorTom || lower.contains ("floor") || lower.contains ("ftom") || lower.contains ("lowtom"))
        return juce::Colour (0xffc026d3); // Deep Magenta / Plum

    // 4. Snare
    if (piece == KitPiece::snare || lower.contains ("snare"))
    {
        if (lower.contains ("rimshot"))
            return juce::Colour (0xffff6b35); // Sunset Orange
        if (lower.contains ("click") || lower.contains ("cross"))
            return juce::Colour (0xffffb703); // Golden Amber
        return juce::Colour (0xfff97316);     // Electric Orange
    }

    // 5. Kick
    if (piece == KitPiece::kick || lower.contains ("kick") || lower.contains ("bd") || lower.contains ("kdrum"))
        return juce::Colour (0xffdc2626); // Fiery Crimson Red

    // 6. ETC / Percussion
    if (lower.contains ("cowbell"))
        return juce::Colour (0xffff006e); // Hot Pink
    if (lower.contains ("tamb") || lower.contains ("shaker"))
        return juce::Colour (0xfff72585); // Fuchsia

    return juce::Colour (0xffec4899);     // Pink
}

juce::String Scope::shortLabelForInstrument (KitPiece piece, const juce::String& name)
{
    const auto lower = name.toLowerCase();

    if (piece == KitPiece::crash1 || lower.contains ("crash1")) return "CR1";
    if (piece == KitPiece::crash2 || lower.contains ("crash2")) return "CR2";
    if (piece == KitPiece::ride || lower.contains ("ride"))
    {
        if (lower.contains ("bell")) return "BEL";
        if (lower.contains ("edge")) return "R.ED";
        return "RID";
    }
    if (lower.contains ("china")) return "CHN";
    if (lower.contains ("splash")) return "SPL";

    if (piece == KitPiece::hihat || lower.contains ("hat") || lower.contains ("hihat"))
    {
        if (lower.contains ("open")) return "HH.O";
        if (lower.contains ("semi")) return "HH.S";
        if (lower.contains ("pedal") || lower.contains ("foot")) return "HH.P";
        return "HH.C";
    }

    if (piece == KitPiece::tom1 || lower.contains ("tom1") || lower.contains ("hightom")) return "T1";
    if (piece == KitPiece::tom2 || lower.contains ("tom2") || lower.contains ("midtom")) return "T2";
    if (piece == KitPiece::floorTom || lower.contains ("floor") || lower.contains ("ftom")) return "FT";

    if (piece == KitPiece::snare || lower.contains ("snare"))
    {
        if (lower.contains ("rimshot")) return "SNR.R";
        if (lower.contains ("click") || lower.contains ("cross")) return "SNR.C";
        return "SNR";
    }

    if (piece == KitPiece::kick || lower.contains ("kick") || lower.contains ("bd")) return "KCK";

    if (lower.contains ("cowbell")) return "COW";
    if (lower.contains ("tamb")) return "TMB";
    if (lower.contains ("shaker")) return "SHK";

    return name.substring (0, 3).toUpperCase();
}

void Scope::hitReceived (const juce::String& instrumentName, KitPiece piece, int note, float velocity, double timestampSeconds)
{
    const double now = (timestampSeconds > 0.0)
                           ? timestampSeconds
                           : juce::Time::getMillisecondCounterHiRes() * 0.001;
    const auto lane = laneForPieceAndName (piece, instrumentName);
    const auto color = colorForInstrument (piece, instrumentName);
    const auto label = shortLabelForInstrument (piece, instrumentName);
    const int midiVel = std::clamp (static_cast<int> (velocity * 127.0f), 1, 127);

    ScopeHit& h = hits[writeIndex];
    h.lane = lane;
    h.instrumentName = instrumentName;
    h.shortLabel = label;
    h.color = color;
    h.velocity = std::clamp (velocity, 0.05f, 1.0f);
    h.midiVelocity = midiVel;
    h.note = note;
    h.timeSeconds = now;

    writeIndex = (writeIndex + 1) % maxHits;
    if (hitCount < maxHits)
        ++hitCount;

    // Update active chord tracking for Live Hit Inspector HUD
    const double chordThreshold = 0.035; // 35ms window for simultaneous hits
    if (std::abs (now - recentChord.timestamp) > chordThreshold)
    {
        recentChord.timestamp = now;
        recentChord.items.clear();
    }

    RecentChord::Item item;
    item.label = label;
    item.color = color;
    item.velocity = midiVel;
    item.note = note;
    recentChord.items.push_back (item);

    repaint();
}

void Scope::noteReceived (int note, int velocity)
{
    // Fallback: infer piece from General MIDI standard numbers
    KitPiece piece = KitPiece::count;
    juce::String name = "MIDI " + juce::String (note);

    if (note == 35 || note == 36) { piece = KitPiece::kick; name = "Kick"; }
    else if (note == 38 || note == 40) { piece = KitPiece::snare; name = "Snare"; }
    else if (note == 37) { piece = KitPiece::snare; name = "SnareSide"; }
    else if (note == 42 || note == 44 || note == 46 || note == 26) { piece = KitPiece::hihat; name = "Hihat"; }
    else if (note == 48 || note == 50 || note == 82) { piece = KitPiece::tom1; name = "Tom1"; }
    else if (note == 45 || note == 47 || note == 80) { piece = KitPiece::tom2; name = "Tom2"; }
    else if (note == 41 || note == 43 || note == 75) { piece = KitPiece::floorTom; name = "FloorTom"; }
    else if (note == 49 || note == 55 || note == 84) { piece = KitPiece::crash1; name = "Crash1"; }
    else if (note == 57 || note == 52 || note == 89) { piece = KitPiece::crash2; name = "Crash2"; }
    else if (note == 51 || note == 53 || note == 59) { piece = KitPiece::ride; name = "Ride"; }
    else if (note == 56) { name = "Cowbell"; }

    hitReceived (name, piece, note, velocity / 127.0f);
}

void Scope::controllerReceived (int cc, int value)
{
    if (cc == 4) // Hi-Hat pedal
    {
        lastCc4Value = value / 127.0f;
        lastCc4Time = juce::Time::getMillisecondCounterHiRes() * 0.001;
        repaint();
    }
}

void Scope::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    // Background paper
    g.setColour (theme.panel);
    g.fillRoundedRectangle (b, 4.0f);
    drawSketchRect (g, b, theme.panelEdge, 1.4f, 5);

    // =========================================================================
    // 1. Header Bar: Title on left, Live Hit Inspector HUD on right
    // =========================================================================
    const float headerH = 20.0f;
    auto headerBounds = b.removeFromTop (headerH);

    g.setFont (displayFont (12.5f));
    g.setColour (theme.ink);
    g.drawText ("MIDI ACTIVITY", headerBounds.getX() + 8.0f, headerBounds.getY() + 2.0f, 130.0f, 16.0f,
                juce::Justification::centredLeft, false);

    // HUD Chord Display
    const double chordAge = now - recentChord.timestamp;
    if (chordAge < 2.0 && ! recentChord.items.empty())
    {
        const float alpha = static_cast<float> (std::clamp ((2.0 - chordAge) / 0.8, 0.0, 1.0));
        float curX = headerBounds.getRight() - 10.0f;

        // Draw pills in reverse so they line up from the right
        for (int i = static_cast<int> (recentChord.items.size()) - 1; i >= 0; --i)
        {
            const auto& it = recentChord.items[static_cast<size_t> (i)];
            const juce::String text = it.label + " " + juce::String (it.velocity);

            g.setFont (displayFont (11.0f));
            const float pillW = g.getCurrentFont().getStringWidthFloat (text) + 12.0f;
            const float minPillX = headerBounds.getX() + 125.0f;
            if (curX - pillW < minPillX)
                break;

            curX -= pillW;

            auto pill = juce::Rectangle<float> (curX, headerBounds.getY() + 1.0f, pillW, 16.0f);

            // Pill background
            g.setColour (it.color.withAlpha (alpha * (theme.isDark ? 0.35f : 0.20f)));
            g.fillRoundedRectangle (pill, 3.0f);

            // Sketched pill outline
            g.setColour (it.color.withAlpha (alpha * 0.95f));
            g.drawRoundedRectangle (pill, 3.0f, 1.0f);

            // Pill text
            g.setColour (theme.isDark ? it.color.withAlpha (alpha) : theme.ink.withAlpha (alpha));
            g.drawText (text, pill, juce::Justification::centred, false);

            curX -= 5.0f;
        }
    }
    else
    {
        g.setFont (uiFont (11.0f));
        g.setColour (theme.inkSoft.withAlpha (0.45f));
        g.drawText ("READY", headerBounds.getRight() - 65.0f, headerBounds.getY() + 2.0f, 55.0f, 16.0f,
                    juce::Justification::centredRight, false);
    }

    // =========================================================================
    // 2. 6 Dedicated Lanes Setup
    // =========================================================================
    const float labelColW = 34.0f;
    const float startX = b.getX() + labelColW + 8.0f;
    const float endX = b.getRight() - 8.0f;
    const float timeW = endX - startX;

    const int numLanes = static_cast<int> (ScopeLane::count); // 6
    const float laneH = b.getHeight() / static_cast<float> (numLanes);

    static const char* laneNames[6] = { "CYM", "HH", "TOM", "SNR", "KCK", "ETC" };
    static const uint32_t laneAccentHex[6] = {
        0xff84cc16, // CYM Lime
        0xff00b4d8, // HH Cyan
        0xffa855f7, // TOM Violet
        0xfff97316, // SNR Amber
        0xffdc2626, // KCK Crimson
        0xffff006e  // ETC Pink
    };

    // Draw Lane Headers & Separators
    for (int l = 0; l < numLanes; ++l)
    {
        const float ly = b.getY() + l * laneH;
        auto labelRect = juce::Rectangle<float> (b.getX() + 4.0f, ly + 1.0f, labelColW, laneH - 2.0f);

        // Lane label pill
        const auto laneCol = juce::Colour (laneAccentHex[l]);
        g.setColour (laneCol.withAlpha (theme.isDark ? 0.25f : 0.12f));
        g.fillRoundedRectangle (labelRect, 2.0f);

        g.setColour (theme.isDark ? laneCol : theme.ink.withAlpha (0.85f));
        g.setFont (displayFont (9.5f));
        g.drawText (laneNames[l], labelRect, juce::Justification::centred, false);

        // Lane horizontal divider line
        if (l > 0)
        {
            g.setColour (theme.ink.withAlpha (0.09f));
            g.drawHorizontalLine (static_cast<int> (ly), startX - 2.0f, endX);
        }
    }

    // Baseline separator between label column and timeline
    g.setColour (theme.ink.withAlpha (0.20f));
    g.drawVerticalLine (static_cast<int> (startX - 4.0f), b.getY(), b.getBottom());

    // =========================================================================
    // 3. Render Hits along the Scrolling Timeline
    // =========================================================================
    const double timelineWindowSec = 3.5; // 3.5 seconds visible window

    // First pass: detect simultaneous hits (chords within 35ms) to draw connecting guide
    for (int i = 0; i < hitCount; ++i)
    {
        const auto& h1 = hits[(writeIndex - 1 - i + maxHits) % maxHits];
        const double age1 = now - h1.timeSeconds;
        if (age1 > timelineWindowSec)
            break;
        if (age1 < 0.0)
            continue;

        const float x1 = endX - static_cast<float> (age1 / timelineWindowSec) * timeW;

        for (int j = i + 1; j < hitCount; ++j)
        {
            const auto& h2 = hits[(writeIndex - 1 - j + maxHits) % maxHits];
            const double dt = std::abs (h1.timeSeconds - h2.timeSeconds);
            if (dt > 0.035)
                break; // Beyond chord window

            if (h1.lane != h2.lane)
            {
                const float y1 = b.getY() + (static_cast<int> (h1.lane) + 0.5f) * laneH;
                const float y2 = b.getY() + (static_cast<int> (h2.lane) + 0.5f) * laneH;

                // Subtle vertical chord guide connecting simultaneous hits
                const float dashes[2] = { 3.0f, 3.0f };
                g.setColour (theme.ink.withAlpha (0.28f));
                g.drawDashedLine (juce::Line<float> (x1, y1, x1, y2), dashes, 2, 1.0f);
            }
        }
    }

    // Second pass: Draw hit capsules in each lane
    for (int i = 0; i < hitCount; ++i)
    {
        const auto& h = hits[(writeIndex - 1 - i + maxHits) % maxHits];
        const double age = now - h.timeSeconds;
        if (age > timelineWindowSec)
            break;
        if (age < 0.0)
            continue;

        const float x = endX - static_cast<float> (age / timelineWindowSec) * timeW;
        const int laneIdx = static_cast<int> (h.lane);
        const float laneY = b.getY() + (laneIdx + 0.5f) * laneH;

        const float vel = h.velocity; // 0.05 .. 1.0
        const float capW = std::clamp (5.0f + vel * 9.0f, 5.0f, 14.0f);
        const float capH = std::clamp (4.0f + vel * (laneH - 3.0f), 4.0f, laneH - 2.0f);

        auto capRect = juce::Rectangle<float> (x - capW * 0.5f, laneY - capH * 0.5f, capW, capH);

        // Recent hit ripple/glow (under 0.15s)
        if (age < 0.15)
        {
            const float expand = static_cast<float> ((0.15 - age) / 0.15) * 6.0f;
            g.setColour (h.color.withAlpha (0.45f));
            g.drawRoundedRectangle (capRect.expanded (expand * 0.7f, expand * 0.4f), 3.0f, 1.2f);
        }

        // Fill capsule
        g.setColour (h.color.withAlpha (vel < 0.35f ? 0.65f : 0.95f));
        g.fillRoundedRectangle (capRect, 2.5f);

        // Sketched ink border
        g.setColour (theme.ink.withAlpha (0.85f));
        g.drawRoundedRectangle (capRect, 2.5f, 1.1f);

        // High intensity spark / starburst for hard accents (vel >= 100 / 0.78f)
        if (vel >= 0.78f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.fillEllipse (x - 1.2f, laneY - 1.2f, 2.4f, 2.4f);

            // Tiny 4-point star accent
            juce::Path star;
            star.startNewSubPath (x, laneY - 3.0f);
            star.lineTo (x, laneY + 3.0f);
            star.startNewSubPath (x - 3.0f, laneY);
            star.lineTo (x + 3.0f, laneY);
            g.strokePath (star, juce::PathStrokeType (1.0f));
        }
    }
}

} // namespace glorp
