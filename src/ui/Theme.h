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

#include <atomic>
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
    juce::Colour ink;     ///< outlines and text
    juce::Colour inkSoft; ///< secondary text and tick marks
    juce::Colour accent;  ///< highlights, active state, learned mapping
    juce::Colour warning;
    juce::Colour trough; ///< recessed wells, fader slots, meters, graph interior

    // -- drum kit art ------------------------------------------------------
    juce::Colour shell;
    juce::Colour shellShade;
    juce::Colour cymbal;
    juce::Colour head;

    bool isDark = false;
    bool cleanStyle = false; ///< Clean sans-serif fonts & straight geometric borders for high readability

    static Theme sketch() noexcept
    {
        Theme t;
        t.isDark = false;
        t.cleanStyle = false;
        t.background = juce::Colour(0xffe2e5e2);
        t.panel = juce::Colour(0xfffcfdfc);
        t.panelEdge = juce::Colour(0xff111215);
        t.ink = juce::Colour(0xff101114);
        t.inkSoft = juce::Colour(0xff4b4e54);
        t.accent = juce::Colour(0xffea8c16);
        t.warning = juce::Colour(0xffd03020);
        t.trough = juce::Colour(0xffede8dc);
        t.shell = juce::Colour(0xfff9fafb);
        t.shellShade = juce::Colour(0xffcbd0d6);
        t.cymbal = juce::Colour(0xfff4f5f6);
        t.head = juce::Colour(0xffffffff);
        return t;
    }

    static Theme dark() noexcept
    {
        Theme t;
        t.isDark = true;
        t.cleanStyle = false;
        t.background = juce::Colour(0xff161719);
        t.panel = juce::Colour(0xff1c1d21);
        t.panelEdge = juce::Colour(0xffffffff);
        t.ink = juce::Colour(0xffffffff);
        t.inkSoft = juce::Colour(0xffb0b4ba);
        t.accent = juce::Colour(0xffea8c16);
        t.warning = juce::Colour(0xffe8a33f);
        t.trough = juce::Colour(0xff121316);
        t.shell = juce::Colour(0xff3a3a41);
        t.shellShade = juce::Colour(0xff2a2a30);
        t.cymbal = juce::Colour(0xff9c8f52);
        t.head = juce::Colour(0xff45454d);
        return t;
    }

    static Theme studioClean() noexcept
    {
        Theme t;
        t.isDark = false;
        t.cleanStyle = true;
        t.background = juce::Colour(0xfff0f2f5);
        t.panel = juce::Colour(0xffffffff);
        t.panelEdge = juce::Colour(0xff2d3748);
        t.ink = juce::Colour(0xff0d1117);
        t.inkSoft = juce::Colour(0xff4a5568);
        t.accent = juce::Colour(0xffd96b08);
        t.warning = juce::Colour(0xffc92a2a);
        t.trough = juce::Colour(0xffe2e8f0);
        t.shell = juce::Colour(0xfff8fafc);
        t.shellShade = juce::Colour(0xffcbd5e1);
        t.cymbal = juce::Colour(0xfff1f5f9);
        t.head = juce::Colour(0xffffffff);
        return t;
    }

    static Theme highContrast() noexcept
    {
        Theme t;
        t.isDark = true;
        t.cleanStyle = true;
        t.background = juce::Colour(0xff0a0c10);
        t.panel = juce::Colour(0xff161b22);
        t.panelEdge = juce::Colour(0xffcbd5e1);
        t.ink = juce::Colour(0xffffffff);
        t.inkSoft = juce::Colour(0xffcbd5e1);
        t.accent = juce::Colour(0xffffb020);
        t.warning = juce::Colour(0xffff5252);
        t.trough = juce::Colour(0xff0d1117);
        t.shell = juce::Colour(0xff30363d);
        t.shellShade = juce::Colour(0xff21262d);
        t.cymbal = juce::Colour(0xffa69658);
        t.head = juce::Colour(0xff3b424c);
        return t;
    }

    static constexpr int numSkins() noexcept { return 4; }

    static juce::String skinName(int index)
    {
        switch (index)
        {
            case 1:
                return "Dark (Night)";
            case 2:
                return "Studio Clean (Light)";
            case 3:
                return "High Contrast Pro (Dark)";
            case 0:
            default:
                return "Sketch (Light)";
        }
    }

    static Theme byIndex(int index) noexcept
    {
        switch (index)
        {
            case 1:
                return dark();
            case 2:
                return studioClean();
            case 3:
                return highContrast();
            case 0:
            default:
                return sketch();
        }
    }
};

//==============================================================================
/** Global UI presentation controls (font scale & clean high-readability skin mode). */
inline std::atomic<float> &globalUiTextScale() noexcept
{
    static std::atomic<float> scale {1.0f};
    return scale;
}

inline std::atomic<bool> &globalCleanStyle() noexcept
{
    static std::atomic<bool> clean {false};
    return clean;
}

