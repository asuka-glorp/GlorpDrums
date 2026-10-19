/*
    AudioRecorder.h - Lock-free 24-bit WAV background audio recorder for GlorpDrums.
    Guarantees zero heap allocation and zero disk I/O on the real-time audio thread.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

namespace glorp
{

class AudioRecorder : private juce::Thread
{
public:
    enum class RecordingSource
    {
        drumsOnly = 0, // Default: captures pure drum performance for DAW production
        fullMix = 1    // Captures drums + backing track
    };

    AudioRecorder();
    ~AudioRecorder() override;

    void prepareToPlay (double sampleRate, int maxBlockSize);

    /// Start recording to the specified destination file.
    bool startRecording (const juce::File& targetWavFile);

    /// Stop recording, flush buffer, and finalize the WAV file headers.
    void stopRecording();

    bool isRecording() const noexcept { return recordingActive.load (std::memory_order_relaxed); }

    void setSource (RecordingSource source) noexcept { recordSource.store (source, std::memory_order_relaxed); }
    RecordingSource getSource() const noexcept { return recordSource.load (std::memory_order_relaxed); }

    double getRecordedSeconds() const noexcept;
    juce::File getRecordedFile() const { return currentOutputFile; }

    /// Called from processBlock(). Copies rendered audio into lock-free ring buffer.
    void pushAudio (const float* const* channelPointers, int numChannels, int numSamples);

private:
    void run() override;

    double currentSampleRate = 44100.0;
    std::atomic<bool> recordingActive { false };
    std::atomic<RecordingSource> recordSource { RecordingSource::drumsOnly };
    std::atomic<int64_t> totalRecordedSamples { 0 };

    juce::File currentOutputFile;
    std::unique_ptr<juce::AudioFormatWriter> wavWriter;

    // Lock-free ring buffer
    static constexpr int fifoCapacity = 65536;
    juce::AbstractFifo fifo { fifoCapacity };
    juce::AudioBuffer<float> ringBuffer;

    juce::CriticalSection writerLock;
};

} // namespace glorp
