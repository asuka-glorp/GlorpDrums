/*
    EDrumWizard.h - Interactive step-by-step setup wizard for electronic drum kits.
*/

#pragma once

#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <vector>

namespace glorp
{

class PluginProcessor;

class EDrumWizard : public juce::Component,
                    private juce::Timer
{
public:
    explicit EDrumWizard (PluginProcessor& processor);
    ~EDrumWizard() override;

    void open();
    void close();

    std::function<void()> onClose;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    struct Step
    {
        int instrumentIndex = -1;
        juce::String displayName;
        juce::String prompt;
        juce::String hint;
        int currentNote = -1;
        bool completed = false;
    };

    void buildSteps();
    void goToStep (int stepIndex);
    void applyModulePreset (const juce::String& presetName);
    void cancelAndRevert();
    void finishAndClose();

    PluginProcessor& processor;
    Theme theme = Theme::sketch();

    std::vector<Step> steps;
    int currentStepIndex = 0;
    bool isWaitingForHit = false;
    bool justLearned = false;
    int autoAdvanceCounter = 0;

    std::map<int, int> backupNotes;
    juce::String statusFeedback;

    // Buttons
    class WizardButton : public juce::Button
    {
    public:
        WizardButton (const juce::String& name, bool isPrimary = false, bool isSmall = false);
        void paintButton (juce::Graphics& g, bool isHover, bool isDown) override;
    private:
        bool isPrimary = false;
        bool isSmall = false;
    };

    WizardButton prevButton   { "PREVIOUS" };
    WizardButton skipButton   { "SKIP" };
    WizardButton nextButton   { "NEXT", true };
    WizardButton auditionBtn  { "AUDITION SOUND" };
    WizardButton cancelButton { "CANCEL" };
    WizardButton doneButton   { "DONE", true };

    // Quick-preset buttons
    WizardButton rolandBtn    { "ROLAND", false, true };
    WizardButton yamahaBtn    { "YAMAHA", false, true };
    WizardButton alesisBtn    { "ALESIS", false, true };
    WizardButton milleniumBtn { "MILLENIUM", false, true };
    WizardButton efnoteBtn    { "EFNOTE", false, true };
    WizardButton gmBtn        { "GM", false, true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EDrumWizard)
};

} // namespace glorp
