#include "Settings.h"
#include "KitScanner.h"

namespace glorp
{

namespace
{
    constexpr int kVersionTag = 1;
}

Settings::Settings()
{
    load();
}

juce::File Settings::getConfigDirectory() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("GlorpDrums");
}

juce::File Settings::getConfigFile() const
{
    return getConfigDirectory().getChildFile ("settings.txt");
}

juce::File Settings::getPresetDirectory() const
{
    return getConfigDirectory().getChildFile ("presets");
}

void Settings::load()
{
    const auto file = getConfigFile();

    if (! file.existsAsFile())
        return;

    juce::MemoryBlock block;

    if (! file.loadFileAsData (block))
        return;

    readState (block);
}

void Settings::saveIfChanged()
{
    if (! dirty)
        return;

    const auto file = getConfigFile();
    file.getParentDirectory().createDirectory();

    juce::MemoryBlock block;
    writeState (block);

    if (file.replaceWithData (block.getData(), block.getSize()))
        dirty = false;
}

void Settings::writeState (juce::MemoryBlock& destination) const
{
    juce::XmlElement root ("GlorpDrumsSettings");
    root.setAttribute ("version", kVersionTag);

    auto* kits = root.createNewChildElement ("KitPaths");

    for (const auto& p : extraKitPaths)
        kits->createNewChildElement ("path")->setAttribute ("value", p);

    auto* recent = root.createNewChildElement ("RecentKits");

    for (const auto& p : recentKits)
        recent->createNewChildElement ("kit")->setAttribute ("value", p);

    root.setAttribute ("lastKit", lastKitPath);
    root.setAttribute ("lastMapPreset", lastMapPreset);
    root.setAttribute ("editorWidth", editorWidth);
    root.setAttribute ("editorHeight", editorHeight);
    root.setAttribute ("editorMaximised", editorMaximised);
    root.setAttribute ("theme", themeIndex);
    root.setAttribute ("openGlEnabled", openGlEnabled);

    juce::MemoryOutputStream stream (destination, false);
    root.writeToStream (stream, {});
}

void Settings::readState (const juce::MemoryBlock& source)
{
    auto xmlString = juce::String::fromUTF8 (
        static_cast<const char*> (source.getData()), static_cast<int> (source.getSize()));
    auto root = juce::parseXML (xmlString);

    if (root == nullptr || root->getTagName() != "GlorpDrumsSettings")
        return;

    if (auto* kits = root->getChildByName ("KitPaths"))
        for (auto* p : kits->getChildIterator())
            if (auto v = p->getStringAttribute ("value"); v.isNotEmpty())
                extraKitPaths.add (v);

    if (auto* recent = root->getChildByName ("RecentKits"))
        for (auto* k : recent->getChildIterator())
            if (auto v = k->getStringAttribute ("value"); v.isNotEmpty())
                recentKits.add (v);

    lastKitPath = root->getStringAttribute ("lastKit", lastKitPath);
    lastMapPreset = root->getStringAttribute ("lastMapPreset", lastMapPreset);
    editorWidth = root->getIntAttribute ("editorWidth", editorWidth);
    editorHeight = root->getIntAttribute ("editorHeight", editorHeight);
    editorMaximised = root->getBoolAttribute ("editorMaximised", editorMaximised);
    themeIndex = root->getIntAttribute ("theme", themeIndex);
    openGlEnabled = root->getBoolAttribute ("openGlEnabled", openGlEnabled);
}

void Settings::addKitPath (const juce::String& path)
{
    if (path.isEmpty())
        return;

    const auto resolved = KitScanner::resolvePath (path.toStdString());

    if (resolved.empty())
        return;

    const auto s = juce::String (resolved);

    if (! extraKitPaths.contains (s))
    {
        extraKitPaths.add (s);
        dirty = true;
    }
}

void Settings::removeKitPath (const juce::String& path)
{
    extraKitPaths.removeString (path);
    dirty = true;
}

std::vector<std::string> Settings::allSearchPaths() const
{
    auto paths = KitScanner::defaultSearchPaths();

    for (const auto& p : extraKitPaths)
    {
        const auto resolved = KitScanner::resolvePath (p.toStdString());

        if (! resolved.empty())
            paths.push_back (resolved);
    }

    return paths;
}

void Settings::pushRecentKit (const juce::String& path)
{
    if (path.isEmpty())
        return;

    recentKits.removeString (path);
    recentKits.insert (0, path);

    while (recentKits.size() > maxRecentKits())
        recentKits.remove (recentKits.size() - 1);

    lastKitPath = path;
    dirty = true;
}

} // namespace glorp
