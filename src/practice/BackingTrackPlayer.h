/*
    BackingTrackPlayer.h - Audio backing track engine for GlorpDrums practice and recording.
    Loads WAV, MP3, FLAC, AIFF, and OGG with automatic host-rate resampling and zero-latency playback.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace glorp
{

class BackingTrackPlayer
{
public:
    BackingTrackPlayer();
    ~BackingTrackPlayer() = default;

    void prepareToPlay (double sampleRate, int maxBlockSize);

    /// Loads an audio file (WAV, MP3, FLAC, AIFF, OGG), decoding and resampling to currentSampleRate.
    bool loadFile (const juce::File& audioFile);

    /// Preloads an upcoming setlist track into a standby buffer off the audio thread.
    bool preloadFile (const juce::File& audioFile);

    /// Swaps in the preloaded standby buffer if it matches audioFile, otherwise falls back to loadFile().
    bool swapToPreloadedOrLoad (const juce::File& audioFile);

    /// Unloads current track and resets playback state.
    void clear();

    bool hasTrack() const noexcept { return trackLoaded.load (std::memory_order_relaxed); }
    juce::String getTrackName() const { return currentTrackName; }
    juce::File getLoadedFile() const { return currentFile; }

    /// Returns true once when a non-looping track naturally finishes playback at EOF.
    bool consumeTrackFinishedEOF() noexcept { return trackFinishedEOF.exchange (false, std::memory_order_acq_rel); }
    bool hasFinishedEOF() const noexcept { return trackFinishedEOF.load (std::memory_order_acquire); }

    // -- Transport controls ---------------------------------------------------
    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;
    bool isPlaying() const noexcept { return playing.load (std::memory_order_relaxed); }

    void setPlayheadSeconds (double seconds) noexcept;
    double getPlayheadSeconds() const noexcept;
    double getDurationSeconds() const noexcept;

    /// Real-time audio scrub when dragging the waveform playhead
    void scrubTo (double seconds) noexcept;

    void setLooping (bool loop) noexcept { looping.store (loop, std::memory_order_relaxed); }
    bool isLooping() const noexcept { return looping.load (std::memory_order_relaxed); }

    void setLoopRangeSeconds (double startSec, double endSec) noexcept;
    double getLoopStartSeconds() const noexcept;
    double getLoopEndSeconds() const noexcept;

    // -- Audio levels ---------------------------------------------------------
    void setLevel (float level) noexcept { trackLevel.store (std::clamp (level, 0.0f, 2.0f), std::memory_order_relaxed); }
    float getLevel() const noexcept { return trackLevel.load (std::memory_order_relaxed); }

    void setMuted (bool mute) noexcept { isMuted.store (mute, std::memory_order_relaxed); }
    bool getMuted() const noexcept { return isMuted.load (std::memory_order_relaxed); }

    /// Access to resampled in-memory buffer for waveform display and tempo detection.
    const juce::AudioBuffer<float>& getDecodedBuffer() const noexcept { return decodedAudio; }

    /// Real-time playback renderer called inside processBlock(). Zero allocations.
    void render (float* const* channels, int numChannels, int numSamples);

private:
    double currentSampleRate = 44100.0;
    juce::AudioFormatManager formatManager;

    std::atomic<bool> trackLoaded { false };
    std::atomic<bool> playing { false };
    std::atomic<bool> looping { false };
    std::atomic<bool> isMuted { false };
    std::atomic<bool> trackFinishedEOF { false };
    std::atomic<float> trackLevel { 0.8f };

    std::atomic<int64_t> playheadSample { 0 };
    std::atomic<int64_t> totalSampleCount { 0 };
    std::atomic<int64_t> loopStartSample { 0 };
    std::atomic<int64_t> loopEndSample { 0 };

    std::atomic<int64_t> scrubPos { -1 };
    std::atomic<int> scrubRemaining { 0 };

    bool decodeFileToBuffer (const juce::File& audioFile, juce::AudioBuffer<float>& destBuffer, int& outTargetLength);

    juce::AudioBuffer<float> decodedAudio;
    juce::AudioBuffer<float> preloadedAudio;
    juce::File preloadedFile;
    int preloadedSampleCount = 0;
    mutable std::mutex bufferMutex;
    mutable std::mutex preloadMutex;

    juce::String currentTrackName;
    juce::File currentFile;
};

} // namespace glorp
