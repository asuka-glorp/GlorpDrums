/*
    CalloutBox.h - hand-drawn comic style callout box for drum instruments.
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
    void setSelected (bool selected) { isSelected = selected; repaint(); }
    void setMuted (bool m) { muted = m; repaint(); }

    /// Where the dashed leader line should attach on the drum in parent coordinates.
    void setAnchor (const juce::Point<float>& a) { anchor = a; }
    juce::Point<float> getAnchor() const noexcept { return anchor; }

    /// Attachment point on the callout box's edge in parent coordinates.
    juce::Point<float> getLeaderStartPoint (juce::Component* parent) const;

    std::function<void()> onSelectClicked;
    std::function<void()> onAutoClicked;
    std::function<void()> onLearnClicked;
    std::function<void()> onMidiLabelClicked;
    std::function<void()> onBoxClicked;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String instrumentName;
    juce::String sampleName;
    juce::String noteLabel;
    juce::Point<float> anchor { -100.0f, -100.0f };

    juce::Rectangle<float> selectButtonBounds;
    juce::Rectangle<float> autoButtonBounds;
    juce::Rectangle<float> learnButtonBounds;
    juce::Rectangle<float> midiRowBounds;

    bool autoEnabled = false;
    bool learnArmed = false;
    bool isSelected = false;
    bool muted = false;

    Theme theme = Theme::sketch();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CalloutBox)
};

} // namespace glorp
