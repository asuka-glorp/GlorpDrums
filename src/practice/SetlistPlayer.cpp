/*
    SetlistPlayer.cpp - Sequential Setlist playback & count-in transition coordinator for GlorpDrums.
*/

#include "SetlistPlayer.h"

#include <algorithm>

namespace glorp
{

SetlistPlayer::SetlistPlayer (SongDatabase& db, BackingTrackPlayer& trackPlayer, Metronome& metro)
    : database (db), backingPlayer (trackPlayer), metronome (metro)
{
}

SetlistPlayer::~SetlistPlayer()
{
    preloadPool.removeAllJobs (true, 2000);
}

void SetlistPlayer::preloadUpcomingSong (int nextIndex)
{
    const auto setlist = database.getActiveSetlist();
    if (nextIndex < 0 || nextIndex >= static_cast<int> (setlist.items.size()))
        return;

    const auto nextItem = setlist.items[static_cast<size_t> (nextIndex)];
    juce::File nextFile (nextItem.filePath);
    if (! nextFile.existsAsFile())
    {
        if (const auto* s = database.findSongById (nextItem.songId))
            nextFile = juce::File (s->filePath);
    }

    if (nextFile.existsAsFile())
    {
        preloadPool.removeAllJobs (false, 0);
        preloadPool.addJob ([this, nextFile]()
        {
            backingPlayer.preloadFile (nextFile);
        });
    }
}

bool SetlistPlayer::selectSong (int index)
{
    const auto setlist = database.getActiveSetlist();
    if (index < 0 || index >= static_cast<int> (setlist.items.size()))
        return false;

    metronome.stop();
    backingPlayer.stop();
    transitionCountIn.store (false, std::memory_order_relaxed);
    state.store (State::idle, std::memory_order_relaxed);

    const auto& item = setlist.items[static_cast<size_t> (index)];
    const SongEntry* entryPtr = database.findSongById (item.songId);
    juce::File audioFile (item.filePath);
    if (entryPtr != nullptr && juce::File (entryPtr->filePath).existsAsFile())
        audioFile = juce::File (entryPtr->filePath);

    SongEntry loadedEntry;
    if (entryPtr != nullptr)
        loadedEntry = *entryPtr;

    if (audioFile.existsAsFile())
    {
        backingPlayer.swapToPreloadedOrLoad (audioFile);
        const double dur = backingPlayer.getDurationSeconds();
        database.updateSongDuration (audioFile, dur);
        loadedEntry.durationSeconds = dur;
    }

    const double effectiveBpm = (item.customBpmOverride > 0.0)
                                    ? item.customBpmOverride
                                    : (loadedEntry.bpm > 0.0 ? loadedEntry.bpm : 120.0);

    metronome.setBpm (effectiveBpm);
    metronome.setGridOffset (loadedEntry.firstBeatOffsetSec);
    metronome.setTimeSignature (std::max (1, loadedEntry.beatsPerBar), 4);
    backingPlayer.setLevel (loadedEntry.backingLevel);

    currentSongIndex.store (index, std::memory_order_relaxed);
    preloadUpcomingSong (index + 1);

    if (onSongLoaded != nullptr)
        onSongLoaded (index, loadedEntry, effectiveBpm);

    return true;
}

bool SetlistPlayer::startSetlistPlayback (int startIndex, bool isBetweenSongTransition)
{
    const auto setlist = database.getActiveSetlist();
    if (setlist.items.empty())
        return false;

    int targetIdx = (startIndex >= 0) ? startIndex : currentSongIndex.load (std::memory_order_relaxed);
    targetIdx = std::clamp (targetIdx, 0, static_cast<int> (setlist.items.size()) - 1);

    // If we are resuming the already-loaded song from a paused position (and not transitioning), don't reload from disk
    const auto& item = setlist.items[static_cast<size_t> (targetIdx)];
    const SongEntry* entryPtr = database.findSongById (item.songId);
    juce::File audioFile (item.filePath);
    if (entryPtr != nullptr && juce::File (entryPtr->filePath).existsAsFile())
        audioFile = juce::File (entryPtr->filePath);

    const bool needLoad = isBetweenSongTransition
                          || (targetIdx != currentSongIndex.load (std::memory_order_relaxed))
                          || (! backingPlayer.hasTrack())
                          || (backingPlayer.getLoadedFile() != audioFile);

    if (needLoad)
    {
        if (! selectSong (targetIdx))
            return false;
    }
    else
    {
        // Ensure BPM, offset, and time signature match the current song
        SongEntry loadedEntry;
        if (entryPtr != nullptr)
            loadedEntry = *entryPtr;
        const double effectiveBpm = (item.customBpmOverride > 0.0)
                                        ? item.customBpmOverride
                                        : (loadedEntry.bpm > 0.0 ? loadedEntry.bpm : 120.0);
        metronome.setBpm (effectiveBpm);
        metronome.setGridOffset (loadedEntry.firstBeatOffsetSec);
        metronome.setTimeSignature (std::max (1, loadedEntry.beatsPerBar), 4);
    }

    // Between songs in a setlist, always deliver a count-in (at least 1 bar if CountInMode was off)
    auto countMode = metronome.getCountInMode();
    const bool restoreCountOffAfter = (isBetweenSongTransition && countMode == Metronome::CountInMode::off);
    if (restoreCountOffAfter)
        metronome.setCountInMode (Metronome::CountInMode::oneBar);

    if (metronome.getCountInMode() != Metronome::CountInMode::off)
    {
        state.store (State::countingIn, std::memory_order_relaxed);
        transitionCountIn.store (isBetweenSongTransition, std::memory_order_relaxed);
        metronome.setTransportPlaying (false);

        metronome.onCountInComplete = [this, restoreCountOffAfter]()
        {
            if (restoreCountOffAfter)
                metronome.setCountInMode (Metronome::CountInMode::off);
            transitionCountIn.store (false, std::memory_order_relaxed);
            state.store (State::playingSong, std::memory_order_relaxed);
            backingPlayer.play();
            metronome.setTransportPlaying (true);
        };

        const double startPosSec = isBetweenSongTransition ? 0.0 : backingPlayer.getPlayheadSeconds();
        metronome.startCountIn (startPosSec);
    }
    else
    {
        transitionCountIn.store (false, std::memory_order_relaxed);
        state.store (State::playingSong, std::memory_order_relaxed);
        backingPlayer.play();
        metronome.setTransportPlaying (true);
    }

    preloadUpcomingSong (targetIdx + 1);
    return true;
}

bool SetlistPlayer::nextSong (bool autoPlay)
{
    const auto setlist = database.getActiveSetlist();
    const int nextIdx = currentSongIndex.load (std::memory_order_relaxed) + 1;
    if (nextIdx >= static_cast<int> (setlist.items.size()))
        return false;

    if (autoPlay)
        return startSetlistPlayback (nextIdx, true);

    return selectSong (nextIdx);
}

bool SetlistPlayer::previousSong (bool autoPlay)
{
    const auto setlist = database.getActiveSetlist();
    if (setlist.items.empty())
        return false;

    // If more than 3 seconds into the song, rewind to 0.0s of current song first
    if (backingPlayer.getPlayheadSeconds() > 3.0)
    {
        backingPlayer.setPlayheadSeconds (0.0);
        return true;
    }

    const int prevIdx = std::max (0, currentSongIndex.load (std::memory_order_relaxed) - 1);
    if (autoPlay)
        return startSetlistPlayback (prevIdx, true);

    return selectSong (prevIdx);
}

void SetlistPlayer::pause()
{
    backingPlayer.pause();
    metronome.setTransportPlaying (false);
    metronome.stop();
    transitionCountIn.store (false, std::memory_order_relaxed);
    state.store (State::paused, std::memory_order_relaxed);
}

void SetlistPlayer::stop()
{
    metronome.stop();
    backingPlayer.stop();
    transitionCountIn.store (false, std::memory_order_relaxed);
    state.store (State::idle, std::memory_order_relaxed);
}

bool SetlistPlayer::checkAndAdvanceOnEOF()
{
    if (! backingPlayer.consumeTrackFinishedEOF())
        return false;

    metronome.setTransportPlaying (false);

    if (! autoNextEnabled.load (std::memory_order_relaxed))
    {
        state.store (State::idle, std::memory_order_relaxed);
        return false;
    }

    const auto setlist = database.getActiveSetlist();
    const int curIdx = currentSongIndex.load (std::memory_order_relaxed);
    const int nextIdx = curIdx + 1;

    if (nextIdx < static_cast<int> (setlist.items.size()))
    {
        startSetlistPlayback (nextIdx, true);
        return true;
    }

    transitionCountIn.store (false, std::memory_order_relaxed);
    state.store (State::completed, std::memory_order_relaxed);
    return true;
}

} // namespace glorp
