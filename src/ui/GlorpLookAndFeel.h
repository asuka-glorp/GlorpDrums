/*
    GlorpLookAndFeel.h - Theme-aware JUCE LookAndFeel for dropdown menus,
    Standalone title bar + Options button, and standard dialog widgets.
*/

#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>

namespace glorp
{

class GlorpLookAndFeel final : public juce::LookAndFeel_V4
{
  public:
    GlorpLookAndFeel() { setTheme(Theme::sketch()); }

    void setTheme(const Theme &newTheme)
    {
        theme = newTheme;

        const juce::Colour windowBg = theme.background;
        const juce::Colour widgetBg = theme.panel;
        const juce::Colour menuBg = theme.panel;
        const juce::Colour outline = theme.panelEdge;
        const juce::Colour textCol = theme.ink;
        const juce::Colour accentCol = theme.accent;
        const juce::Colour highlightText = contrastTextFor(accentCol);
        const juce::Colour buttonFill =
            theme.minimalStyle ? theme.trough.brighter(theme.isDark ? 0.14f : 0.0f) : theme.panel;

        setColourScheme({windowBg, widgetBg, menuBg, outline, textCol, accentCol, highlightText, accentCol, textCol});

        setColour(juce::ResizableWindow::backgroundColourId, windowBg);
        setColour(juce::DocumentWindow::textColourId, textCol);

        setColour(juce::PopupMenu::backgroundColourId, menuBg);
        setColour(juce::PopupMenu::textColourId, textCol);
        setColour(juce::PopupMenu::headerTextColourId, theme.inkSoft);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, accentCol);
        setColour(juce::PopupMenu::highlightedTextColourId, highlightText);

        setColour(juce::TextButton::buttonColourId, buttonFill);
        setColour(juce::TextButton::buttonOnColourId, accentCol);
        setColour(juce::TextButton::textColourOffId, textCol);
        setColour(juce::TextButton::textColourOnId, highlightText);

        setColour(juce::ComboBox::backgroundColourId, theme.minimalStyle ? theme.trough : theme.panel);
        setColour(juce::ComboBox::textColourId, textCol);
        setColour(juce::ComboBox::outlineColourId, outline);
        setColour(juce::ComboBox::buttonColourId, buttonFill);
        setColour(juce::ComboBox::arrowColourId, theme.inkSoft);

        setColour(juce::AlertWindow::backgroundColourId, menuBg);
        setColour(juce::AlertWindow::textColourId, textCol);
        setColour(juce::AlertWindow::outlineColourId, outline);

        setColour(juce::Label::textColourId, textCol);
        setColour(juce::TextEditor::backgroundColourId, theme.minimalStyle ? theme.trough : theme.background);
        setColour(juce::TextEditor::textColourId, textCol);
        setColour(juce::TextEditor::outlineColourId, outline);
        setColour(juce::TextEditor::focusedOutlineColourId, accentCol);

        setColour(juce::ListBox::backgroundColourId, menuBg);
        setColour(juce::ListBox::textColourId, textCol);
        setColour(juce::ListBox::outlineColourId, outline);

        setColour(juce::ScrollBar::thumbColourId, outline);
        setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    }

    const Theme &getTheme() const noexcept { return theme; }

    juce::Font getPopupMenuFont() override { return uiFont(13.5f); }

    int getPopupMenuBorderSize() override { return 2; }

    void drawPopupMenuBackground(juce::Graphics &g, int width, int height) override
    {
        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        g.setColour(theme.panel);
        g.fillRect(bounds);

        if (theme.minimalStyle || theme.cleanStyle)
        {
            g.setColour(theme.panelEdge);
            g.drawRect(bounds.reduced(0.5f), 1.0f);
        }
        else
        {
            drawSketchRect(g, bounds.reduced(1.0f), theme.panelEdge, 1.8f, 11);
        }
    }

    void drawPopupMenuSectionHeader(juce::Graphics &g,
                                    const juce::Rectangle<int> &area,
                                    const juce::String &sectionName) override
    {
        g.setFont(uiFont(11.0f).boldened());
        g.setColour(theme.inkSoft);
        g.drawFittedText(sectionName.toUpperCase(),
                         area.getX() + 10,
                         area.getY(),
                         area.getWidth() - 16,
                         area.getHeight(),
                         juce::Justification::bottomLeft,
                         1);
    }

