/*
    MinimalInstrumentGrid.cpp - Flat hardware-style pad/instrument grid for Minimal skins.
*/

#include "MinimalInstrumentGrid.h"

#include <algorithm>
#include <cmath>

namespace glorp
{

MinimalInstrumentGrid::MinimalInstrumentGrid()
{
    content = std::make_unique<GridContent>(*this);
    viewport.setViewedComponent(content.get(), false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
}

void MinimalInstrumentGrid::setTheme(const Theme &newTheme)
{
    theme = newTheme;
    viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, theme.inkSoft.withAlpha(0.45f));
    repaint();
    if (content != nullptr)
        content->repaint();
}

void MinimalInstrumentGrid::setItems(std::vector<Item> newItems)
{
    // Preserve any active hit flash across parameter/zone refreshes when size matches
    if (newItems.size() == items.size())
    {
        for (size_t i = 0; i < newItems.size(); ++i)
        {
            newItems[i].hitLevel = items[i].hitLevel;
            newItems[i].peakFlash = items[i].peakFlash;
        }
    }

    items = std::move(newItems);
    if (items.empty())
        selectedIndex = -1;
    else
        selectedIndex = juce::jlimit(0, static_cast<int>(items.size()) - 1, std::max(0, selectedIndex));

    resized();
    if (content != nullptr)
        content->repaint();
}

void MinimalInstrumentGrid::setSelectedIndex(int index)
{
    const int clamped = items.empty() ? -1 : juce::jlimit(0, static_cast<int>(items.size()) - 1, index);
    if (selectedIndex != clamped)
    {
        selectedIndex = clamped;
        if (content != nullptr)
            content->repaint();
    }
}

void MinimalInstrumentGrid::triggerItemHit(int logicalIndex, float velocity)
{
    if (logicalIndex < 0 || logicalIndex >= static_cast<int>(items.size()))
        return;

    auto &it = items[static_cast<size_t>(logicalIndex)];
    const float v = juce::jlimit(0.15f, 1.0f, velocity);
    it.hitLevel = std::max(it.hitLevel, v);
    it.peakFlash = 1.0f;
    if (content != nullptr)
        content->repaint();
}

bool MinimalInstrumentGrid::tickAnimations()
{
    bool anyActive = false;
    for (auto &it : items)
    {
        if (it.hitLevel > 0.004f || it.peakFlash > 0.004f)
        {
            it.hitLevel *= 0.84f;
            it.peakFlash *= 0.78f;
            if (it.hitLevel < 0.004f)
                it.hitLevel = 0.0f;
            if (it.peakFlash < 0.004f)
                it.peakFlash = 0.0f;
            anyActive = true;
        }
    }

    if (anyActive && content != nullptr)
        content->repaint();

    return anyActive;
}

juce::Colour MinimalInstrumentGrid::cellAccentColour(size_t index) const noexcept
{
    switch (index % 4)
    {
        case 0:
            return theme.accent;
        case 1:
            return theme.cymbal;
        case 2:
            return theme.accentAlt;
        default:
            return theme.accentWarm;
    }
}

void MinimalInstrumentGrid::paint(juce::Graphics &g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour(theme.trough.withAlpha(theme.isDark ? 0.45f : 0.55f));
    g.fillRoundedRectangle(r, 7.0f);
    g.setColour(theme.panelEdge.withAlpha(0.75f));
    g.drawRoundedRectangle(r.reduced(0.5f), 7.0f, 1.0f);
}

void MinimalInstrumentGrid::resized()
{
    const auto inner = getLocalBounds().reduced(6);
    viewport.setBounds(inner);
    if (content != nullptr)
        content->layoutCells(inner.getWidth(), inner.getHeight());
}

//==============================================================================
void MinimalInstrumentGrid::GridContent::layoutCells(int viewportWidth, int viewportHeight)
{
    cellBounds.clear();
    const int n = static_cast<int>(owner.items.size());
    if (n == 0 || viewportWidth <= 20)
    {
        setSize(std::max(1, viewportWidth), std::max(1, viewportHeight));
        return;
    }

    const int cols = (n <= 4) ? n : juce::jlimit(3, 5, viewportWidth / 168);
    const int rows = (n + cols - 1) / cols;
    const float gap = 10.0f;
    const float pad = 4.0f;
    const float sbReserve = (rows * 108 > viewportHeight) ? 12.0f : 0.0f;
    const float availW = std::max(120.0f, static_cast<float>(viewportWidth) - pad * 2.0f - sbReserve);
    const float cellW = (availW - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);

    const float availH = std::max(96.0f, static_cast<float>(viewportHeight) - pad * 2.0f);
    const float idealH = (availH - gap * static_cast<float>(rows - 1)) / static_cast<float>(rows);
    const float cellH = juce::jlimit(92.0f, 138.0f, idealH);

    const float totalH = std::max(static_cast<float>(viewportHeight), pad * 2.0f + rows * cellH + (rows - 1) * gap);
    setSize(std::max(1, viewportWidth - static_cast<int>(sbReserve)), static_cast<int>(std::ceil(totalH)));

    cellBounds.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const int r = i / cols;
        const int c = i % cols;
        const float x = pad + static_cast<float>(c) * (cellW + gap);
        const float y = pad + static_cast<float>(r) * (cellH + gap);
        cellBounds.emplace_back(x, y, cellW, cellH);
    }
}

