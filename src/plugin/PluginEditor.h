/*
    PluginEditor.h - the thin AudioProcessorEditor wrapper.

    Keeps JUCE's plumbing out of the hand-drawn UI so that Editor can also be
    hosted by the standalone app without dragging AudioProcessor in with it.
*/

#pragma once

#include "Editor.h"

namespace glorp
{

class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor& p)
        : juce::AudioProcessorEditor (&p), proc (p), editor (p)
    {
        addAndMakeVisible (editor);

        setResizable (true, true);
        setResizeLimits (900, 620, 2400, 1800);

        auto& settings = p.getSettings();
        setSize (juce::jlimit (900, 2400, settings.editorWidth),
                 juce::jlimit (620, 1800, settings.editorHeight));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::byIndex (proc.getSettings().themeIndex).background);
    }

    void resized() override
    {
        editor.setBounds (getLocalBounds());
    }

private:
    PluginProcessor& proc;
    Editor editor;
};

} // namespace glorp
