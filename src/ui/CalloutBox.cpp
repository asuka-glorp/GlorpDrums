#include "CalloutBox.h"

namespace glorp
{

CalloutBox::CalloutBox()
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);
}

void CalloutBox::setInstrumentName (const juce::String& n)
{
    instrumentName = n;
    repaint();
}

void CalloutBox::setSampleName (const juce::String& n)
{
    sampleName = n;
    repaint();
}

void CalloutBox::setNoteLabel (const juce::String& n)
{
    noteLabel = n;
    repaint();
}

void CalloutBox::setAutoEnabled (bool enabled)
{
    autoEnabled = enabled;
    repaint();
}

void CalloutBox::setAnchor (const juce::Point<float>& a)
{
    anchor = a;
    repaint();
}

void CalloutBox::drawLeaderLine (juce::Graphics& g, const juce::Rectangle<float>& b)
{
    if (anchor.x < -50.0f)
        return;

    const auto parent = getParentComponent();

    if (parent == nullptr)
        return;

    const auto target = parent->getLocalPoint (this, anchor);

    if (target.isFinite() && target.x > -50.0f)
    {
        juce::Point<float> from;

        // Attach to whichever side of the box the target is nearest.
        from.x = target.x < b.getCentreX() ? b.getX() : b.getRight();
        from.y = b.getCentreY();

        // Dashed, because a solid line would compete with the kit drawing.
        juce::Path line;
        line.startNewSubPath (from);
        line.lineTo (target);

        g.setColour (theme.ink.withAlpha (0.7f));
        g.strokePath (line, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Dashes drawn manually: PathStrokeType cannot dash.
        const auto delta = target - from;
        const auto length = delta.getDistanceFromOrigin();
        const auto direction = length > 0.001f ? delta / length : juce::Point<float> (1.0f, 0.0f);

        constexpr float dash = 5.0f;
        constexpr float gap = 4.0f;

        for (float t = 0.0f; t < length; t += dash + gap)
            g.drawLine (juce::Line<float> (from + direction * t,
                                           from + direction * juce::jmin (length, t + dash)), 1.4f);

        g.setColour (theme.ink.withAlpha (0.85f));
        g.fillEllipse (target.x - 2.5f, target.y - 2.5f, 5.0f, 5.0f);
    }
}

void CalloutBox::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);

    drawLeaderLine (g, b);

    drawSketchRect (g, b, theme.panelEdge, 1.7f, 7);

    g.setColour (theme.panel);
    g.fillRect (b.reduced (1.0f));

    const auto titleHeight = juce::jmin (15.0f, b.getHeight() * 0.2f);
    auto content = b.reduced (5.0f);
    auto titleArea = content.removeFromTop (titleHeight);

    g.setColour (muted ? theme.warning : theme.ink);
    g.setFont (uiFont (titleHeight + 2.0f));
    g.drawText (instrumentName, titleArea, juce::Justification::centredLeft, true);

    // SELECT: the sample row, with up/down arrows.
    const auto rowHeight = juce::jmin (22.0f, content.getHeight() * 0.34f);
    auto row = content.removeFromTop (rowHeight).reduced (0.0f, 1.0f);

    const auto labelWidth = row.getWidth() * 0.16f;

    g.setColour (theme.inkSoft);
    g.setFont (uiFont (rowHeight * 0.8f));
    g.drawText ("SELECT", row.removeFromLeft (labelWidth), juce::Justification::centredLeft, false);

    auto sampleBox = row.reduced (2.0f, 2.0f);

    g.setColour (theme.panel);
    g.fillRect (sampleBox);
    drawSketchRect (g, sampleBox, theme.ink.withAlpha (0.75f), 1.3f, 3);

    auto sampleText = sampleBox.reduced (5.0f, 0.0f);
    g.setColour (theme.ink);
    g.setFont (uiFont (rowHeight * 0.75f));
    g.drawText (sampleName, sampleText.withTrimmedRight (16.0f),
                juce::Justification::centredLeft, true);

    // The little up/down triangle pair.
    auto arrows = sampleBox.reduced (0.0f, sampleBox.getHeight() * 0.28f)
                        .withWidth (12.0f)
                        .withX (sampleBox.getRight() - 14.0f);

    juce::Path up, down;
    up.addTriangle (arrows.getCentreX() - 4.0f, arrows.getCentreY() - 2.0f,
                    arrows.getCentreX(), arrows.getCentreY() - 6.0f,
                    arrows.getCentreX() + 4.0f, arrows.getCentreY() - 2.0f);
    down.addTriangle (arrows.getCentreX() - 4.0f, arrows.getCentreY() + 3.0f,
                      arrows.getCentreX(), arrows.getCentreY() + 7.0f,
                      arrows.getCentreX() + 4.0f, arrows.getCentreY() + 3.0f);

    g.setColour (theme.ink);
    g.fillPath (up);
    g.fillPath (down);

    // AUTO and LEARN side by side.
    const auto buttonHeight = juce::jmin (rowHeight, content.getHeight() * 0.4f);
    auto buttonRow = content.removeFromTop (buttonHeight).reduced (0.0f, 2.0f);

    const auto buttonWidth = (buttonRow.getWidth() - 6.0f) * 0.5f;

    auto autoBox = buttonRow.removeFromLeft (buttonWidth);
    auto learnBox = buttonRow.withTrimmedLeft (6.0f);

    auto drawButton = [&] (juce::Rectangle<float> box, const juce::String& text,
                           bool filled, bool armed, const juce::Colour& colour)
    {
        auto b2 = box.reduced (1.0f, 1.0f);

        g.setColour (armed ? colour.withAlpha (0.28f) : theme.panel);
        g.fillRect (b2);
        drawSketchRect (g, b2, armed ? colour : theme.ink.withAlpha (0.8f), 1.3f, 11);

        g.setColour (armed ? colour : theme.ink);
        g.setFont (uiFont (buttonHeight * 0.72f));
        g.drawText (text, b2, juce::Justification::centred, false);
    };

    drawButton (autoBox, "AUTO", autoEnabled, autoEnabled, theme.accent);
    drawButton (learnBox, "LEARN", false, learnArmed, theme.accent);

    // MIDI readout. Remembered so a click can be routed without recomputing.
    midiRow = content;
    if (midiRow.getHeight() > 9.0f)
    {
        g.setColour (learnArmed ? theme.accent : theme.inkSoft);
        g.setFont (uiFont (juce::jmin (15.0f, midiRow.getHeight())));
        g.drawText (learnArmed ? "listening..." : ("MIDI: " + noteLabel),
                    midiRow.reduced (2.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

void CalloutBox::mouseUp (const juce::MouseEvent& e)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    const auto titleHeight = juce::jmin (15.0f, b.getHeight() * 0.2f);
    const auto rowHeight = juce::jmin (22.0f, (b.getHeight() - titleHeight) * 0.34f);

    auto y = b.getY() + 5.0f + titleHeight;

    // SELECT row.
    auto selectRow = juce::Rectangle<float> (b.getX() + 5.0f, y, b.getWidth() - 10.0f, rowHeight);

    if (selectRow.contains (e.position))
    {
        if (onSelectClicked) onSelectClicked();
        return;
    }

    y += rowHeight;

    // AUTO / LEARN row.
    auto buttonHeight = juce::jmin (rowHeight, (b.getBottom() - 5.0f - y) * 0.4f);
    auto buttonRow = juce::Rectangle<float> (b.getX() + 5.0f, y, b.getWidth() - 10.0f, buttonHeight);
    const auto buttonWidth = (buttonRow.getWidth() - 6.0f) * 0.5f;

    auto autoBox = buttonRow.withWidth (buttonWidth);
    auto learnBox = buttonRow.withTrimmedLeft (buttonWidth + 6.0f).withTrimmedRight (0.0f);

    if (autoBox.contains (e.position))
    {
        if (onAutoClicked) onAutoClicked();
        return;
    }

    if (learnBox.contains (e.position))
    {
        if (onLearnClicked) onLearnClicked();
        return;
    }

    // MIDI row.
    if (midiRow.contains (e.position))
    {
        if (onMidiLabelClicked) onMidiLabelClicked();
        return;
    }
}

} // namespace glorp