    void drawPopupMenuItem(juce::Graphics &g,
                           const juce::Rectangle<int> &area,
                           bool isSeparator,
                           bool isActive,
                           bool isHighlighted,
                           bool isTicked,
                           bool hasSubMenu,
                           const juce::String &text,
                           const juce::String &shortcutKeyText,
                           const juce::Drawable *icon,
                           const juce::Colour *textColourToUse) override
    {
        if (isSeparator)
        {
            auto r = area.reduced(8, 0);
            r.removeFromTop(juce::roundToInt((static_cast<float>(r.getHeight()) * 0.5f) - 0.5f));
            g.setColour(theme.panelEdge.withAlpha(theme.minimalStyle ? 0.85f : 0.65f));
            g.fillRect(r.removeFromTop(1));
            return;
        }

        const juce::Colour baseTextCol = (textColourToUse != nullptr) ? *textColourToUse : theme.ink;
        const juce::Colour hiTextCol = contrastTextFor(theme.accent);

        auto r = area.reduced(4, 1);
        if (isHighlighted && isActive)
        {
            g.setColour(theme.accent);
            g.fillRoundedRectangle(r.toFloat(), theme.minimalStyle ? 4.5f : 3.5f);
            g.setColour(hiTextCol);
        }
        else
        {
            g.setColour(baseTextCol.withMultipliedAlpha(isActive ? 1.0f : 0.48f));
        }

        auto contentArea = area.reduced(1);
        contentArea.reduce(juce::jmin(6, area.getWidth() / 18), 0);

        auto font = getPopupMenuFont();
        const float maxFontHeight = static_cast<float>(contentArea.getHeight()) / 1.3f;
        if (font.getHeight() > maxFontHeight)
            font.setHeight(maxFontHeight);
        g.setFont(font);

        const auto iconArea = contentArea.removeFromLeft(juce::roundToInt(maxFontHeight)).toFloat();
        if (icon != nullptr)
        {
            icon->drawWithin(
                g, iconArea, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
            contentArea.removeFromLeft(juce::roundToInt(maxFontHeight * 0.45f));
        }
        else if (isTicked)
        {
            const auto tick = getTickShape(1.0f);
            const juce::Colour tickCol = (isHighlighted && isActive) ? hiTextCol : theme.accent;
            g.setColour(tickCol.withMultipliedAlpha(isActive ? 1.0f : 0.48f));
            g.fillPath(tick, tick.getTransformToScaleToFit(iconArea.reduced(iconArea.getWidth() * 0.22f, 0.0f), true));
            g.setColour((isHighlighted && isActive) ? hiTextCol
                                                    : baseTextCol.withMultipliedAlpha(isActive ? 1.0f : 0.48f));
        }

        if (hasSubMenu)
        {
            const float arrowH = 0.56f * font.getAscent();
            const float x = static_cast<float>(contentArea.removeFromRight(static_cast<int>(arrowH) + 2).getX());
            const float halfH = static_cast<float>(contentArea.getCentreY());

            juce::Path path;
            path.startNewSubPath(x, halfH - arrowH * 0.5f);
            path.lineTo(x + arrowH * 0.6f, halfH);
            path.lineTo(x, halfH + arrowH * 0.5f);
            g.strokePath(path, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        contentArea.removeFromRight(4);
        g.drawFittedText(text, contentArea, juce::Justification::centredLeft, 1);

        if (shortcutKeyText.isNotEmpty())
        {
            auto f2 = font;
            f2.setHeight(f2.getHeight() * 0.78f);
            g.setFont(f2);
            if (!(isHighlighted && isActive))
                g.setColour(theme.inkSoft.withMultipliedAlpha(isActive ? 1.0f : 0.48f));
            g.drawText(shortcutKeyText, contentArea, juce::Justification::centredRight, true);
        }
    }

    void getIdealPopupMenuItemSize(const juce::String &text,
                                   bool isSeparator,
                                   int standardMenuItemHeight,
                                   int &idealWidth,
                                   int &idealHeight) override
    {
        if (isSeparator)
        {
            idealWidth = 50;
            idealHeight = standardMenuItemHeight > 0 ? juce::jmax(8, standardMenuItemHeight / 3) : 9;
            return;
        }

        auto font = getPopupMenuFont();
        if (standardMenuItemHeight > 0 && font.getHeight() > static_cast<float>(standardMenuItemHeight) / 1.3f)
            font.setHeight(static_cast<float>(standardMenuItemHeight) / 1.3f);

        idealHeight = standardMenuItemHeight > 0 ? standardMenuItemHeight : juce::roundToInt(font.getHeight() * 1.48f);
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, text, 0.0f, 0.0f);
        idealWidth = juce::roundToInt(ga.getBoundingBox(0, -1, true).getWidth()) + idealHeight * 2 + 12;
    }

    void drawDocumentWindowTitleBar(juce::DocumentWindow &window,
                                    juce::Graphics &g,
                                    int w,
                                    int h,
                                    int titleSpaceX,
                                    int titleSpaceW,
                                    const juce::Image *icon,
                                    bool drawTitleTextOnLeft) override
    {
        if (w <= 0 || h <= 0)
            return;

        const bool isActive = window.isActiveWindow();
        const juce::Colour titleBg =
            theme.minimalStyle ? theme.panel.darker(theme.isDark ? 0.18f : 0.05f) : theme.panel;

        g.fillAll(titleBg);
        g.setColour(theme.panelEdge);
        g.fillRect(0, h - 1, w, 1);

        const float fontSz = std::clamp(static_cast<float>(h) * 0.56f, 12.0f, 16.0f);
        const juce::Font font =
            (theme.minimalStyle || theme.cleanStyle) ? uiFont(fontSz).boldened() : displayFont(fontSz + 1.0f);
        g.setFont(font);

        juce::GlyphArrangement ga;
        ga.addLineOfText(font, window.getName(), 0.0f, 0.0f);
        int textW = juce::roundToInt(ga.getBoundingBox(0, -1, true).getWidth()) + 8;
        int iconW = 0;
        int iconH = 0;

        if (icon != nullptr && icon->isValid() && icon->getHeight() > 0)
        {
            iconH = juce::roundToInt(font.getHeight() * 1.05f);
            iconW = icon->getWidth() * iconH / icon->getHeight() + 6;
        }

        textW = juce::jmin(titleSpaceW, textW + iconW);
        int textX = drawTitleTextOnLeft ? titleSpaceX : juce::jmax(titleSpaceX, (w - textW) / 2);

        if (textX + textW > titleSpaceX + titleSpaceW)
            textX = titleSpaceX + titleSpaceW - textW;

        if (icon != nullptr && icon->isValid() && iconW > 0)
        {
            g.setOpacity(isActive ? 1.0f : 0.65f);
            g.drawImageWithin(
                *icon, textX, (h - iconH) / 2, iconW - 6, iconH, juce::RectanglePlacement::centred, false);
            g.setOpacity(1.0f);
            textX += iconW;
            textW -= iconW;
        }

        if (window.isColourSpecified(juce::DocumentWindow::textColourId)
            || isColourSpecified(juce::DocumentWindow::textColourId))
            g.setColour(
                window.findColour(juce::DocumentWindow::textColourId).withMultipliedAlpha(isActive ? 1.0f : 0.65f));
        else
            g.setColour((isActive ? theme.ink : theme.inkSoft));

        g.drawText(window.getName(), textX, 0, juce::jmax(0, textW), h, juce::Justification::centredLeft, true);
    }

    juce::Button *createDocumentWindowButton(int buttonType) override
    {
        return new ThemedWindowButton(*this, buttonType);
    }

    juce::Font getTextButtonFont(juce::TextButton &, int buttonHeight) override
    {
        const float size = std::clamp(static_cast<float>(buttonHeight) * 0.56f, 10.5f, 14.0f);
        return uiFont(size).boldened();
    }

    void drawButtonBackground(juce::Graphics &g,
                              juce::Button &button,
                              const juce::Colour &backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const float corner = theme.minimalStyle ? 5.0f : (theme.cleanStyle ? 4.0f : 3.5f);

        juce::Colour fill = backgroundColour;
        if (button.getToggleState())
            fill = theme.accent;
        if (shouldDrawButtonAsDown)
            fill = theme.isDark ? fill.brighter(0.18f) : fill.darker(0.12f);
        else if (shouldDrawButtonAsHighlighted)
            fill = theme.isDark ? fill.brighter(0.10f) : fill.darker(0.05f);

        g.setColour(fill);
        g.fillRoundedRectangle(bounds, corner);

        const juce::Colour borderCol =
            (shouldDrawButtonAsHighlighted || button.getToggleState()) ? theme.accent : theme.panelEdge;
        if (theme.minimalStyle || theme.cleanStyle)
        {
            g.setColour(borderCol);
            g.drawRoundedRectangle(bounds.reduced(0.5f), corner, 1.0f);
        }
        else
        {
            drawSketchRect(g, bounds.reduced(0.5f), borderCol, shouldDrawButtonAsHighlighted ? 1.7f : 1.3f, 5);
        }
    }

    void drawButtonText(juce::Graphics &g,
                        juce::TextButton &button,
                        bool /*shouldDrawButtonAsHighlighted*/,
                        bool /*shouldDrawButtonAsDown*/) override
    {
        const auto font = getTextButtonFont(button, button.getHeight());
        g.setFont(font);

        const bool toggled = button.getToggleState();
        const juce::Colour textCol =
            toggled ? contrastTextFor(theme.accent) : button.findColour(juce::TextButton::textColourOffId);

        g.setColour(textCol.withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.45f));

        const int yIndent = juce::jmin(4, button.proportionOfHeight(0.22f));
        const int leftIndent = juce::jmin(font.getHeight() * 0.4f, 6.0f);
        const int textWidth = button.getWidth() - leftIndent * 2;
        if (textWidth > 0)
        {
            g.drawFittedText(button.getButtonText(),
                             leftIndent,
                             yIndent,
                             textWidth,
                             button.getHeight() - yIndent * 2,
                             juce::Justification::centred,
                             1);
        }
    }

  private:
    class ThemedWindowButton final : public juce::Button
    {
      public:
        ThemedWindowButton(const GlorpLookAndFeel &ownerLaf, int type)
            : juce::Button(buttonNameFor(type)), laf(ownerLaf), buttonType(type)
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }

        void paintButton(juce::Graphics &g, bool isHover, bool isDown) override
        {
            const Theme &t = laf.getTheme();
            const juce::Colour titleBg = t.minimalStyle ? t.panel.darker(t.isDark ? 0.18f : 0.05f) : t.panel;
            g.fillAll(titleBg);
            g.setColour(t.panelEdge);
            g.fillRect(0, getHeight() - 1, getWidth(), 1);

            const auto pill = getLocalBounds().toFloat().reduced(4.0f, 4.0f);
            const bool isClose = (buttonType == juce::DocumentWindow::closeButton);

            if (isHover || isDown)
            {
                const juce::Colour hoverBg = isClose ? juce::Colour(0xffd94848)
                                                     : (t.minimalStyle ? t.trough.brighter(t.isDark ? 0.25f : -0.05f)
                                                                       : t.accent.withAlpha(0.24f));
                g.setColour(isDown ? hoverBg.darker(0.15f) : hoverBg);
                g.fillRoundedRectangle(pill, 4.5f);
            }

            const juce::Colour glyphCol = (isClose && (isHover || isDown))
                                              ? juce::Colours::white
                                              : (isClose ? juce::Colour(0xffe06c75) : t.inkSoft);
            g.setColour(glyphCol.withMultipliedAlpha(isEnabled() ? 1.0f : 0.45f));

            const float side = std::min(pill.getWidth(), pill.getHeight()) * 0.28f;
            const auto c = pill.getCentre();
            const float strokeW = (t.minimalStyle || t.cleanStyle) ? 1.55f : 1.85f;

            if (buttonType == juce::DocumentWindow::closeButton)
            {
                g.drawLine(c.x - side, c.y - side, c.x + side, c.y + side, strokeW);
                g.drawLine(c.x + side, c.y - side, c.x - side, c.y + side, strokeW);
            }
            else if (buttonType == juce::DocumentWindow::minimiseButton)
            {
                g.drawLine(c.x - side, c.y + side * 0.35f, c.x + side, c.y + side * 0.35f, strokeW);
            }
            else
            {
                g.drawRect(c.x - side, c.y - side, side * 2.0f, side * 2.0f, strokeW);
            }
        }

      private:
        static juce::String buttonNameFor(int type)
        {
            if (type == juce::DocumentWindow::closeButton)
                return "close";
            if (type == juce::DocumentWindow::minimiseButton)
                return "minimise";
            return "maximise";
        }

        const GlorpLookAndFeel &laf;
        int buttonType = juce::DocumentWindow::closeButton;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedWindowButton)
    };

    static juce::Colour contrastTextFor(juce::Colour bg) noexcept
    {
        return bg.getPerceivedBrightness() > 0.62f ? juce::Colour(0xff101114) : juce::Colours::white;
    }

    Theme theme = Theme::sketch();
};

} // namespace glorp
