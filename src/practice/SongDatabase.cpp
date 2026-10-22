/*
    SongDatabase.cpp - Persistent Song Library & Setlist manager for GlorpDrums Practice mode.
*/

#include "SongDatabase.h"

#include <algorithm>
#include <cmath>

namespace glorp
{

namespace
{
    juce::File defaultDatabaseFile()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile (".config/GlorpDrums/song_library.xml");
    }
}

//==============================================================================
SongDatabase::SongDatabase (const juce::File& customStorageFile)
    : storageFile (customStorageFile != juce::File() ? customStorageFile : defaultDatabaseFile())
{
    load();
    ensureDefaultSetlistExists();
}

SongDatabase::~SongDatabase()
{
    save();
}

void SongDatabase::setStorageFile (const juce::File& file)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        storageFile = file;
        songs.clear();
        setlists.clear();
        activeSetlistIndex = 0;
    }
    load();
    ensureDefaultSetlistExists();
}

juce::String SongDatabase::makeSongId (const juce::File& audioFile)
{
    const auto fullPath = audioFile.getFullPathName();
    const int64_t sz = audioFile.existsAsFile() ? audioFile.getSize() : 0;
    const auto combined = fullPath.toLowerCase() + "|" + juce::String (sz);
    return "song_" + juce::String::toHexString (static_cast<uint64_t> (combined.hashCode64()));
}

void SongDatabase::ensureDefaultSetlistExists()
{
    std::lock_guard<std::mutex> lock (mutex);
    if (setlists.empty())
    {
        Setlist def;
        def.id = "setlist_default";
        def.name = "Practice Setlist 1";
        setlists.push_back (std::move (def));
        activeSetlistIndex = 0;
    }
    else
    {
        activeSetlistIndex = std::clamp (activeSetlistIndex, 0, static_cast<int> (setlists.size()) - 1);
    }
}

int SongDatabase::findSongIndexUnlocked (const juce::File& audioFile) const
{
    const auto fullPath = audioFile.getFullPathName();
    for (size_t i = 0; i < songs.size(); ++i)
    {
        if (songs[i].filePath == fullPath)
            return static_cast<int> (i);
    }

    // Fallback: match by filename + fileSize if file was moved
    const auto fileName = audioFile.getFileName();
    const int64_t sz = audioFile.existsAsFile() ? audioFile.getSize() : -1;
    if (fileName.isNotEmpty())
    {
        for (size_t i = 0; i < songs.size(); ++i)
        {
            const juce::File existingFile (songs[i].filePath);
            if (existingFile.getFileName().equalsIgnoreCase (fileName))
            {
                if (sz <= 0 || songs[i].fileSize <= 0 || songs[i].fileSize == sz)
                    return static_cast<int> (i);
            }
        }
    }

    return -1;
}

int SongDatabase::findSongIndexByIdUnlocked (const juce::String& songId) const
{
    for (size_t i = 0; i < songs.size(); ++i)
    {
        if (songs[i].songId == songId)
            return static_cast<int> (i);
    }
    return -1;
}

//==============================================================================
SongEntry SongDatabase::findOrRegisterSong (const juce::File& audioFile, double durationSec)
{
    bool shouldSave = false;
    SongEntry result;

    {
        std::lock_guard<std::mutex> lock (mutex);
        const int idx = findSongIndexUnlocked (audioFile);
        if (idx >= 0)
        {
            auto& existing = songs[static_cast<size_t> (idx)];
            if (audioFile.existsAsFile() && existing.filePath != audioFile.getFullPathName())
            {
                existing.filePath = audioFile.getFullPathName();
                shouldSave = true;
            }
            if (durationSec > 0.0 && std::abs (existing.durationSeconds - durationSec) > 0.05)
            {
                existing.durationSeconds = durationSec;
                shouldSave = true;
            }
            result = existing;
        }
        else
        {
            SongEntry entry;
            entry.songId = makeSongId (audioFile);
            entry.filePath = audioFile.getFullPathName();
            entry.title = audioFile.getFileName();
            entry.bpm = 120.0;
            entry.firstBeatOffsetSec = 0.0;
            entry.beatsPerBar = 4;
            entry.backingLevel = 0.8f;
            entry.durationSeconds = std::max (0.0, durationSec);
            entry.fileSize = audioFile.existsAsFile() ? audioFile.getSize() : 0;
            entry.isConfigured = false;
            entry.lastUpdatedMs = juce::Time::currentTimeMillis();

            songs.push_back (entry);
            result = entry;
            shouldSave = true;
        }
    }

    if (shouldSave)
        save();

    return result;
}