inline void setUiTextScale(float scale) noexcept
{
    globalUiTextScale().store(juce::jlimit(0.85f, 1.35f, scale), std::memory_order_relaxed);
}

inline float getUiTextScale() noexcept
{
    return globalUiTextScale().load(std::memory_order_relaxed);
}

inline void setCleanStyleEnabled(bool enabled) noexcept
{
    globalCleanStyle().store(enabled, std::memory_order_relaxed);
}

inline bool isCleanStyleEnabled() noexcept
{
    return globalCleanStyle().load(std::memory_order_relaxed);
}

//==============================================================================
/** Loads one of the bundled fonts. */
inline juce::Typeface::Ptr uiTypeface()
{
    static const juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor(
        GlorpAssets::PatrickHandRegular_ttf, GlorpAssets::PatrickHandRegular_ttfSize);

    return face;
}

inline juce::Typeface::Ptr displayTypeface()
{
    static const juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor(
        GlorpAssets::LilitaOneRegular_ttf, GlorpAssets::LilitaOneRegular_ttfSize);

    return face;
}

inline juce::Font uiFont(float height)
{
    const auto size = juce::jmax(9.0f, height * getUiTextScale());

    if (isCleanStyleEnabled())
        return juce::Font(juce::FontOptions(size));

    if (auto face = uiTypeface())
    {
        juce::Font font(face);
        font.setHeight(size);
        return font;
    }

    return juce::Font(juce::FontOptions(size));
}

inline juce::Font displayFont(float height)
{
    const auto size = juce::jmax(10.5f, height * getUiTextScale());

    if (isCleanStyleEnabled())
        return juce::Font(juce::FontOptions(size).withStyle("Bold"));

    if (auto face = displayTypeface())
    {
        juce::Font font(face);
        font.setHeight(size);
        return font;
    }

    return juce::Font(juce::FontOptions(size).withStyle("Bold"));
}

/// The bundled logo. Parsed fresh each call, because it is only ever needed once
/// per editor and an SVG parse is not worth caching across plugin instances.
inline std::unique_ptr<juce::Drawable> logoDrawable()
{
    auto root = juce::parseXML(juce::String::fromUTF8(GlorpAssets::logo_svg, GlorpAssets::logo_svgSize));

    if (root != nullptr)
        return juce::Drawable::createFromSVG(*root);

    return {};
}

inline juce::Image imageFromMemory(const char *data, int size)
{
    if (data == nullptr || size <= 0)
        return {};
    return juce::ImageFileFormat::loadFrom(data, static_cast<size_t>(size));
}

//==============================================================================
/** Draws a rectangle outline with a hand-drawn wobble.
    A perfectly straight line is what makes a UI look like it came out of a
    plugin, so every outline gets a deterministic per-edge jitter. Deterministic
    matters: the same control must not shimmer when redrawn.
*/
inline void drawSketchRect(
    juce::Graphics &g, juce::Rectangle<float> r, const juce::Colour &colour, float thickness = 1.6f, unsigned seed = 0)
{
    if (isCleanStyleEnabled())
    {
        g.setColour(colour);
        g.drawRoundedRectangle(r, 4.0f, thickness);
        return;
    }

    auto jitter = [](unsigned s, int index, float amount)
    {
        // Cheap integer hash, stable for a given seed and edge.
        auto h = static_cast<unsigned>(index) * 374761393u + s * 668265263u + 1442695040u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h = h ^ (h >> 16);
        return (static_cast<float>(h & 0xFFFF) / 32767.5f - 1.0f) * amount;
    };

    juce::Path path;
    constexpr int segmentsPerEdge = 4;
    float travelled = 0.0f;

    for (int edge = 0; edge < 4; ++edge)
    {
        const auto from = [&](float t)
        {
            switch (edge)
            {
                case 0:
                    return r.getX() + t * r.getWidth();
                case 1:
                    return r.getY() + t * r.getHeight();
                case 2:
                    return r.getRight() - t * r.getWidth();
                default:
                    return r.getBottom() - t * r.getHeight();
            }
        };

        const bool horizontal = (edge == 0 || edge == 2);

        for (int s = 0; s <= segmentsPerEdge; ++s)
        {
            const auto t = static_cast<float>(s) / segmentsPerEdge;
            const auto base = from(t);
            const auto nudge = jitter(seed, edge * 8 + s, std::min(1.4f, r.getWidth() * 0.012f));

            const juce::Point<float> point =
                horizontal ? juce::Point<float>(base, r.getY() + (edge == 0 ? 0.0f : r.getHeight()) + nudge)
                           : juce::Point<float>(r.getX() + (edge == 3 ? 0.0f : r.getWidth()) + nudge, base);

            if (edge == 0 && s == 0)
                path.startNewSubPath(point);
            else
                path.lineTo(point);
        }

        travelled += horizontal ? r.getWidth() : r.getHeight();
        juce::ignoreUnused(travelled);
    }

    path.closeSubPath();

    g.setColour(colour);
    g.strokePath(path, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace glorp
