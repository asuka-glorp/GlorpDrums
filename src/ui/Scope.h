/*
    Scope.h - the MIDI activity strip along the bottom of the mockup.

    Shows every note that arrives, which is the fastest way to tell whether a
    pad is sending velocity, whether a mapping is firing twice, or whether a CC
    is stuck.
*/

#pragma once

#include "Theme.h"

namespace glorp
{

class Scope : public juce::Component
{
public:
    Scope();

    void noteReceived (int note, int velocity);
    void controllerReceived (int cc, int value);

    void paint (juce::Graphics&) override;

private:
    struct Marker
    {
        int lane = 0;
        float level = 0.0f;
        int64_t bornAt = 0;
    };

    static constexpr int maxMarkers = 220;

    Marker markers[maxMarkers];
    int writeIndex = 0;
    int64_t clock = 0;

    Theme theme = Theme::sketch();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Scope)
};

} // namespace glorp
