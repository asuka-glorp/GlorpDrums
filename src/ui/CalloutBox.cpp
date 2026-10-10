#include "CalloutBox.h"

namespace glorp
{

CalloutBox::CalloutBox()
{
    setOpaque (false);
    setInterceptsMouseClicks (true, true);
}

void CalloutBox::setInstrumentName (const juce::String& name)
{
    instrumentName = name;
    repaint();
}

void CalloutBox::setSampleName (const juce::String& name)
{
    sampleName = name;
    repaint();
}

void CalloutBox::setNoteLabel (const juce::String& label)
{
    noteLabel = label;
    repaint();
}

void CalloutBox::setAutoEnabled (bool enabled)
{
    autoEnabled = enabled;
    repaint();
}

juce::Point<float> CalloutBox::getLeaderStartPoint (juce::Component* parent) const
{
    const auto b = getBounds().toFloat();
    if (parent == nullptr)
        return b.getCentre();

    // Attach to left or right edge depending on anchor position
    if (anchor.x < b.getCentreX())
        return { b.getX(), b.getCentreY() };

    return { b.getRight(), b.getCentreY() };
}

void CalloutBox::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (2.0f);

    // 1. Box background & hand-drawn border
    g.setColour (isSelected ? juce::Colour (0xfffffdf4) : theme.panel);
    g.fillRect (b);

    // Outer sketch rect
    const auto borderCol = isSelected ? theme.accent : theme.panelEdge;
    drawSketchRect (g, b, borderCol, isSelected ? 2.4f : 1.7f, 7);

    // Secondary inner border for hand-drawn comic look
    drawSketchRect (g, b.reduced (2.0f), borderCol.withAlpha (isSelected ? 0.8f : 0.4f), 1.0f, 13);

    auto content = b.reduced (4.0f);

    // 2. Top Header: "SELECT"
    const float titleH = 14.0f;
    auto titleArea = content.removeFromTop (titleH);
    g.setFont (uiFont (12.0f));
    g.setColour (isSelected ? theme.accent : theme.inkSoft);
    g.drawText ("SELECT", titleArea, juce::Justification::centredLeft, false);

    content.removeFromTop (2.0f);

    // 3. Dropdown Name Button: "[INSTRUMENT NAME ▲▼]"
    const float selectH = juce::jmin (20.0f, content.getHeight() * 0.36f);
    selectButtonBounds = content.removeFromTop (selectH);

    g.setColour (theme.panel);
    g.fillRect (selectButtonBounds);
    drawSketchRect (g, selectButtonBounds, theme.ink, 1.3f, 9);

    const auto displayName = (sampleName.isNotEmpty() ? sampleName : instrumentName).toUpperCase();
    g.setFont (uiFont (selectH * 0.72f));
    g.setColour (theme.ink);

    auto textArea = selectButtonBounds.reduced (4.0f, 0.0f);
    g.drawText (displayName, textArea.withTrimmedRight (16.0f), juce::Justification::centredLeft, true);
    g.drawText ("▲▼", textArea.removeFromRight (14.0f), juce::Justification::centredRight, false);

    content.removeFromTop (3.0f);

    // 4. Middle Row: [AUTO] [LEARN] buttons
    const float btnH = juce::jmin (18.0f, content.getHeight() * 0.52f);
    auto btnRow = content.removeFromTop (btnH);
    const float btnW = (btnRow.getWidth() - 4.0f) * 0.5f;

    autoButtonBounds = btnRow.withWidth (btnW);
    learnButtonBounds = btnRow.withTrimmedLeft (btnW + 4.0f);

    auto drawBtn = [&] (juce::Rectangle<float> rect, const juce::String& text, bool active)
    {
        g.setColour (active ? theme.accent.withAlpha (0.35f) : theme.panel);
        g.fillRect (rect);
        drawSketchRect (g, rect, active ? theme.accent : theme.ink, 1.2f, 11);

        g.setColour (active ? theme.ink : theme.inkSoft);
        g.setFont (uiFont (btnH * 0.70f));
        g.drawText (text, rect, juce::Justification::centred, false);
    };

    drawBtn (autoButtonBounds, "AUTO", autoEnabled);
    drawBtn (learnButtonBounds, "LEARN", learnArmed);

    content.removeFromTop (2.0f);

    // 5. Bottom Row: MIDI readout
    midiRowBounds = content;
    if (midiRowBounds.getHeight() >= 8.0f)
    {
        g.setColour (learnArmed ? theme.accent : (isSelected ? theme.ink : theme.inkSoft));
        g.setFont (uiFont (juce::jmin (13.0f, midiRowBounds.getHeight() * 0.95f)));
        g.drawText (learnArmed ? "listening..." : ("MIDI: " + noteLabel),
                    midiRowBounds.reduced (2.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

void CalloutBox::mouseDown (const juce::MouseEvent&)
{
    if (onBoxClicked != nullptr)
        onBoxClicked();
}

void CalloutBox::mouseUp (const juce::MouseEvent& e)
{
    if (selectButtonBounds.contains (e.position))
    {
        if (onSelectClicked != nullptr)
            onSelectClicked();
        return;
    }

    if (autoButtonBounds.contains (e.position))
    {
        if (onAutoClicked != nullptr)
            onAutoClicked();
        return;
    }

    if (learnButtonBounds.contains (e.position))
    {
        if (onLearnClicked != nullptr)
            onLearnClicked();
        return;
    }

    if (midiRowBounds.contains (e.position))
    {
        if (onMidiLabelClicked != nullptr)
            onMidiLabelClicked();
        return;
    }
}

} // namespace glorp
