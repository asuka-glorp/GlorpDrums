#include "Knob.h"
#include <cmath>

namespace glorp
{

namespace
{
juce::Point<float> polarPoint(juce::Point<float> centre, float angle, float radius)
{
    return {centre.x + radius * std::sin(angle), centre.y - radius * std::cos(angle)};
}
} // namespace

Knob::Knob(const juce::String &label) : labelText(label)
{
    setOpaque(false);
}

Knob::Knob(const juce::String &label, float minVal, float maxVal, float defVal, const juce::String &unit)
    : labelText(label), minVal_(minVal), maxVal_(maxVal), unitString(unit)
{
    setOpaque(false);
    setValue(defVal);
    setDoubleClickReturnValue(true, normalised);
}

Knob::~Knob()
{
    if (param != nullptr)
        param->removeListener(this);
}

void Knob::attachToParameter(juce::AudioProcessorParameter *parameter)
{
    if (param != nullptr)
        param->removeListener(this);

    param = parameter;

    if (param != nullptr)
    {
        param->addListener(this);
        normalised = param->getValue();
    }

    repaint();
}

void Knob::setDoubleClickReturnValue(bool enabled, double value)
{
    hasDefaultValue = enabled;
    defaultValue = value;
}

void Knob::setRange(double, double, double)
{
}

void Knob::setRange(juce::NormalisableRange<float>, double)
{
}

void Knob::setRange(float minVal, float maxVal, float defVal, const juce::String &unit)
{
    minVal_ = minVal;
    maxVal_ = maxVal;
    unitString = unit;
    setValue(defVal);
    setDoubleClickReturnValue(true, normalised);
}

void Knob::setValue(float realValue)
{
    const float norm = (maxVal_ > minVal_) ? (realValue - minVal_) / (maxVal_ - minVal_) : 0.0f;
    setNormalized(norm);
}

float Knob::getValue() const noexcept
{
    return minVal_ + getDisplayValue() * (maxVal_ - minVal_);
}

void Knob::parameterValueChanged(int, float newValue)
{
    normalised = newValue;
    if (onValueChanged != nullptr)
        onValueChanged(getValue());
    juce::MessageManager::callAsync([this] { repaint(); });
}

void Knob::paint(juce::Graphics &g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    const auto labelHeight = juce::jmin(16.0f, bounds.getHeight() * 0.25f);
    const auto labelArea = bounds.removeFromBottom(labelHeight);
    const auto knobArea = bounds.reduced(3.0f);

    const auto centre = knobArea.getCentre();
    const auto radius = juce::jmin(knobArea.getWidth(), knobArea.getHeight()) * 0.44f;

    if (theme.minimalStyle)
    {
        if (radius > 4.0f)
        {
            constexpr float startAngle =
                2.356f; // -135 deg in JUCE radians (2*pi - 2.356 .. +2.356? wait keep exact 2.35..5.98)
            constexpr float arcStart = -2.35f;
            constexpr float arcSweep = 4.70f;
            const auto value = getDisplayValue();
            const auto activeCol = learnArmed ? theme.accentWarm : (hasCustomAccent ? customAccent : theme.accent);

            // Outer radial dot ticks (subtle Minimal Audio telemetry ring)
            constexpr int numDots = 15;
            const float dotR = radius * 1.24f;
            for (int i = 0; i < numDots; ++i)
            {
                const float frac = static_cast<float>(i) / static_cast<float>(numDots - 1);
                const float a = arcStart + arcSweep * frac;
                const auto pt = polarPoint(centre, a, dotR);
                const bool passed = bipolar ? ((value >= 0.5f) ? (frac >= 0.5f && frac <= value + 0.01f)
                                                               : (frac <= 0.5f && frac >= value - 0.01f))
                                            : (frac <= value + 0.01f);
                g.setColour(passed ? activeCol.withAlpha(0.85f) : theme.inkSoft.withAlpha(0.32f));
                g.fillEllipse(pt.x - 1.1f, pt.y - 1.1f, 2.2f, 2.2f);
            }

            // Recessed track arc
            const float trackRadius = radius * 1.08f;
            {
                juce::Path bgArc;
                bgArc.addCentredArc(
                    centre.x, centre.y, trackRadius, trackRadius, 0.0f, arcStart, arcStart + arcSweep, true);
                g.setColour(theme.trough);
                g.strokePath(bgArc,
                             juce::PathStrokeType(2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Active value arc
            const float curAngle = arcStart + arcSweep * value;
            const float midAngle = arcStart + arcSweep * 0.5f;
            const float fromA = bipolar ? std::min(midAngle, curAngle) : arcStart;
            const float toA = bipolar ? std::max(midAngle, curAngle) : curAngle;
            if (std::abs(toA - fromA) > 0.02f)
            {
                juce::Path valArc;
                valArc.addCentredArc(centre.x, centre.y, trackRadius, trackRadius, 0.0f, fromA, toA, true);
                g.setColour(activeCol);
                g.strokePath(valArc,
                             juce::PathStrokeType(2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Machined metallic rotary cap
            const float capR = radius * 0.84f;
            const auto capBounds = juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(centre);

            if (!theme.isDark)
            {
                g.setColour(juce::Colour(0x1e111622));
                g.fillEllipse(capBounds.translated(0.0f, 1.8f));
            }

            juce::ColourGradient capGrad(theme.isDark ? juce::Colour(0xff383e4d) : juce::Colour(0xffffffff),
                                         centre.x,
                                         capBounds.getY(),
                                         theme.isDark ? juce::Colour(0xff20242e) : juce::Colour(0xffd7dde8),
                                         centre.x,
                                         capBounds.getBottom(),
                                         false);
            g.setGradientFill(capGrad);
            g.fillEllipse(capBounds);

            g.setColour(learnArmed ? theme.accentWarm
                                   : (theme.isDark ? juce::Colour(0xff4a5164) : juce::Colour(0xffb8c0cf)));
            g.drawEllipse(capBounds, 1.2f);

            // Inner ring highlight
            g.setColour(theme.isDark ? juce::Colours::white.withAlpha(0.07f) : juce::Colours::white.withAlpha(0.8f));
            g.drawEllipse(capBounds.reduced(1.4f), 0.9f);

            // Crisp radial pointer notch
            juce::Path pointer;
            pointer.startNewSubPath(polarPoint(centre, curAngle, capR * 0.34f));
            pointer.lineTo(polarPoint(centre, curAngle, capR * 0.86f));
            g.setColour(theme.isDark ? juce::Colour(0xfff4f6fb) : juce::Colour(0xff1e2430));
            g.strokePath(pointer,
                         juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::ignoreUnused(startAngle);
        }

        auto font = uiFont(labelHeight * 0.85f).boldened();
        if (font.getHeight() > 7.5f)
        {
            g.setFont(font);
            g.setColour(theme.inkSoft);
            g.drawText(labelText.toUpperCase(), labelArea, juce::Justification::centred, false);
        }
        return;
    }

    if (radius > 4.0f)
    {
        // Tick ring: short spokes around the outside, like the mockup.
        g.setColour(theme.ink.withAlpha(0.50f));

        constexpr int numTicks = 11;
        constexpr float startAngle = 2.35f;
        constexpr float sweep = 3.63f;

        for (int i = 0; i < numTicks; ++i)
        {
            const auto angle = startAngle + sweep * i / static_cast<float>(numTicks - 1);
            const auto inner = polarPoint(centre, angle, radius * 1.05f);
            const auto outer = polarPoint(centre, angle, radius * 1.25f);
            g.drawLine(juce::Line<float>(inner, outer), 1.2f);
        }

        // Body.
        juce::Path body;
        body.addEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));

        g.setColour(theme.panel);
        g.fillPath(body);
        g.setColour(learnArmed ? theme.accent : theme.ink);
        g.strokePath(body, juce::PathStrokeType(1.8f));

        // Pointer.
        const auto value = getDisplayValue();
        const auto pointerAngle = startAngle + sweep * value;

        juce::Path pointer;
        pointer.startNewSubPath(polarPoint(centre, pointerAngle, radius * 0.20f));
        pointer.lineTo(polarPoint(centre, pointerAngle, radius * 0.88f));

        g.setColour(learnArmed ? theme.accent : theme.ink);
        g.strokePath(pointer, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Value arc, drawn faintly so it reads without shouting.
        if (value > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc(
                centre.x, centre.y, radius * 1.12f, radius * 1.12f, 0.0f, startAngle, startAngle + sweep * value, true);
            g.setColour(theme.ink.withAlpha(0.4f));
            g.strokePath(arc, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    auto font = uiFont(labelHeight * 0.95f);
    if (font.getHeight() > 8.0f)
    {
        g.setFont(font);
        g.setColour(theme.ink);
        g.drawText(labelText, labelArea, juce::Justification::centred, false);
    }
}

void Knob::mouseDown(const juce::MouseEvent &e)
{
    if (e.mods.isRightButtonDown())
    {
        showPopupMenu();
        return;
    }

    dragStartY = static_cast<float>(e.getPosition().y);
    dragStartValue = getDisplayValue();
    isDragging = true;

    if (param != nullptr)
        param->beginChangeGesture();
}

void Knob::mouseDrag(const juce::MouseEvent &e)
{
    if (!isDragging)
        return;

    float delta = (dragStartY - static_cast<float>(e.getPosition().y)) / 150.0f;

    if (e.mods.isShiftDown())
        delta *= 0.2f;

    setNormalized(dragStartValue + delta);
}

void Knob::mouseUp(const juce::MouseEvent &)
{
    if (isDragging)
    {
        isDragging = false;
        if (param != nullptr)
            param->endChangeGesture();
    }
}

void Knob::mouseDoubleClick(const juce::MouseEvent &)
{
    if (param != nullptr)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->getDefaultValue());
        param->endChangeGesture();
    }
    else if (hasDefaultValue)
    {
        setNormalized(static_cast<float>(defaultValue));
    }
}

void Knob::showPopupMenu()
{
    if (onLearnRequested != nullptr && onLearnRequested())
        return;

    if (param != nullptr)
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->getDefaultValue());
        param->endChangeGesture();
    }
}

float Knob::getDisplayValue() const noexcept
{
    if (param != nullptr)
        return param->getValue();

    return juce::jlimit(0.0f, 1.0f, normalised);
}

void Knob::setNormalized(float v)
{
    normalised = juce::jlimit(0.0f, 1.0f, v);

    if (param != nullptr)
        param->setValueNotifyingHost(normalised);

    if (onValueChanged != nullptr)
        onValueChanged(getValue());

    repaint();
}

} // namespace glorp
