/*
    MinimalInstrumentGrid.h - Flat hardware-style pad/instrument grid for Minimal skins.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

#include "Theme.h"

namespace glorp
{

class MinimalInstrumentGrid : public juce::Component
{
  public:
    struct Item
    {
        juce::String name;
        juce::String activeZoneName;
        juce::String midiNoteText;
        int zoneCount = 1;
        int outputBus = 0;
        float hitLevel = 0.0f;
        float peakFlash = 0.0f;
    };

    MinimalInstrumentGrid();
    ~MinimalInstrumentGrid() override = default;

    void setTheme(const Theme &newTheme);
    void setItems(std::vector<Item> newItems);
    void setSelectedIndex(int index);
    int getSelectedIndex() const noexcept { return selectedIndex; }
    int getItemCount() const noexcept { return static_cast<int>(items.size()); }

    void triggerItemHit(int logicalIndex, float velocity);
    bool tickAnimations();

    std::function<void(int logicalIndex, float velocity)> onCellClicked;

    void paint(juce::Graphics &g) override;
    void resized() override;

  private:
    class GridContent : public juce::Component
    {
      public:
        explicit GridContent(MinimalInstrumentGrid &ownerGrid) : owner(ownerGrid) {}

        void paint(juce::Graphics &g) override;
        void mouseMove(const juce::MouseEvent &e) override;
        void mouseExit(const juce::MouseEvent &e) override;
        void mouseDown(const juce::MouseEvent &e) override;
        void layoutCells(int viewportWidth, int viewportHeight);

        std::vector<juce::Rectangle<float>> cellBounds;
        int hoveredIndex = -1;

      private:
        int hitTestCell(juce::Point<float> p) const noexcept;
        MinimalInstrumentGrid &owner;
    };

    juce::Colour cellAccentColour(size_t index) const noexcept;

    Theme theme = Theme::minimalDark();
    std::vector<Item> items;
    int selectedIndex = 0;

    juce::Viewport viewport;
    std::unique_ptr<GridContent> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MinimalInstrumentGrid)
};

} // namespace glorp
