#include "Knob.h"
#include <cmath>

namespace glorp
{

namespace
{
    juce::Point<float> polarPoint (juce::Point<float> centre, float angle, float radius)
    {
        return { centre.x + radius * std::sin (angle),
                 centre.y - radius * std::cos (angle) };
    }
}

Knob::Knob (const juce::String& label)
    : labelText (label)
{
    setOpaque (false);
}

Knob::~Knob()
{
    if (param != nullptr)
        param->removeListener (this);
}

void Knob::attachToParameter (juce::AudioProcessorParameter* parameter)
{
    if (param != nullptr)
        param->removeListener (this);

    param = parameter;

    if (param != nullptr)
    {
        param->addListener (this);
        normalised = param->getValue();
    }

    repaint();
}

void Knob::setDoubleClickReturnValue (bool enabled, double value)
{
    hasDefaultValue = enabled;
    defaultValue = value;
}

void Knob::setRange (double, double, double)
{
}

void Knob::setRange (juce::NormalisableRange<float>, double)
{
}

void Knob::parameterValueChanged (int, float newValue)
{
    normalised = newValue;
    juce::MessageManager::callAsync ([this] { repaint(); });
}

void Knob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    const auto labelHeight = juce::jmin (18.0f, bounds.getHeight() * 0.26f);
    const auto knobArea = bounds.removeFromBottom (labelHeight + 2.0f).reduced (4.0f);

    const auto centre = knobArea.getCentre();
    const auto radius = juce::jmin (knobArea.getWidth(), knobArea.getHeight()) * 0.5f;

    if (radius > 4.0f)
    {
        // Tick ring: short spokes around the outside, like the mockup.
        g.setColour (theme.ink.withAlpha (0.45f));

        constexpr int numTicks = 11;
        constexpr float startAngle = 2.35f;
        constexpr float sweep = 3.63f;

        for (int i = 0; i < numTicks; ++i)
        {
            const auto angle = startAngle + sweep * i / static_cast<float> (numTicks - 1);
            const auto inner = polarPoint (centre, angle, radius * 1.05f);
            const auto outer = polarPoint (centre, angle, radius * 1.22f);
            g.drawLine (juce::Line<float> (inner, outer), 1.0f);
        }

        // Body.
        juce::Path body;
        body.addEllipse (juce::Rectangle<float> (radius * 2.04f, radius * 2.04f).withCentre (centre));

        g.setColour (theme.panel);
        g.fillPath (body);
        g.setColour (learnArmed ? theme.accent : theme.ink);
        g.strokePath (body, juce::PathStrokeType (1.7f));

        // Pointer.
        const auto value = getDisplayValue();
        const auto pointerAngle = startAngle + sweep * value;

        juce::Path pointer;
        pointer.startNewSubPath (polarPoint (centre, pointerAngle, radius * 0.22f));
        pointer.lineTo (polarPoint (centre, pointerAngle, radius * 0.85f));

        g.setColour (learnArmed ? theme.accent : theme.ink);
        g.strokePath (pointer, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        // Value arc, drawn faintly so it reads without shouting.
        if (value > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, radius * 1.12f, radius * 1.12f, 0.0f,
                               startAngle, startAngle + sweep * value, true);
            g.setColour (theme.ink.withAlpha (0.5f));
            g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }
    }

    auto text = bounds.withTrimmedBottom (2.0f);
    auto font = uiFont (labelHeight);

    if (font.getHeight() > 8.0f)
    {
        g.setFont (font);
        g.setColour (theme.ink);
        g.drawText (labelText, text, juce::Justification::centred, false);
    }
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        showPopupMenu();
        return;
    }

    dragStartY = static_cast<float> (e.getPosition().y);
    dragStartValue = getDisplayValue();
    isDragging = true;

    if (param != nullptr)
        param->beginChangeGesture();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDragging)
        return;

    float delta = (dragStartY - static_cast<float> (e.getPosition().y)) / 150.0f;

    if (e.mods.isShiftDown())
        delta *= 0.2f;

    setNormalized (dragStartValue + delta);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (isDragging)
    {
        isDragging = false;
        if (param != nullptr)
            param->endChangeGesture();
    }
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (param != nullptr)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getDefaultValue());
        param->endChangeGesture();
    }
    else if (hasDefaultValue)
    {
        setNormalized (static_cast<float> (defaultValue));
    }
}

void Knob::showPopupMenu()
{
    if (onLearnRequested != nullptr && onLearnRequested())
        return;

    if (param != nullptr)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->getDefaultValue());
        param->endChangeGesture();
    }
}

float Knob::getDisplayValue() const noexcept
{
    if (param != nullptr)
        return param->getValue();

    return juce::jlimit (0.0f, 1.0f, normalised);
}

void Knob::setNormalized (float v)
{
    normalised = juce::jlimit (0.0f, 1.0f, v);

    if (param != nullptr)
        param->setValueNotifyingHost (normalised);

    repaint();
}

} // namespace glorp
