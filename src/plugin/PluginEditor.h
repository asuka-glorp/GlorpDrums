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
    explicit PluginEditor(PluginProcessor &p) : juce::AudioProcessorEditor(&p), proc(p), editor(p)
    {
        addAndMakeVisible(editor);

#if defined(__linux__)
        // Let the X11/Wayland window manager handle resize handles cleanly without an extra JUCE corner resizer.
        setResizable(true, false);
#else
        setResizable(true, true);
#endif
        setResizeLimits(900, 620, 2400, 1800);

        auto &settings = p.getSettings();
        setSize(juce::jlimit(900, 2400, settings.editorWidth), juce::jlimit(620, 1800, settings.editorHeight));
    }

    void paint(juce::Graphics &g) override { g.fillAll(Theme::byIndex(proc.getSettings().themeIndex).background); }

    void resized() override
    {
        auto bounds = getLocalBounds();
        editor.setBounds(bounds);
        if (bounds.getWidth() >= 900 && bounds.getHeight() >= 620)
        {
            auto &settings = proc.getSettings();
            if (settings.editorWidth != bounds.getWidth() || settings.editorHeight != bounds.getHeight())
            {
                settings.editorWidth = bounds.getWidth();
                settings.editorHeight = bounds.getHeight();
                settings.markDirty();
            }
        }
    }

  private:
    PluginProcessor &proc;
    Editor editor;
};

} // namespace glorp
