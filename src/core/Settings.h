/*
    Settings.h - user preferences that are not part of a saved plugin state.

    These are the things a user sets once for the whole installation (where
    their kits live, which kit they last used, window size) as opposed to the
    per-song state that belongs in the project file.

    Backed by a plain ini-style file under the user's config directory, which is
    safer than JUCE's shared-value store when the same plugin is loaded twice in
    one DAW.
*/

#pragma once

#include <juce_core/juce_core.h>

#include <string>
#include <vector>

namespace glorp
{

class Settings
{
public:
    Settings();

    /// Loads from disk, creating the directory and file if this is the first run.
    void load();

    /// Writes if anything changed. Safe to call often; it no-ops when clean.
    void saveIfChanged();

    juce::File getConfigDirectory() const;
    juce::File getConfigFile() const;
    juce::File getPresetDirectory() const;

    // -- kit locations ------------------------------------------------------
    juce::StringArray extraKitPaths;
    void addKitPath (const juce::String& path);
    void removeKitPath (const juce::String& path);

    /// Every root to scan: the built-in defaults plus the user's additions.
    std::vector<std::string> allSearchPaths() const;

    // -- last session ------------------------------------------------------
    juce::String lastKitPath;
    juce::String lastMapPreset;

    // -- editor ------------------------------------------------------------
    int editorWidth = 1180;
    int editorHeight = 860;
    bool editorMaximised = false;
    int themeIndex = 0;   ///< 0 = sketch (light), 1 = dark

    /// Most recent kits, most recent first, for the FILE menu.
    juce::StringArray recentKits;

    void pushRecentKit (const juce::String& path);
    static int maxRecentKits() { return 12; }

private:
    void writeState (juce::MemoryBlock& destination) const;
    void readState (const juce::MemoryBlock& source);

    bool dirty = false;
};

} // namespace glorp
