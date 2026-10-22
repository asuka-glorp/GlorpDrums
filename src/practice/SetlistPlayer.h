/*
    SetlistPlayer.h - Sequential Setlist playback & count-in transition coordinator for GlorpDrums.
*/

#pragma once

#include "BackingTrackPlayer.h"
#include "Metronome.h"
#include "SongDatabase.h"

#include <atomic>
#include <functional>

namespace glorp
{

class SetlistPlayer
{
public:
    enum class State
    {
        idle = 0,
        countingIn = 1,
        playingSong = 2,
        paused = 3,
        completed = 4
    };

    SetlistPlayer (SongDatabase& db, BackingTrackPlayer& trackPlayer, Metronome& metro);
    ~SetlistPlayer();

    State getState() const noexcept { return state.load (std::memory_order_relaxed); }
    int getCurrentSongIndex() const noexcept { return currentSongIndex.load (std::memory_order_relaxed); }
    bool isTransitionCountIn() const noexcept { return transitionCountIn.load (std::memory_order_relaxed); }

    void setAutoNextEnabled (bool enabled) noexcept { autoNextEnabled.store (enabled, std::memory_order_relaxed); }
    bool isAutoNextEnabled() const noexcept { return autoNextEnabled.load (std::memory_order_relaxed); }

    /// Selects and loads a song from the active setlist without starting playback.
    bool selectSong (int index);

    /// Starts playing the active setlist from startIndex (or currentSongIndex if startIndex < 0).
    bool startSetlistPlayback (int startIndex = -1, bool isBetweenSongTransition = false);

    /// Advances to the next song in the active setlist (with count-in if currently playing).
    bool nextSong (bool autoPlay = true);

    /// Steps back to the previous song in the active setlist.
    bool previousSong (bool autoPlay = true);

    /// Pauses setlist playback.
    void pause();

    /// Stops setlist playback and resets to the start of the current song.
    void stop();

    /// Checks whether the current backing track finished playing at EOF and, if so,
    /// triggers the count-in transition to the next song in the active setlist.
    /// Returns true if a transition or completion occurred.
    bool checkAndAdvanceOnEOF();

    /// Fired on the message thread (or synchronously in tests) whenever the active song index or state changes.
    std::function<void (int newSongIndex, const SongEntry& song, double effectiveBpm)> onSongLoaded;

private:
    void preloadUpcomingSong (int nextIndex);

    SongDatabase& database;
    BackingTrackPlayer& backingPlayer;
    Metronome& metronome;

    std::atomic<State> state { State::idle };
    std::atomic<int> currentSongIndex { 0 };
    std::atomic<bool> transitionCountIn { false };
    std::atomic<bool> autoNextEnabled { true };

    juce::ThreadPool preloadPool { 1 };
};

} // namespace glorp
