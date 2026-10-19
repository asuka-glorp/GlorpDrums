/*
    AudioRecorder.cpp - Lock-free 24-bit WAV background audio recorder for GlorpDrums.
*/

#include "AudioRecorder.h"

#include <algorithm>

namespace glorp
{

//==============================================================================
AudioRecorder::AudioRecorder()
    : juce::Thread ("GlorpAudioRecorderThread"),
      fifo (fifoCapacity)
{
    ringBuffer.setSize (2, fifoCapacity);
    ringBuffer.clear();
}

AudioRecorder::~AudioRecorder()
{
    stopRecording();
}

void AudioRecorder::prepareToPlay (double sampleRate, int /*maxBlockSize*/)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
}

bool AudioRecorder::startRecording (const juce::File& targetWavFile)
{
    stopRecording();

    currentOutputFile = targetWavFile;
    if (currentOutputFile.exists())
        currentOutputFile.deleteFile();

    auto outStream = currentOutputFile.createOutputStream();
    if (outStream == nullptr)
        return false;

    juce::WavAudioFormat wavFormat;
    auto writer = wavFormat.createWriterFor (outStream.get(),
                                             currentSampleRate,
                                             2,
                                             24, // 24-bit PCM
                                             {},
                                             0);

    if (writer == nullptr)
        return false;

    outStream.release(); // AudioFormatWriter takes ownership

    {
        const juce::ScopedLock sl (writerLock);
        wavWriter.reset (writer);
    }

    fifo.reset();
    totalRecordedSamples.store (0, std::memory_order_relaxed);
    recordingActive.store (true, std::memory_order_release);

    startThread (juce::Thread::Priority::normal);
    return true;
}

void AudioRecorder::stopRecording()
{
    if (! recordingActive.load (std::memory_order_relaxed))
        return;

    recordingActive.store (false, std::memory_order_release);
    signalThreadShouldExit();
    notify();
    stopThread (3000);

    // Flush any remaining samples in the ring buffer
    const juce::ScopedLock sl (writerLock);
    if (wavWriter != nullptr)
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        const int numReady = fifo.getNumReady();
        if (numReady > 0)
        {
            fifo.prepareToRead (numReady, start1, size1, start2, size2);
            if (size1 > 0)
                wavWriter->writeFromAudioSampleBuffer (ringBuffer, start1, size1);
            if (size2 > 0)
                wavWriter->writeFromAudioSampleBuffer (ringBuffer, start2, size2);
            fifo.finishedRead (size1 + size2);
        }

        wavWriter.reset();
    }
}

double AudioRecorder::getRecordedSeconds() const noexcept
{
    const int64_t samples = totalRecordedSamples.load (std::memory_order_relaxed);
    return (currentSampleRate > 0.0) ? (static_cast<double> (samples) / currentSampleRate) : 0.0;
}

//==============================================================================
// Real-time audio thread callback - ZERO ALLOCATION
//==============================================================================
void AudioRecorder::pushAudio (const float* const* channelPointers, int numChannels, int numSamples)
{
    if (! recordingActive.load (std::memory_order_relaxed) || numSamples <= 0 || channelPointers == nullptr)
        return;

    const int freeSpace = fifo.getFreeSpace();
    const int toWrite = std::min (numSamples, freeSpace);

    if (toWrite <= 0)
        return; // FIFO overrun safeguard

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (toWrite, start1, size1, start2, size2);

    const int channelsToCopy = std::min (numChannels, 2);

    if (size1 > 0)
    {
        for (int ch = 0; ch < channelsToCopy; ++ch)
        {
            if (channelPointers[ch] != nullptr)
                ringBuffer.copyFrom (ch, start1, channelPointers[ch], size1);
            else
                ringBuffer.clear (ch, start1, size1);
        }
        if (channelsToCopy == 1) // Duplicate mono to right channel
            ringBuffer.copyFrom (1, start1, ringBuffer, 0, start1, size1);
    }

    if (size2 > 0)
    {
        for (int ch = 0; ch < channelsToCopy; ++ch)
        {
            if (channelPointers[ch] != nullptr)
                ringBuffer.copyFrom (ch, start2, channelPointers[ch] + size1, size2);
            else
                ringBuffer.clear (ch, start2, size2);
        }
        if (channelsToCopy == 1)
            ringBuffer.copyFrom (1, start2, ringBuffer, 0, start2, size2);
    }

    fifo.finishedWrite (size1 + size2);
    totalRecordedSamples.fetch_add (size1 + size2, std::memory_order_relaxed);

    notify();
}

//==============================================================================
// Background worker thread
//==============================================================================
void AudioRecorder::run()
{
    while (! threadShouldExit())
    {
        const int numReady = fifo.getNumReady();

        if (numReady >= 512 || (threadShouldExit() && numReady > 0))
        {
            int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
            const int toRead = std::min (numReady, 4096);
            fifo.prepareToRead (toRead, start1, size1, start2, size2);

            const juce::ScopedLock sl (writerLock);
            if (wavWriter != nullptr)
            {
                if (size1 > 0)
                    wavWriter->writeFromAudioSampleBuffer (ringBuffer, start1, size1);
                if (size2 > 0)
                    wavWriter->writeFromAudioSampleBuffer (ringBuffer, start2, size2);
            }

            fifo.finishedRead (size1 + size2);
        }
        else
        {
            wait (25);
        }
    }
}

} // namespace glorp
