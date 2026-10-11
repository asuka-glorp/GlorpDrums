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
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme.panel);
    g.fillRect (b);
    drawSketchRect (g, b, theme.panelEdge, 1.4f, 5);

    // Header label: "MIDI Activity" (matching mockup)
    g.setFont (uiFont (14.0f));
    g.setColour (theme.ink);
    g.drawText ("MIDI Activity", b.getX() + 10.0f, b.getY() + 4.0f, 180.0f, 18.0f,
                juce::Justification::centredLeft, false);

    // Waveform strip area
    const float midY = b.getY() + b.getHeight() * 0.62f;

    // Baseline
    g.setColour (theme.ink.withAlpha (0.25f));
    g.drawLine (b.getX() + 6.0f, midY, b.getRight() - 6.0f, midY, 1.0f);

    const auto span = static_cast<float> (maxMarkers);
    const auto step = (b.getWidth() - 20.0f) / span;
    const float startX = b.getX() + 10.0f;

    juce::Path wave;
    wave.startNewSubPath (startX, midY);

    for (int i = 0; i < maxMarkers; ++i)
    {
        const auto& m = markers[(writeIndex + i) % maxMarkers];
        const float x = startX + i * step;

        // Either actual marker level or subtle idle sketch ripples
        float amp = m.level * (b.getHeight() * 0.40f);
        if (amp < 2.0f)
        {
            // Subtle idle hand-drawn heartbeat ripples
            amp = ((i % 4 == 0) ? 2.5f : ((i % 4 == 2) ? -2.0f : 0.0f));
        }

        const float y1 = midY - amp;
        const float y2 = midY + amp * 0.5f;

        wave.lineTo (x + step * 0.3f, y1);
        wave.lineTo (x + step * 0.7f, y2);
        wave.lineTo (x + step, midY);
    }

    g.setColour (theme.ink.withAlpha (0.85f));
    g.strokePath (wave, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

} // namespace glorp
