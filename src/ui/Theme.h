/*
    Theme.h - colours, fonts and the hand-drawn look.

    The mockup is ink on paper: black outlines, white callout boxes, one warm
    off-white background. Everything here exists to reproduce that, plus a dark
    variant for people who work at night, because a drum plugin that glares at
    you during a night session is no fun.
*/

#pragma once

#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

#include "GlorpDrumsAssets.h"

namespace glorp
{

struct Theme
{
    // -- paper -------------------------------------------------------------
    juce::Colour background;
    juce::Colour panel;
    juce::Colour panelEdge;
    juce::Colour ink;              ///< outlines and text
    juce::Colour inkSoft;          ///< secondary text and tick marks
    juce::Colour accent;           ///< highlights, active state, learned mapping
    juce::Colour warning;

    // -- drum kit art ------------------------------------------------------
    juce::Colour shell;
    juce::Colour shellShade;
    juce::Colour cymbal;
    juce::Colour head;

    bool isDark = false;

    static Theme sketch() noexcept
    {
        Theme t;
        t.isDark = false;
        t.background = juce::Colour (0xffd8dbde);
        t.panel = juce::Colour (0xffffffff);
        t.panelEdge = juce::Colour (0xff121214);
        t.ink = juce::Colour (0xff101012);
        t.inkSoft = juce::Colour (0xff54575a);
        t.accent = juce::Colour (0xfff09a1a);
        t.warning = juce::Colour (0xffd03020);
        t.shell = juce::Colour (0xfffcfdfe);
        t.shellShade = juce::Colour (0xffc4c8cd);
        t.cymbal = juce::Colour (0xfff8f9fa);
        t.head = juce::Colour (0xffffffff);
        return t;
    }

    static Theme dark() noexcept
    {
        Theme t;
        t.isDark = true;
        t.background = juce::Colour (0xff1b1b1e);
        t.panel = juce::Colour (0xff26262b);
        t.panelEdge = juce::Colour (0xffd8d4c8);
        t.ink = juce::Colour (0xfff0ece2);
        t.inkSoft = juce::Colour (0xff9a958a);
        t.accent = juce::Colour (0xffe8703f);
        t.warning = juce::Colour (0xffe8a33f);
        t.shell = juce::Colour (0xff3a3a41);
        t.shellShade = juce::Colour (0xff2a2a30);
        t.cymbal = juce::Colour (0xff9c8f52);
        t.head = juce::Colour (0xff45454d);
        return t;
    }

    static Theme byIndex (int index) noexcept { return index == 1 ? dark() : sketch(); }
};

//==============================================================================
/** Loads one of the bundled fonts.
    JUCE's CMake binary-data target only emits raw symbols, so the typeface is
    built straight from the embedded bytes rather than unpacked to a file.
    Both faces are cached, because FreeType is not cheap to construct and every
    repaint asks for a font.

    The symbol names come from JUCE replacing '.' and ' ' with '_' and then
    dropping anything else that is not alphanumeric, so the '-' in
    "PatrickHand-Regular.ttf" simply disappears.
*/
inline juce::Typeface::Ptr uiTypeface()
{
    static const juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (
        GlorpAssets::PatrickHandRegular_ttf, GlorpAssets::PatrickHandRegular_ttfSize);

    return face;
}

inline juce::Typeface::Ptr displayTypeface()
{
    static const juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (
        GlorpAssets::LilitaOneRegular_ttf, GlorpAssets::LilitaOneRegular_ttfSize);

    return face;
}

inline juce::Font uiFont (float height)
{
    const auto size = juce::jmax (6.0f, height * 0.72f);

    if (auto face = uiTypeface())
    {
        juce::Font font (face);
        font.setHeight (size);
        return font;
    }

    return juce::Font (size);
}

inline juce::Font displayFont (float height)
{
    const auto size = juce::jmax (8.0f, height * 0.62f);

    if (auto face = displayTypeface())
    {
        juce::Font font (face);
        font.setHeight (size);
        return font;
    }

    return juce::Font (size);
}

/// The bundled logo. Parsed fresh each call, because it is only ever needed once
/// per editor and an SVG parse is not worth caching across plugin instances.
inline std::unique_ptr<juce::Drawable> logoDrawable()
{
    auto root = juce::parseXML (juce::String::fromUTF8 (GlorpAssets::logo_svg,
                                                        GlorpAssets::logo_svgSize));

    if (root != nullptr)
        return juce::Drawable::createFromSVG (*root);

    return {};
}

//==============================================================================
/** Draws a rectangle outline with a hand-drawn wobble.
    A perfectly straight line is what makes a UI look like it came out of a
    plugin, so every outline gets a deterministic per-edge jitter. Deterministic
    matters: the same control must not shimmer when redrawn.
*/
inline void drawSketchRect (juce::Graphics& g, juce::Rectangle<float> r, const juce::Colour& colour,
                            float thickness = 1.6f, unsigned seed = 0)
{
    auto jitter = [] (unsigned s, int index, float amount)
    {
        // Cheap integer hash, stable for a given seed and edge.
        auto h = static_cast<unsigned> (index) * 374761393u + s * 668265263u + 1442695040u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h = h ^ (h >> 16);
        return (static_cast<float> (h & 0xFFFF) / 32767.5f - 1.0f) * amount;
    };

    juce::Path path;
    constexpr int segmentsPerEdge = 4;
    float travelled = 0.0f;

    for (int edge = 0; edge < 4; ++edge)
    {
        const auto from = [&] (float t)
        {
            switch (edge)
            {
                case 0: return r.getX() + t * r.getWidth();
                case 1: return r.getRight() + t * r.getHeight();
                case 2: return r.getRight() - t * r.getWidth();
                default: return r.getY() - t * r.getHeight();
            }
        };

        const bool horizontal = (edge == 0 || edge == 2);

        for (int s = 0; s <= segmentsPerEdge; ++s)
        {
            const auto t = static_cast<float> (s) / segmentsPerEdge;
            const auto base = from (t);
            const auto nudge = jitter (seed, edge * 8 + s, std::min (1.4f, r.getWidth() * 0.012f));

            const juce::Point<float> point = horizontal
                ? juce::Point<float> (base, r.getY() + (edge == 0 ? 0.0f : r.getHeight()) + nudge)
                : juce::Point<float> (r.getX() + (edge == 3 ? 0.0f : r.getWidth()) + nudge, base);

            if (edge == 0 && s == 0)
                path.startNewSubPath (point);
            else
                path.lineTo (point);
        }

        travelled += horizontal ? r.getWidth() : r.getHeight();
        juce::ignoreUnused (travelled);
    }

    path.closeSubPath();

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

} // namespace glorp
