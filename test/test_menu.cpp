#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    glorp::PluginProcessor proc;
    auto editor = proc.createEditorIfNeeded();
    if (! editor)
    {
        std::cout << "Failed to create editor!\n";
        return 1;
    }

    editor->setSize (1180, 860);
    std::cout << "Current kit path: " << proc.getCurrentKitPath().toStdString() << std::endl;
    std::cout << "Last kit error: " << proc.getLastKitError().toStdString() << std::endl;
    std::cout << "Num instruments: " << proc.getSampler().numInstruments() << std::endl;
    for (int i = 0; i < proc.getSampler().numInstruments(); ++i)
    {
        std::cout << "  Inst " << i << ": " << proc.getSampler().instrumentName(i).toStdString()
                  << " samples: " << proc.getSampler().numSamplesForInstrument(i) << std::endl;
    }
    return 0;
}
