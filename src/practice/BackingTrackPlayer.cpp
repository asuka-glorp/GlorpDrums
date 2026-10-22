/*
    BackingTrackPlayer.cpp - Audio backing track engine for GlorpDrums practice and recording.
*/

#include "BackingTrackPlayer.h"

#include <algorithm>
#include <cmath>

namespace glorp
{

//==============================================================================
BackingTrackPlayer::BackingTrackPlayer()
{
    formatManager.registerBasicFormats();
   #if JUCE_USE_MP3AUDIOFORMAT
    formatManager.registerFormat (new juce::MP3AudioFormat(), false);
   #endif
   #if JUCE_USE_OGGVORBIS
    formatManager.registerFormat (new juce::OggVorbisAudioFormat(), false);
   #endif
   #if JUCE_USE_FLAC
    formatManager.registerFormat (new juce::FlacAudioFormat(), false);
   #endif
}

void BackingTrackPlayer::prepareToPlay (double sampleRate, int /*maxBlockSize*/)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
}

bool BackingTrackPlayer::decodeFileToBuffer (const juce::File& audioFile,
                                             juce::AudioBuffer<float>& destBuffer,
                                             int& outTargetLength)
{
    if (! audioFile.existsAsFile())
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (audioFile));
    if (reader == nullptr)
        return false;

    const int numChannels = std::min (2, static_cast<int> (reader->numChannels));
    const int64_t sourceLength = static_cast<int64_t> (reader->lengthInSamples);

    if (sourceLength <= 0 || numChannels <= 0)
        return false;

    juce::AudioBuffer<float> tempBuffer (numChannels, static_cast<int> (sourceLength));
    reader->read (&tempBuffer, 0, static_cast<int> (sourceLength), 0, true, true);

    const double sourceRate = reader->sampleRate;
    const double ratio = sourceRate / currentSampleRate;
    const int targetLength = static_cast<int> (std::round (static_cast<double> (sourceLength) / ratio));

    destBuffer.setSize (2, targetLength);
    destBuffer.clear();

    if (std::abs (ratio - 1.0) < 0.001)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            destBuffer.copyFrom (ch, 0, tempBuffer, ch, 0, targetLength);

        if (numChannels == 1)
            destBuffer.copyFrom (1, 0, destBuffer, 0, 0, targetLength);
    }
    else
    {
        for (int i = 0; i < targetLength; ++i)
        {
            const double srcPos = static_cast<double> (i) * ratio;
            const int idx0 = static_cast<int> (srcPos);
            const int idx1 = std::min (idx0 + 1, static_cast<int> (sourceLength - 1));
            const float frac = static_cast<float> (srcPos - static_cast<double> (idx0));

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float* in = tempBuffer.getReadPointer (ch);
                const float val = in[idx0] * (1.0f - frac) + in[idx1] * frac;
                destBuffer.setSample (ch, i, val);
            }

            if (numChannels == 1)
                destBuffer.setSample (1, i, destBuffer.getSample (0, i));
        }
    }

    outTargetLength = targetLength;
    return true;
}

bool BackingTrackPlayer::loadFile (const juce::File& audioFile)
{
    juce::AudioBuffer<float> newDecoded;
    int targetLength = 0;
    if (! decodeFileToBuffer (audioFile, newDecoded, targetLength))
        return false;

    std::lock_guard<std::mutex> lock (bufferMutex);
    decodedAudio = std::move (newDecoded);

    currentFile = audioFile;
    currentTrackName = audioFile.getFileName();
    totalSampleCount.store (targetLength, std::memory_order_relaxed);
    playheadSample.store (0, std::memory_order_relaxed);
    loopStartSample.store (0, std::memory_order_relaxed);
    loopEndSample.store (targetLength, std::memory_order_relaxed);
    trackFinishedEOF.store (false, std::memory_order_relaxed);
    trackLoaded.store (true, std::memory_order_release);
    playing.store (false, std::memory_order_relaxed);

    return true;
}

