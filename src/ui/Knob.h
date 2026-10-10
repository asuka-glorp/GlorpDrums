/*
    Knob.h - a hand-drawn rotary control.

    Deliberately not a standard juce::Slider: the mockup's knobs are
    sketchy circles with a ring of tick spokes, and reproducing that faithfully is
    easier by drawing it directly and listening to the AudioProcessorParameter.
*/

#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace glorp
{

class Knob : public juce::Component,
             private juce::AudioProcessorParameter::Listener
{
public:
    explicit Knob (const juce::String& label);
    ~Knob() override;

    void attachToParameter (juce::AudioProcessorParameter* parameter);
    void setDoubleClickReturnValue (bool enabled, double value);
    void setRange (double newMin, double newMax, double newInterval = 0.0);
    void setRange (juce::NormalisableRange<float> range, double newInterval = 0.0);

    float getDisplayValue() const noexcept;
    void setNormalized (float v);
    juce::AudioProcessorParameter* getParameter() const noexcept { return param; }

    std::function<bool()> onLearnRequested;
    void setLearnArmed (bool armed) { learnArmed = armed; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int parameterIndex, bool gestureIsStarting) override {}

    void showPopupMenu();

    juce::String labelText;
    Theme theme = Theme::sketch();
    bool learnArmed = false;
    bool isDragging = false;

    float dragStartY = 0.0f;
    float dragStartValue = 0.0f;
    float normalised = 0.0f;

    double defaultValue = 0.0;
    bool hasDefaultValue = false;

    juce::AudioProcessorParameter* param = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace glorp
