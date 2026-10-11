#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Editor.h"
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    glorp::PluginProcessor proc;

    const std::string kitPath = "/home/servandoc/DrumGizmo/kits/CrocellKit/CrocellKit_default.xml";
    std::cout << "Loading Crocell kit: " << kitPath << std::endl;
    proc.loadKit (kitPath);

    std::cout << "Loaded instruments: " << proc.getSampler().numInstruments() << std::endl;
    for (int i = 0; i < proc.getSampler().numInstruments(); ++i)
    {
        std::cout << "  Inst " << i << ": " << proc.getSampler().instrumentName(i).toStdString() << std::endl;
    }

    auto editor = proc.createEditorIfNeeded();
    if (! editor)
    {
        std::cerr << "Failed to create editor\n";
        return 1;
    }

    auto* ed = dynamic_cast<glorp::Editor*> (editor->findChildWithID ("") != nullptr ? editor : editor->getChildComponent (0));
    // Let's find Editor component
    glorp::Editor* glorpEd = nullptr;
    for (auto* child : editor->getChildren())
    {
        if (auto* e = dynamic_cast<glorp::Editor*> (child))
        {
            glorpEd = e;
            break;
        }
    }
    if (! glorpEd) glorpEd = dynamic_cast<glorp::Editor*> (editor);

    if (! glorpEd)
    {
        std::cerr << "Could not find glorp::Editor!\n";
        return 1;
    }

    std::cout << "\nLogical instruments in Editor: " << glorpEd->getLogicalInstruments().size() << std::endl;
    for (size_t i = 0; i < glorpEd->getLogicalInstruments().size(); ++i)
    {
        const auto& li = glorpEd->getLogicalInstruments()[i];
        std::cout << "Logical " << i << ": " << li.name.toStdString()
                  << " (piece: " << (int)li.piece << "), zones: " << li.zones.size() << std::endl;
        for (size_t z = 0; z < li.zones.size(); ++z)
        {
            std::cout << "   Zone " << z << ": " << li.zones[z].zoneName.toStdString()
                      << " -> samplerIdx " << li.zones[z].samplerIndex << std::endl;
        }
    }

    // Let's test selecting each logical instrument and checking getActiveSamplerIndex()
    for (size_t i = 0; i < glorpEd->getLogicalInstruments().size(); ++i)
    {
        glorpEd->selectLogicalInstrument (static_cast<int> (i));
        int activeIdx = glorpEd->getActiveSamplerIndex();
        std::cout << "Selected Logical " << i << " (" << glorpEd->getLogicalInstruments()[i].name.toStdString()
                  << ") -> Active Sampler Index: " << activeIdx << std::endl;
    }

    std::cout << "\n--- Testing Knob state across instrument changes ---\n";
    std::cout << "Total parameters in proc: " << proc.getParameters().size() << std::endl;
    for (auto* p : proc.getParameters())
    {
        if (auto* pId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
        {
            if (pId->paramID.startsWith ("inst15.") || pId->paramID.startsWith ("inst16.") || pId->paramID.startsWith ("inst17."))
                std::cout << "   Param: " << pId->paramID.toStdString() << std::endl;
        }
    }
    auto* snareVolParam = proc.findParameter ("inst16.level");
    auto* snarePanParam = proc.findParameter ("inst16.pan");
    std::cout << "Snare vol param before edit: " << (snareVolParam ? snareVolParam->getValue() : -1.0f) << std::endl;

    // Simulate user editing Snare Volume via parameter
    if (snareVolParam) snareVolParam->setValueNotifyingHost (0.25f);
    if (snarePanParam) snarePanParam->setValueNotifyingHost (0.10f); // Pan left

    // Run processBlock to sync
    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    proc.processBlock (buffer, midi);

    auto snareParams = proc.getSampler().getInstrumentParams (16);
    std::cout << "Snare params in Sampler: level=" << snareParams.level << " pan=" << snareParams.pan << std::endl;

    // 2. Select Kick (logical 7, KDrumL, samplerIdx 12)
    glorpEd->selectLogicalInstrument (7);
    std::cout << "Selected Kick. Active sampler index: " << glorpEd->getActiveSamplerIndex() << std::endl;
    auto* kickVolParam = proc.findParameter ("inst12.level");
    std::cout << "Kick vol param before edit: " << (kickVolParam ? kickVolParam->getValue() : -1.0f) << std::endl;

    if (kickVolParam) kickVolParam->setValueNotifyingHost (0.90f);
    proc.processBlock (buffer, midi);

    auto kickParams = proc.getSampler().getInstrumentParams (12);
    std::cout << "Kick params in Sampler: level=" << kickParams.level << " pan=" << kickParams.pan << std::endl;

    // 3. Switch back to Snare
    glorpEd->selectLogicalInstrument (10);
    std::cout << "Switched back to Snare. Active sampler index: " << glorpEd->getActiveSamplerIndex() << std::endl;
    snareVolParam = proc.findParameter ("inst16.level");
    std::cout << "Snare vol param after switching back: " << (snareVolParam ? snareVolParam->getValue() : -1.0f) << std::endl;

    // 4. Test clicking drums on DrumKitView!
    std::cout << "\n--- Testing DrumKitView clicks on CrocellKit ---\n";
    // Find DrumKitView
    glorp::DrumKitView* kv = nullptr;
    for (auto* child : glorpEd->getChildren())
    {
        if (auto* k = dynamic_cast<glorp::DrumKitView*> (child))
        {
            kv = k;
            break;
        }
    }
    if (kv && kv->onPieceClicked)
    {
        std::cout << "Simulating click on snare piece:\n";
        kv->onPieceClicked (glorp::KitPiece::snare);
        std::cout << "  Active sampler index after snare click: " << glorpEd->getActiveSamplerIndex() << std::endl;

        std::cout << "Simulating click on kick piece:\n";
        kv->onPieceClicked (glorp::KitPiece::kick);
        std::cout << "  Active sampler index after kick click: " << glorpEd->getActiveSamplerIndex() << std::endl;

        std::cout << "Simulating click on hihat piece:\n";
        kv->onPieceClicked (glorp::KitPiece::hihat);
        std::cout << "  Active sampler index after hihat click: " << glorpEd->getActiveSamplerIndex() << std::endl;

        std::cout << "Simulating click on crash1 piece:\n";
        kv->onPieceClicked (glorp::KitPiece::crash1);
        std::cout << "  Active sampler index after crash1 click: " << glorpEd->getActiveSamplerIndex() << std::endl;

        std::cout << "Simulating click on crash2 piece:\n";
        kv->onPieceClicked (glorp::KitPiece::crash2);
        std::cout << "  Active sampler index after crash2 click: " << glorpEd->getActiveSamplerIndex() << std::endl;

        std::cout << "Simulating click on ride piece:\n";
        kv->onPieceClicked (glorp::KitPiece::ride);
        std::cout << "  Active sampler index after ride click: " << glorpEd->getActiveSamplerIndex() << std::endl;
    }

    return 0;
}