bool BackingTrackPlayer::preloadFile (const juce::File& audioFile)
{
    if (! audioFile.existsAsFile())
        return false;

    {
        std::lock_guard<std::mutex> plock (preloadMutex);
        if (preloadedFile == audioFile && preloadedSampleCount > 0)
            return true;
    }

    juce::AudioBuffer<float> newPreload;
    int targetLength = 0;
    if (! decodeFileToBuffer (audioFile, newPreload, targetLength))
        return false;

    std::lock_guard<std::mutex> plock (preloadMutex);
    preloadedAudio = std::move (newPreload);
    preloadedFile = audioFile;
    preloadedSampleCount = targetLength;
    return true;
}

bool BackingTrackPlayer::swapToPreloadedOrLoad (const juce::File& audioFile)
{
    {
        std::lock_guard<std::mutex> plock (preloadMutex);
        if (preloadedFile == audioFile && preloadedSampleCount > 0 && preloadedAudio.getNumSamples() == preloadedSampleCount)
        {
            std::lock_guard<std::mutex> lock (bufferMutex);
            playing.store (false, std::memory_order_relaxed);
            decodedAudio = std::move (preloadedAudio);
            const int targetLength = preloadedSampleCount;
            preloadedFile = juce::File();
            preloadedSampleCount = 0;

            currentFile = audioFile;
            currentTrackName = audioFile.getFileName();
            totalSampleCount.store (targetLength, std::memory_order_relaxed);
            playheadSample.store (0, std::memory_order_relaxed);
            loopStartSample.store (0, std::memory_order_relaxed);
            loopEndSample.store (targetLength, std::memory_order_relaxed);
            trackFinishedEOF.store (false, std::memory_order_relaxed);
            trackLoaded.store (true, std::memory_order_release);
            return true;
        }
    }

    return loadFile (audioFile);
}

void BackingTrackPlayer::clear()
{
    stop();
    trackLoaded.store (false, std::memory_order_release);
    std::lock_guard<std::mutex> lock (bufferMutex);
    decodedAudio.setSize (2, 0);
    totalSampleCount.store (0, std::memory_order_relaxed);
    currentTrackName.clear();
    currentFile = juce::File();
}

void BackingTrackPlayer::play() noexcept
{
    if (trackLoaded.load (std::memory_order_relaxed))
    {
        if (playheadSample.load (std::memory_order_relaxed) >= totalSampleCount.load (std::memory_order_relaxed))
            playheadSample.store (0, std::memory_order_relaxed);
        trackFinishedEOF.store (false, std::memory_order_relaxed);
        playing.store (true, std::memory_order_relaxed);
    }
}

void BackingTrackPlayer::pause() noexcept
{
    playing.store (false, std::memory_order_relaxed);
}

void BackingTrackPlayer::stop() noexcept
{
    playing.store (false, std::memory_order_relaxed);
    trackFinishedEOF.store (false, std::memory_order_relaxed);
    playheadSample.store (0, std::memory_order_relaxed);
}

void BackingTrackPlayer::setPlayheadSeconds (double seconds) noexcept
{
    const int64_t total = totalSampleCount.load (std::memory_order_relaxed);
    if (total <= 0)
        return;

    const int64_t sample = static_cast<int64_t> (seconds * currentSampleRate);
    playheadSample.store (std::clamp (sample, static_cast<int64_t> (0), total), std::memory_order_relaxed);
}

double BackingTrackPlayer::getPlayheadSeconds() const noexcept
{
    const int64_t sample = playheadSample.load (std::memory_order_relaxed);
    return (currentSampleRate > 0.0) ? (static_cast<double> (sample) / currentSampleRate) : 0.0;
}

double BackingTrackPlayer::getDurationSeconds() const noexcept
{
    const int64_t total = totalSampleCount.load (std::memory_order_relaxed);
    return (currentSampleRate > 0.0) ? (static_cast<double> (total) / currentSampleRate) : 0.0;
}

void BackingTrackPlayer::scrubTo (double seconds) noexcept
{
    const int64_t total = totalSampleCount.load (std::memory_order_relaxed);
    if (total <= 0)
        return;

    const int64_t sample = static_cast<int64_t> (seconds * currentSampleRate);
    const int64_t clamped = std::clamp (sample, static_cast<int64_t> (0), total - 1);
    playheadSample.store (clamped, std::memory_order_relaxed);

    if (! playing.load (std::memory_order_relaxed))
    {
        scrubPos.store (clamped, std::memory_order_relaxed);
        // Play an ~80ms audio grain snippet for scrubbing
        scrubRemaining.store (static_cast<int> (currentSampleRate * 0.08), std::memory_order_release);
    }
}

