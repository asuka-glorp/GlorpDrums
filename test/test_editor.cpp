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
    std::cout << "Editor created. Bounds: " << editor->getBounds().toString().toStdString() << std::endl;
    std::cout << "Proc current kit: " << proc.getCurrentKitPath().toStdString() << std::endl;
    std::cout << "Proc instruments count: " << proc.getSampler().numInstruments() << std::endl;
    for (int i = 0; i < proc.getSampler().numInstruments(); ++i)
        std::cout << "  Inst " << i << ": " << proc.getSampler().instrumentName(i).toStdString() << std::endl;

    // Take snapshot of editor
    auto image = editor->createComponentSnapshot (editor->getLocalBounds());
    std::cout << "Snapshot taken: " << image.getWidth() << "x" << image.getHeight() << std::endl;

    juce::File outFile ("/home/servandoc/Documents/DEV/GlorpDrums/test/editor_snapshot.png");
    juce::FileOutputStream fos (outFile);
    if (fos.openedOk())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (image, fos);
        std::cout << "Saved snapshot to: " << outFile.getFullPathName().toStdString() << std::endl;
    }

    return 0;
}