bool SongDatabase::isSongConfigured (const juce::File& audioFile) const
{
    std::lock_guard<std::mutex> lock (mutex);
    const int idx = findSongIndexUnlocked (audioFile);
    return (idx >= 0) ? songs[static_cast<size_t> (idx)].isConfigured : false;
}

const SongEntry* SongDatabase::findSongById (const juce::String& songId) const
{
    std::lock_guard<std::mutex> lock (mutex);
    const int idx = findSongIndexByIdUnlocked (songId);
    return (idx >= 0) ? &songs[static_cast<size_t> (idx)] : nullptr;
}

const SongEntry* SongDatabase::findSongByFile (const juce::File& audioFile) const
{
    std::lock_guard<std::mutex> lock (mutex);
    const int idx = findSongIndexUnlocked (audioFile);
    return (idx >= 0) ? &songs[static_cast<size_t> (idx)] : nullptr;
}

SongEntry SongDatabase::updateSongConfig (const juce::File& audioFile,
                                          double bpm,
                                          double firstBeatOffsetSec,
                                          int beatsPerBar,
                                          float backingLevel,
                                          double durationSec)
{
    SongEntry updated;
    {
        std::lock_guard<std::mutex> lock (mutex);
        int idx = findSongIndexUnlocked (audioFile);
        if (idx < 0)
        {
            SongEntry entry;
            entry.songId = makeSongId (audioFile);
            entry.filePath = audioFile.getFullPathName();
            entry.title = audioFile.getFileName();
            entry.fileSize = audioFile.existsAsFile() ? audioFile.getSize() : 0;
            songs.push_back (entry);
            idx = static_cast<int> (songs.size()) - 1;
        }

        auto& entry = songs[static_cast<size_t> (idx)];
        entry.filePath = audioFile.getFullPathName();
        if (entry.title.isEmpty())
            entry.title = audioFile.getFileName();
        entry.bpm = std::clamp (bpm, 30.0, 300.0);
        entry.firstBeatOffsetSec = firstBeatOffsetSec;
        entry.beatsPerBar = std::clamp (beatsPerBar, 1, 16);
        entry.backingLevel = std::clamp (backingLevel, 0.0f, 2.0f);
        if (durationSec > 0.0)
            entry.durationSeconds = durationSec;
        if (audioFile.existsAsFile())
            entry.fileSize = audioFile.getSize();
        entry.isConfigured = true;
        entry.lastUpdatedMs = juce::Time::currentTimeMillis();

        updated = entry;
    }

    save();
    return updated;
}

void SongDatabase::updateSongDuration (const juce::File& audioFile, double durationSec)
{
    if (durationSec <= 0.0)
        return;

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock (mutex);
        const int idx = findSongIndexUnlocked (audioFile);
        if (idx >= 0)
        {
            auto& entry = songs[static_cast<size_t> (idx)];
            if (std::abs (entry.durationSeconds - durationSec) > 0.05)
            {
                entry.durationSeconds = durationSec;
                changed = true;
            }
        }
    }
    if (changed)
        save();
}

void SongDatabase::removeSongFromLibrary (const juce::String& songId)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        songs.erase (std::remove_if (songs.begin(), songs.end(),
                                     [&](const SongEntry& s) { return s.songId == songId; }),
                     songs.end());
    }
    save();
}