void BackingTrackPlayer::setLoopRangeSeconds (double startSec, double endSec) noexcept
{
    const int64_t total = totalSampleCount.load (std::memory_order_relaxed);
    const int64_t s = std::clamp (static_cast<int64_t> (startSec * currentSampleRate), static_cast<int64_t> (0), total);
    const int64_t e = std::clamp (static_cast<int64_t> (endSec * currentSampleRate), s, total);
    loopStartSample.store (s, std::memory_order_relaxed);
    loopEndSample.store (e, std::memory_order_relaxed);
}

double BackingTrackPlayer::getLoopStartSeconds() const noexcept
{
    return (currentSampleRate > 0.0) ? (static_cast<double> (loopStartSample.load (std::memory_order_relaxed)) / currentSampleRate) : 0.0;
}

double BackingTrackPlayer::getLoopEndSeconds() const noexcept
{
    return (currentSampleRate > 0.0) ? (static_cast<double> (loopEndSample.load (std::memory_order_relaxed)) / currentSampleRate) : 0.0;
}

//==============================================================================
// Real-time audio render callback - ZERO ALLOCATION
//==============================================================================
void BackingTrackPlayer::render (float* const* channels, int numChannels, int numSamples)
{
    if (! trackLoaded.load (std::memory_order_relaxed) || isMuted.load (std::memory_order_relaxed))
        return;

    const bool isPlay = playing.load (std::memory_order_relaxed);
    int scrubRem = scrubRemaining.load (std::memory_order_relaxed);

    if (! isPlay && scrubRem <= 0)
        return;

    const int64_t total = totalSampleCount.load (std::memory_order_relaxed);
    if (total <= 0)
        return;

    const float level = trackLevel.load (std::memory_order_relaxed);
    const float* const trackL = decodedAudio.getReadPointer (0);
    const float* const trackR = decodedAudio.getReadPointer (1);

    if (isPlay)
    {
        const bool isLoop = looping.load (std::memory_order_relaxed);
        const int64_t loopStart = loopStartSample.load (std::memory_order_relaxed);
        const int64_t loopEnd = loopEndSample.load (std::memory_order_relaxed);
        int64_t currentPos = playheadSample.load (std::memory_order_relaxed);

        for (int i = 0; i < numSamples; ++i)
        {
            if (currentPos >= total)
            {
                if (isLoop)
                {
                    currentPos = loopStart;
                }
                else
                {
                    playing.store (false, std::memory_order_relaxed);
                    trackFinishedEOF.store (true, std::memory_order_release);
                    break;
                }
            }
            else if (isLoop && currentPos >= loopEnd)
            {
                currentPos = loopStart;
            }

            const float l = trackL[currentPos] * level;
            const float r = trackR[currentPos] * level;

            if (numChannels >= 1 && channels[0] != nullptr)
                channels[0][i] += l;
            if (numChannels >= 2 && channels[1] != nullptr)
                channels[1][i] += r;

            ++currentPos;
        }

        playheadSample.store (currentPos, std::memory_order_relaxed);
    }
    else if (scrubRem > 0)
    {
        // Render audio scrub snippet with a short smooth envelope
        int64_t pos = scrubPos.load (std::memory_order_relaxed);
        for (int i = 0; i < numSamples && scrubRem > 0; ++i, --scrubRem)
        {
            if (pos < total)
            {
                float env = 1.0f;
                if (scrubRem < 256)
                    env = static_cast<float> (scrubRem) / 256.0f;

                const float l = trackL[pos] * level * env;
                const float r = trackR[pos] * level * env;

                if (numChannels >= 1 && channels[0] != nullptr)
                    channels[0][i] += l;
                if (numChannels >= 2 && channels[1] != nullptr)
                    channels[1][i] += r;

                ++pos;
            }
        }
        scrubPos.store (pos, std::memory_order_relaxed);
        scrubRemaining.store (scrubRem, std::memory_order_relaxed);
    }
}

} // namespace glorp
