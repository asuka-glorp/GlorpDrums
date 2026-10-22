/*
    Scope.h - 6-Lane Drum Rhythm Ribbon MIDI Activity strip.

    Displays incoming drum hits across 6 dedicated horizontal lanes:
      - CYM (Cymbals: Crash 1, Crash 2, Ride with distinct shades)
      - HH  (Hi-Hat: Closed, SemiOpen, Open, Pedal)
      - TOM (Toms: Tom 1, Tom 2, Floor Tom with distinct shades)
      - SNR (Snare: Head, RimClick, Rimshot)
      - KCK (Kick: Crimson)
      - ETC (Aux/Percussion: Cowbell, Tambourine, Shaker)

    Handles simultaneous hits (chords) with zero visual overlap, scales
    capsules with velocity/intensity, and features a Live Hit Inspector HUD.
*/

#pragma once

#include "Theme.h"
#include "DrumKitView.h"
#include <vector>

namespace glorp
{

enum class ScopeLane
{
    cymbal = 0,
    hihat,
    tom,
    snare,
    kick,
    etc,
    count
};

struct ScopeHit
{
    ScopeLane lane { ScopeLane::etc };
    juce::String instrumentName;
    juce::String shortLabel;
    juce::Colour color;
    float velocity = 0.0f;
    int midiVelocity = 0;
    int note = -1;
    double timeSeconds = 0.0;
};

class Scope : public juce::Component,
              private juce::Timer
{
public:
    Scope();
    ~Scope() override;

    /// Called when an instrument is hit from the audio engine or UI click
    void hitReceived (const juce::String& instrumentName, KitPiece piece, int note, float velocity, double timestampSeconds = 0.0);

    /// Fallback for raw MIDI note input
    void noteReceived (int note, int velocity);

    /// Controller input (e.g. hi-hat pedal CC4)
    void controllerReceived (int cc, int value);

    void paint (juce::Graphics&) override;
    void resized() override;
    void setTheme (const Theme& t);

    bool isBackgroundCacheValid() const noexcept { return staticBgImage.isValid(); }

    static ScopeLane laneForPieceAndName (KitPiece piece, const juce::String& name);
    static juce::Colour colorForInstrument (KitPiece piece, const juce::String& name);
    static juce::String shortLabelForInstrument (KitPiece piece, const juce::String& name);

private:
    void timerCallback() override;
    void rebuildBackgroundCache();

    static constexpr int maxHits = 256;
    ScopeHit hits[maxHits];
    int writeIndex = 0;
    int hitCount = 0;
    double lastHitTime = -100.0;
    bool idleFrameRendered = true;
    juce::Image staticBgImage;

    // Active chord tracking for the Live Hit Inspector HUD
    struct RecentChord
    {
        double timestamp = 0.0;
        struct Item
        {
            juce::String label;
            juce::Colour color;
            int velocity = 0;
            int note = -1;
        };
        std::vector<Item> items;
    };
    RecentChord recentChord;

    // Recent CC activity (e.g. Hi-Hat pedal)
    float lastCc4Value = -1.0f;
    double lastCc4Time = 0.0;

    Theme theme = Theme::sketch();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Scope)
};

} // namespace glorp