int SongDatabase::getNumConfiguredSongs() const noexcept
{
    std::lock_guard<std::mutex> lock (mutex);
    int count = 0;
    for (const auto& s : songs)
        if (s.isConfigured)
            ++count;
    return count;
}

//==============================================================================
void SongDatabase::setActiveSetlistIndex (int index)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (! setlists.empty())
            activeSetlistIndex = std::clamp (index, 0, static_cast<int> (setlists.size()) - 1);
    }
    save();
}

const Setlist& SongDatabase::getActiveSetlist() const
{
    std::lock_guard<std::mutex> lock (mutex);
    static const Setlist emptyFallback { "empty", "Default Setlist", {} };
    if (setlists.empty())
        return emptyFallback;
    const int idx = std::clamp (activeSetlistIndex, 0, static_cast<int> (setlists.size()) - 1);
    return setlists[static_cast<size_t> (idx)];
}

Setlist& SongDatabase::getActiveSetlistMutable()
{
    ensureDefaultSetlistExists();
    std::lock_guard<std::mutex> lock (mutex);
    activeSetlistIndex = std::clamp (activeSetlistIndex, 0, static_cast<int> (setlists.size()) - 1);
    return setlists[static_cast<size_t> (activeSetlistIndex)];
}

int SongDatabase::createSetlist (const juce::String& name)
{
    int newIdx = 0;
    {
        std::lock_guard<std::mutex> lock (mutex);
        Setlist s;
        s.id = "setlist_" + juce::String::toHexString (juce::Time::currentTimeMillis() + static_cast<int64_t> (setlists.size()));
        s.name = name.trim().isNotEmpty() ? name.trim() : ("Practice Setlist " + juce::String (setlists.size() + 1));
        setlists.push_back (std::move (s));
        activeSetlistIndex = static_cast<int> (setlists.size()) - 1;
        newIdx = activeSetlistIndex;
    }
    save();
    return newIdx;
}

void SongDatabase::renameActiveSetlist (const juce::String& newName)
{
    if (newName.trim().isEmpty())
        return;

    {
        std::lock_guard<std::mutex> lock (mutex);
        if (! setlists.empty())
        {
            const int idx = std::clamp (activeSetlistIndex, 0, static_cast<int> (setlists.size()) - 1);
            setlists[static_cast<size_t> (idx)].name = newName.trim();
        }
    }
    save();
}

void SongDatabase::deleteSetlist (int index)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (index >= 0 && index < static_cast<int> (setlists.size()))
            setlists.erase (setlists.begin() + index);

        if (setlists.empty())
        {
            Setlist def;
            def.id = "setlist_default";
            def.name = "Practice Setlist 1";
            setlists.push_back (std::move (def));
            activeSetlistIndex = 0;
        }
        else
        {
            activeSetlistIndex = std::clamp (activeSetlistIndex, 0, static_cast<int> (setlists.size()) - 1);
        }
    }
    save();
}

int SongDatabase::addSongToActiveSetlist (const juce::File& audioFile, double durationSec, double customBpmOverride)
{
    const auto song = findOrRegisterSong (audioFile, durationSec);
    return addSongIdToActiveSetlist (song.songId, customBpmOverride);
}

int SongDatabase::addSongIdToActiveSetlist (const juce::String& songId, double customBpmOverride)
{
    ensureDefaultSetlistExists();
    int addedIdx = -1;
    {
        std::lock_guard<std::mutex> lock (mutex);
        const int sIdx = findSongIndexByIdUnlocked (songId);
        if (sIdx < 0)
            return -1;

        auto& active = setlists[static_cast<size_t> (activeSetlistIndex)];
        SetlistItem item;
        item.songId = songs[static_cast<size_t> (sIdx)].songId;
        item.filePath = songs[static_cast<size_t> (sIdx)].filePath;
        item.customBpmOverride = customBpmOverride > 0.0 ? std::clamp (customBpmOverride, 30.0, 300.0) : 0.0;
        active.items.push_back (item);
        addedIdx = static_cast<int> (active.items.size()) - 1;
    }
    save();
    return addedIdx;
}

