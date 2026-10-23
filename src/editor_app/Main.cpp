/*
    Main.cpp - Entry point for the standalone GlorpKitEditor (.glorpkit Studio) desktop application.
*/

#include "GlorpKitEditorWindow.h"
#include "GlorpDrumsAssets.h"

namespace glorp
{

class GlorpKitEditorMainWindow : public juce::DocumentWindow
{
public:
    GlorpKitEditorMainWindow (const juce::String& name, const juce::String& commandLine)
        : DocumentWindow (name,
                          juce::Colour (0xff2b2620),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);

        auto icon = juce::ImageFileFormat::loadFrom (GlorpAssets::glorpkit_studio_icon_png,
                                                     (size_t) GlorpAssets::glorpkit_studio_icon_pngSize);
        if (icon.isValid())
            setIcon (icon);

        auto* editorComp = new GlorpKitEditorComponent (true);
        setContentOwned (editorComp, true);

        setResizable (true, true);
        setResizeLimits (1024, 680, 3840, 2160);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);

        if (icon.isValid())
        {
            if (auto* peer = getPeer())
                peer->setIcon (icon);
        }

        // Allow passing a .glorpkit or DrumGizmo .xml file path on the command line
        auto args = juce::StringArray::fromTokens (commandLine, true);
        for (const auto& rawArg : args)
        {
            juce::String cleaned = rawArg.unquoted().trim();
            if (cleaned.endsWithIgnoreCase (".glorpkit") || cleaned.endsWithIgnoreCase (".xml"))
            {
                juce::File f (cleaned);
                if (f.existsAsFile())
                {
                    editorComp->openKitFile (f);
                    break;
                }
            }
        }
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlorpKitEditorMainWindow)
};

class GlorpKitEditorApplication : public juce::JUCEApplication
{
public:
    GlorpKitEditorApplication() = default;

    const juce::String getApplicationName() override       { return "GlorpKitEditor"; }
    const juce::String getApplicationVersion() override    { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<GlorpKitEditorMainWindow> ("GlorpKit Studio - .glorpkit v2.0 Editor", commandLine);
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override {}

private:
    std::unique_ptr<GlorpKitEditorMainWindow> mainWindow;
};

} // namespace glorp

START_JUCE_APPLICATION (glorp::GlorpKitEditorApplication)