int MinimalInstrumentGrid::GridContent::hitTestCell(juce::Point<float> p) const noexcept
{
    for (size_t i = 0; i < cellBounds.size(); ++i)
        if (cellBounds[i].contains(p))
            return static_cast<int>(i);
    return -1;
}

void MinimalInstrumentGrid::GridContent::mouseMove(const juce::MouseEvent &e)
{
    const int hit = hitTestCell(e.position);
    if (hit != hoveredIndex)
    {
        hoveredIndex = hit;
        setMouseCursor(hit >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void MinimalInstrumentGrid::GridContent::mouseExit(const juce::MouseEvent &)
{
    if (hoveredIndex != -1)
    {
        hoveredIndex = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void MinimalInstrumentGrid::GridContent::mouseDown(const juce::MouseEvent &e)
{
    const int hit = hitTestCell(e.position);
    if (hit < 0 || hit >= static_cast<int>(owner.items.size()))
        return;

    const auto &cell = cellBounds[static_cast<size_t>(hit)];
    const float relY = juce::jlimit(0.0f, 1.0f, (e.position.y - cell.getY()) / std::max(1.0f, cell.getHeight()));
    const float vel = juce::jlimit(0.45f, 1.0f, 0.55f + 0.45f * relY);

    owner.setSelectedIndex(hit);
    owner.triggerItemHit(hit, vel);
    if (owner.onCellClicked != nullptr)
        owner.onCellClicked(hit, vel);
}

void MinimalInstrumentGrid::GridContent::paint(juce::Graphics &g)
{
    const auto &items = owner.items;
    const auto &t = owner.theme;

    if (items.empty())
    {
        g.setFont(uiFont(14.0f));
        g.setColour(t.inkSoft);
        g.drawText("NO INSTRUMENTS LOADED", getLocalBounds().toFloat(), juce::Justification::centred, false);
        return;
    }

    for (size_t i = 0; i < items.size() && i < cellBounds.size(); ++i)
    {
        const auto &item = items[i];
        const auto r = cellBounds[i];
        const bool isSel = (static_cast<int>(i) == owner.selectedIndex);
        const bool isHov = (static_cast<int>(i) == hoveredIndex);
        const auto itemCol = owner.cellAccentColour(i);

        juce::Colour borderCol = t.panelEdge;
        float borderW = 1.0f;
        if (isSel)
        {
            borderCol = t.accent;
            borderW = 1.8f;
        }
        else if (item.peakFlash > 0.15f)
        {
            borderCol = itemCol.interpolatedWith(t.panelEdge, 1.0f - item.peakFlash);
            borderW = 1.5f;
        }
        else if (isHov)
        {
            borderCol = t.accent.withAlpha(0.6f);
            borderW = 1.3f;
        }

        drawMinimalCard(g, r, t, 6.0f, borderCol, borderW);

        // Hit flash wash
        if (item.peakFlash > 0.01f)
        {
            g.setColour(itemCol.withAlpha(0.14f * item.peakFlash));
            g.fillRoundedRectangle(r.reduced(1.5f), 5.0f);
        }

        const auto inner = r.reduced(10.0f, 8.0f);

        // Top row: LED dot + Zone count / BUS badge + Note pill
        const float topH = 18.0f;
        const auto topRow = inner.withHeight(topH);

        // Status / Hit LED
        const float ledSize = 7.0f;
        const auto ledRect =
            juce::Rectangle<float>(topRow.getX(), topRow.getCentreY() - ledSize * 0.5f, ledSize, ledSize);
        g.setColour((isSel || item.peakFlash > 0.08f) ? (item.peakFlash > 0.1f ? itemCol : t.accent)
                                                      : t.inkSoft.withAlpha(0.35f));
        g.fillEllipse(ledRect);

        // Out bus subtle label next to LED
        g.setFont(uiFont(9.5f).boldened());
        g.setColour(t.inkSoft);
        const auto outText = "OUT " + juce::String(item.outputBus * 2 + 1) + "-" + juce::String(item.outputBus * 2 + 2);
        g.drawText(outText,
                   juce::Rectangle<float>(ledRect.getRight() + 6.0f, topRow.getY(), 64.0f, topH),
                   juce::Justification::centredLeft,
                   false);

        // MIDI note pill on top right
        if (item.midiNoteText.isNotEmpty())
        {
            const float pillW = std::min(58.0f, topRow.getWidth() * 0.42f);
            const auto pillRect = juce::Rectangle<float>(topRow.getRight() - pillW, topRow.getY() + 1.0f, pillW, 16.0f);
            g.setColour(t.trough);
            g.fillRoundedRectangle(pillRect, 4.0f);
            g.setFont(uiFont(10.0f).boldened());
            g.setColour(isSel ? t.accent : t.ink);
            g.drawText(item.midiNoteText, pillRect, juce::Justification::centred, false);
        }

        // Bottom meter strip (slanted Rift-style bars)
        const float meterH = 14.0f;
        const auto meterRect =
            juce::Rectangle<float>(inner.getX(), inner.getBottom() - meterH, inner.getWidth(), meterH);
        drawSlantedMeter(g, meterRect, t, item.hitLevel, isSel ? t.accent : itemCol, 12);

        // Middle area: Instrument Title + Active Zone / Count
        const float midTop = topRow.getBottom() + 4.0f;
        const float midBot = meterRect.getY() - 5.0f;
        const auto midArea =
            juce::Rectangle<float>(inner.getX(), midTop, inner.getWidth(), std::max(18.0f, midBot - midTop));

        const float titleH = midArea.getHeight() * 0.58f;
        g.setFont(displayFont(14.5f));
        g.setColour(t.ink);
        g.drawFittedText(item.name.toUpperCase(),
                         midArea.withHeight(titleH).toNearestInt(),
                         juce::Justification::centredLeft,
                         1,
                         0.74f);

        juce::String subText = item.activeZoneName.toUpperCase();
        if (item.zoneCount > 1)
            subText += "  (" + juce::String(item.zoneCount) + " ZONES)";

        g.setFont(uiFont(10.5f));
        g.setColour(isSel ? t.accent : t.inkSoft);
        g.drawFittedText(subText,
                         midArea.withTop(midArea.getY() + titleH).toNearestInt(),
                         juce::Justification::centredLeft,
                         1,
                         0.75f);
    }
}

} // namespace glorp