void SongDatabase::removeItemFromActiveSetlist (int itemIndex)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (setlists.empty())
            return;
        auto& items = setlists[static_cast<size_t> (activeSetlistIndex)].items;
        if (itemIndex >= 0 && itemIndex < static_cast<int> (items.size()))
            items.erase (items.begin() + itemIndex);
    }
    save();
}

bool SongDatabase::moveItemInActiveSetlist (int fromIndex, int toIndex)
{
    bool moved = false;
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (setlists.empty())
            return false;
        auto& items = setlists[static_cast<size_t> (activeSetlistIndex)].items;
        const int n = static_cast<int> (items.size());
        if (fromIndex >= 0 && fromIndex < n && toIndex >= 0 && toIndex < n && fromIndex != toIndex)
        {
            auto item = items[static_cast<size_t> (fromIndex)];
            items.erase (items.begin() + fromIndex);
            items.insert (items.begin() + toIndex, item);
            moved = true;
        }
    }
    if (moved)
        save();
    return moved;
}

void SongDatabase::setItemCustomBpm (int itemIndex, double customBpm)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (setlists.empty())
            return;
        auto& items = setlists[static_cast<size_t> (activeSetlistIndex)].items;
        if (itemIndex >= 0 && itemIndex < static_cast<int> (items.size()))
            items[static_cast<size_t> (itemIndex)].customBpmOverride = (customBpm > 0.0) ? std::clamp (customBpm, 30.0, 300.0) : 0.0;
    }
    save();
}

double SongDatabase::getEffectiveBpmForItem (const SetlistItem& item) const
{
    if (item.customBpmOverride > 0.0)
        return item.customBpmOverride;

    std::lock_guard<std::mutex> lock (mutex);
    const int idx = findSongIndexByIdUnlocked (item.songId);
    if (idx >= 0)
        return songs[static_cast<size_t> (idx)].bpm;

    return 120.0;
}

//==============================================================================
bool SongDatabase::load()
{
    std::lock_guard<std::mutex> lock (mutex);
    songs.clear();
    setlists.clear();
    activeSetlistIndex = 0;

    if (! storageFile.existsAsFile())
        return false;

    std::unique_ptr<juce::XmlElement> root = juce::XmlDocument::parse (storageFile);
    if (root == nullptr || ! root->hasTagName ("GlorpSongDatabase"))
        return false;

    if (auto* libElem = root->getChildByName ("SongLibrary"))
    {
        for (auto* songElem : libElem->getChildWithTagNameIterator ("Song"))
        {
            SongEntry s;
            s.songId = songElem->getStringAttribute ("id");
            s.filePath = songElem->getStringAttribute ("filePath");
            s.title = songElem->getStringAttribute ("title", juce::File (s.filePath).getFileName());
            s.bpm = songElem->getDoubleAttribute ("bpm", 120.0);
            s.firstBeatOffsetSec = songElem->getDoubleAttribute ("firstBeatOffsetSec", 0.0);
            s.beatsPerBar = songElem->getIntAttribute ("beatsPerBar", 4);
            s.backingLevel = static_cast<float> (songElem->getDoubleAttribute ("backingLevel", 0.8));
            s.durationSeconds = songElem->getDoubleAttribute ("durationSeconds", 0.0);
            s.fileSize = songElem->getStringAttribute ("fileSize", "0").getLargeIntValue();
            s.isConfigured = songElem->getBoolAttribute ("isConfigured", false);
            s.lastUpdatedMs = songElem->getStringAttribute ("lastUpdatedMs", "0").getLargeIntValue();

            if (s.songId.isEmpty() && s.filePath.isNotEmpty())
                s.songId = makeSongId (juce::File (s.filePath));

            if (s.songId.isNotEmpty())
                songs.push_back (std::move (s));
        }
    }

    if (auto* setsElem = root->getChildByName ("Setlists"))
    {
        activeSetlistIndex = setsElem->getIntAttribute ("activeIndex", 0);
        for (auto* setElem : setsElem->getChildWithTagNameIterator ("Setlist"))
        {
            Setlist set;
            set.id = setElem->getStringAttribute ("id", "setlist_" + juce::String (setlists.size() + 1));
            set.name = setElem->getStringAttribute ("name", "Practice Setlist");

            for (auto* itemElem : setElem->getChildWithTagNameIterator ("Item"))
            {
                SetlistItem item;
                item.songId = itemElem->getStringAttribute ("songId");
                item.filePath = itemElem->getStringAttribute ("filePath");
                item.customBpmOverride = itemElem->getDoubleAttribute ("customBpmOverride", 0.0);
                item.countInBarsOverride = itemElem->getIntAttribute ("countInBarsOverride", 0);

                // Resolve songId by filePath if needed
                if (item.songId.isEmpty() && item.filePath.isNotEmpty())
                {
                    const int sIdx = findSongIndexUnlocked (juce::File (item.filePath));
                    if (sIdx >= 0)
                        item.songId = songs[static_cast<size_t> (sIdx)].songId;
                }

                if (item.songId.isNotEmpty() || item.filePath.isNotEmpty())
                    set.items.push_back (std::move (item));
            }

            setlists.push_back (std::move (set));
        }
    }

    if (! setlists.empty())
        activeSetlistIndex = std::clamp (activeSetlistIndex, 0, static_cast<int> (setlists.size()) - 1);

    return true;
}

