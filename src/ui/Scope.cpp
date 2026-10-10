#include "Scope.h"

#include <algorithm>

namespace glorp
{

namespace
{
    constexpr double pi = 3.14159265358979323846;
}

Scope::Scope()
{
    setOpaque (false);
}

void Scope::noteReceived (int note, int velocity)
{
    // Lanes by pitch class so the pattern of a groove is readable at a glance.
    Marker& m = markers[writeIndex];
    m.lane = note % 12;
    m.level = std::clamp (velocity / 127.0f, 0.05f, 1.0f);
    m.bornAt = clock;
    writeIndex = (writeIndex + 1) % maxMarkers;
    repaint();
}

void Scope::controllerReceived (int cc, int value)
{
    Marker& m = markers[writeIndex];
    m.lane = 12 + (cc % 4);
    m.level = std::clamp (value / 127.0f, 0.05f, 1.0f);
    m.bornAt = clock;
    writeIndex = (writeIndex + 1) % maxMarkers;
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (theme.panel);
    g.fillRect (b);
    drawSketchRect (g, b.reduced (0.5f), theme.ink.withAlpha (0.8f), 1.4f, 5);

    // Baseline.
    g.setColour (theme.inkSoft.withAlpha (0.35f));
    g.drawLine (b.getX() + 2.0f, b.getCentreY(), b.getRight() - 2.0f, b.getCentreY(), 1.0f);

    const auto span = static_cast<float> (maxMarkers);
    const auto step = b.getWidth() / span;

    // Newest markers at the right, as in the mockup.
    for (int i = 0; i < maxMarkers; ++i)
    {
        const auto& m = markers[(writeIndex + i) % maxMarkers];
        const auto age = static_cast<float> (writeIndex + i < maxMarkers ? writeIndex + i : i);

        const auto x = b.getX() + step * (static_cast<float> (i) - 1.0f);
        const auto amplitude = b.getHeight() * 0.42f * m.level;
        const auto fade = 0.35f + 0.65f * (static_cast<float> (i) / static_cast<float> (maxMarkers));

        juce::ignoreUnused (age);

        g.setColour (theme.ink.withAlpha (0.85f * fade));

        // A little scribble rather than a bar, so it reads as a waveform.
        g.drawLine (x, b.getCentreY() - amplitude, x + step * 0.6f, b.getCentreY(), 1.2f);
        g.drawLine (x + step * 0.6f, b.getCentreY(), x + step * 0.9f, b.getCentreY() + amplitude * 0.6f, 1.2f);
    }
}

} // namespace glorp
