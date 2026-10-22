/*
    SongDatabase.h - Persistent Song Library & Setlist manager for GlorpDrums Practice mode.
    Remembers per-song BPM, Beat-1 grid offset, time signature, and backing track level across setlists.
*/

#pragma once

#include <juce_core/juce_core.h>
#include <mutex>
#include <vector>

namespace glorp
{

struct SongEntry
{
    juce::String songId;
    juce::String filePath;
    juce::String title;
    double bpm = 120.0;
    double firstBeatOffsetSec = 0.0;
    int beatsPerBar = 4;
    float backingLevel = 0.8f;
    double durationSeconds = 0.0;
    int64_t fileSize = 0;
    bool isConfigured = false;
    int64_t lastUpdatedMs = 0;
};

struct SetlistItem
{
    juce::String songId;
    juce::String filePath;
    double customBpmOverride = 0.0; // 0.0 = use SongEntry::bpm from SongDatabase
    int countInBarsOverride = 0;    // 0 = use global between-song count-in setting
};

struct Setlist
{
    juce::String id;
    juce::String name;
    std::vector<SetlistItem> items;
};

class SongDatabase
{
public:
    explicit SongDatabase (const juce::File& customStorageFile = juce::File());
    ~SongDatabase();

    /// Reloads song library and setlists from XML storage on disk.
    bool load();

    /// Saves song library and setlists to XML storage on disk.
    bool save() const;

    /// Redirects persistence to a custom XML file (used by headless unit tests).
    void setStorageFile (const juce::File& file);
    juce::File getStorageFile() const { return storageFile; }

    // -- Persistent Song Library ----------------------------------------------
    /// Looks up an audio file in the library by canonical path or (filename + fileSize).
    /// If not found, registers a new SongEntry and returns it.
    SongEntry findOrRegisterSong (const juce::File& audioFile, double durationSec = 0.0);

    /// Returns true if the audio file exists in the database and has isConfigured == true.
    bool isSongConfigured (const juce::File& audioFile) const;

    /// Finds a song by its songId. Returns nullptr if not found.
    const SongEntry* findSongById (const juce::String& songId) const;
    const SongEntry* findSongByFile (const juce::File& audioFile) const;

    /// Updates or inserts a song's tempo, Beat-1 offset, meter, and level, marking it configured and saving to disk.
    SongEntry updateSongConfig (const juce::File& audioFile,
                                double bpm,
                                double firstBeatOffsetSec,
                                int beatsPerBar,
                                float backingLevel,
                                double durationSec = -1.0);

    /// Updates only the duration of a registered song without marking an unconfigured song as user-configured.
    void updateSongDuration (const juce::File& audioFile, double durationSec);

    /// Removes a song from the persistent library.
    void removeSongFromLibrary (const juce::String& songId);

    const std::vector<SongEntry>& getSongs() const noexcept { return songs; }
    int getNumConfiguredSongs() const noexcept;

    // -- Setlist Management ---------------------------------------------------
    const std::vector<Setlist>& getSetlists() const noexcept { return setlists; }
    int getActiveSetlistIndex() const noexcept { return activeSetlistIndex; }
    void setActiveSetlistIndex (int index);

    const Setlist& getActiveSetlist() const;
    Setlist& getActiveSetlistMutable();

    /// Creates a new setlist, selects it as active, and saves.
    int createSetlist (const juce::String& name);

    /// Renames the active setlist and saves.
    void renameActiveSetlist (const juce::String& newName);

    /// Deletes a setlist by index (ensuring at least 1 setlist always remains).
    void deleteSetlist (int index);

    /// Adds a song (by file or songId) to the active setlist and saves.
    int addSongToActiveSetlist (const juce::File& audioFile, double durationSec = 0.0, double customBpmOverride = 0.0);
    int addSongIdToActiveSetlist (const juce::String& songId, double customBpmOverride = 0.0);

    /// Removes an item at itemIndex from the active setlist and saves.
    void removeItemFromActiveSetlist (int itemIndex);

    /// Moves an item within the active setlist (e.g., up/down reorder) and saves.
    bool moveItemInActiveSetlist (int fromIndex, int toIndex);

    /// Sets an optional custom BPM override on a specific setlist item (0.0 = use Song DB BPM).
    void setItemCustomBpm (int itemIndex, double customBpm);

    /// Resolves the effective BPM for an item in the active setlist (custom override if > 0, else Song DB BPM).
    double getEffectiveBpmForItem (const SetlistItem& item) const;

    static juce::String makeSongId (const juce::File& audioFile);

private:
    void ensureDefaultSetlistExists();
    int findSongIndexUnlocked (const juce::File& audioFile) const;
    int findSongIndexByIdUnlocked (const juce::String& songId) const;

    juce::File storageFile;
    mutable std::mutex mutex;
    std::vector<SongEntry> songs;
    std::vector<Setlist> setlists;
    int activeSetlistIndex = 0;
};

} // namespace glorp