bool SongDatabase::save() const
{
    std::lock_guard<std::mutex> lock (mutex);
    if (storageFile == juce::File())
        return false;

    storageFile.getParentDirectory().createDirectory();

    juce::XmlElement root ("GlorpSongDatabase");
    root.setAttribute ("version", 1);

    auto* libElem = root.createNewChildElement ("SongLibrary");
    for (const auto& s : songs)
    {
        auto* songElem = libElem->createNewChildElement ("Song");
        songElem->setAttribute ("id", s.songId);
        songElem->setAttribute ("filePath", s.filePath);
        songElem->setAttribute ("title", s.title);
        songElem->setAttribute ("bpm", s.bpm);
        songElem->setAttribute ("firstBeatOffsetSec", s.firstBeatOffsetSec);
        songElem->setAttribute ("beatsPerBar", s.beatsPerBar);
        songElem->setAttribute ("backingLevel", static_cast<double> (s.backingLevel));
        songElem->setAttribute ("durationSeconds", s.durationSeconds);
        songElem->setAttribute ("fileSize", juce::String (s.fileSize));
        songElem->setAttribute ("isConfigured", s.isConfigured);
        songElem->setAttribute ("lastUpdatedMs", juce::String (s.lastUpdatedMs));
    }

    auto* setsElem = root.createNewChildElement ("Setlists");
    setsElem->setAttribute ("activeIndex", activeSetlistIndex);
    for (const auto& set : setlists)
    {
        auto* setElem = setsElem->createNewChildElement ("Setlist");
        setElem->setAttribute ("id", set.id);
        setElem->setAttribute ("name", set.name);

        for (const auto& item : set.items)
        {
            auto* itemElem = setElem->createNewChildElement ("Item");
            itemElem->setAttribute ("songId", item.songId);
            itemElem->setAttribute ("filePath", item.filePath);
            if (item.customBpmOverride > 0.0)
                itemElem->setAttribute ("customBpmOverride", item.customBpmOverride);
            if (item.countInBarsOverride > 0)
                itemElem->setAttribute ("countInBarsOverride", item.countInBarsOverride);
        }
    }

    return root.writeTo (storageFile);
}

} // namespace glorp
