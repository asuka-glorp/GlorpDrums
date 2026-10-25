#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <string>
#include <vector>

namespace glorp
{

struct OraLayer
{
    std::string name;
    std::string src;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float opacity = 1.0f;
    bool visible = true;
    std::vector<uint8_t> pngBytes;
    juce::Image image;
    juce::Image thumbnail;
};

struct OraDocument
{
    int canvasWidth = 1920;
    int canvasHeight = 1080;
    std::vector<OraLayer> layers;
};

class OpenRasterReader
{
public:
    static bool read (const juce::File& oraFile, OraDocument& outDoc, juce::String& outError)
    {
        outDoc.layers.clear();
        if (! oraFile.existsAsFile())
        {
            outError = "File does not exist: " + oraFile.getFullPathName();
            return false;
        }

        juce::ZipFile zip (oraFile);
        if (zip.getNumEntries() == 0)
        {
            outError = "Could not open or parse as ZIP archive: " + oraFile.getFileName();
            return false;
        }

        const auto* stackEntry = zip.getEntry ("stack.xml", true);
        if (stackEntry == nullptr)
        {
            outError = "Not a valid OpenRaster file: missing stack.xml in " + oraFile.getFileName();
            return false;
        }

        std::unique_ptr<juce::InputStream> stackStream (zip.createStreamForEntry (*stackEntry));
        if (stackStream == nullptr)
        {
            outError = "Failed to read stack.xml from " + oraFile.getFileName();
            return false;
        }

        const juce::String xmlText = stackStream->readEntireStreamAsString();
        auto doc = juce::XmlDocument::parse (xmlText);
        if (doc == nullptr || ! doc->hasTagName ("image"))
        {
            outError = "Invalid stack.xml root element in " + oraFile.getFileName();
            return false;
        }

        outDoc.canvasWidth  = doc->getIntAttribute ("w", 1920);
        outDoc.canvasHeight = doc->getIntAttribute ("h", 1080);

        std::vector<juce::XmlElement*> layerNodes;
        std::function<void(juce::XmlElement*)> findLayers = [&] (juce::XmlElement* parent)
        {
            if (parent == nullptr) return;
            for (auto* child : parent->getChildIterator())
            {
                if (child->hasTagName ("layer"))
                    layerNodes.push_back (child);
                else if (child->hasTagName ("stack"))
                    findLayers (child);
            }
        };
        findLayers (doc.get());

        for (auto* node : layerNodes)
        {
            OraLayer layer;
            layer.name = node->getStringAttribute ("name").toStdString();
            layer.src  = node->getStringAttribute ("src").toStdString();
            layer.x    = node->getIntAttribute ("x", 0);
            layer.y    = node->getIntAttribute ("y", 0);
            layer.opacity = (float) node->getDoubleAttribute ("opacity", 1.0);
            layer.visible = (node->getStringAttribute ("visibility", "visible") == "visible");

            if (layer.src.empty())
                continue;

            const auto* pngEntry = zip.getEntry (juce::String (layer.src), true);
            if (pngEntry != nullptr)
            {
                std::unique_ptr<juce::InputStream> pngStream (zip.createStreamForEntry (*pngEntry));
                if (pngStream != nullptr)
                {
                    juce::MemoryBlock mb;
                    pngStream->readIntoMemoryBlock (mb);
                    if (mb.getSize() > 16)
                    {
                        const auto* pData = static_cast<const uint8_t*> (mb.getData());
                        layer.pngBytes.assign (pData, pData + mb.getSize());
                        layer.image = juce::ImageFileFormat::loadFrom (mb.getData(), mb.getSize());
                        if (layer.image.isValid())
                        {
                            layer.width  = layer.image.getWidth();
                            layer.height = layer.image.getHeight();
                            layer.thumbnail = layer.image.rescaled (48, 48, juce::Graphics::mediumResamplingQuality);
                        }
                    }
                }
            }

            if (! layer.pngBytes.empty())
                outDoc.layers.push_back (std::move (layer));
        }

        if (outDoc.layers.empty())
        {
            outError = "No valid raster layers found in " + oraFile.getFileName();
            return false;
        }

        return true;
    }
};

} // namespace glorp
