/*
    CalloutBox.h - the SELECT / AUTO / LEARN / MIDI box from the mockup.

    This is the signature element of the design: a small white box with a dashed
    leader line running back to the piece of the kit it controls. It does three
    jobs at once - picking the sample, arming MIDI learn, and showing the note the
    instrument is currently mapped to.
*/

#pragma once

#include "Theme.h"

#include <functional>

namespace glorp
{

class CalloutBox : public juce::Component
{
public:
    CalloutBox();

    void setInstrumentName (const juce::String& name);
    void setSampleName (const juce::String& name);
    void setNoteLabel (const juce::String& label);
    void setAutoEnabled (bool enabled);
    void setLearnArmed (bool armed) { learnArmed = armed; repaint(); }
    void setMuted (bool m) { muted = m; repaint(); }

    /// Where the dashed leader line should attach, in the *parent's* coordinates.
    void setAnchor (const juce::Point<float>& anchor);

    std::function<void()> onSelectClicked;
    std::function<void()> onAutoClicked;
    std::function<void()> onLearnClicked;
    std::function<void()> onMidiLabelClicked;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void drawLeaderLine (juce::Graphics&, const juce::Rectangle<float>& localBounds);

    juce::String instrumentName;
    juce::String sampleName;
    juce::String noteLabel;
    juce::Point<float> anchor { -100.0f, -100.0f };
    juce::Rectangle<float> midiRow;

    bool autoEnabled = false;
    bool learnArmed = false;
    bool muted = false;

    Theme theme = Theme::sketch();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalloutBox)
};

} // namespace glorp
