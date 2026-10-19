/*
    MidiRecorder.h - Sample-accurate Standard MIDI File (SMF Type 1) recorder for GlorpDrums.
    Captures note-ons, note-offs, velocities, polyphonic aftertouch (chokes), and continuous CC#4 pedal motion.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace glorp
{

class MidiRecorder
{
public:
    MidiRecorder();
    ~MidiRecorder() = default;

    /// Prepares buffers and starts recording with the given sample rate and tempo.
    void startRecording (double sampleRate, double bpm, int timeSigNum = 4, int timeSigDenom = 4);

    /// Stops recording and finalizes the sequence.
    void stopRecording();

    bool isRecording() const noexcept { return recordingActive.load (std::memory_order_relaxed); }

    /// Called from processBlock(). Real-time thread-safe event logger. ZERO ALLOCATION.
    void logMessage (const juce::MidiMessage& message, int64_t currentGlobalSample);

    /// Exports recorded take to a standard Type 1 MIDI file (.mid).
    bool exportToFile (const juce::File& destinationFile) const;

    int getEventCount() const noexcept;
    juce::File getLastExportedFile() const { return lastExportedFile; }

    static constexpr int defaultPpq = 960;

private:
    struct RawMidiEvent
    {
        int64_t sampleOffset = 0;
        uint8_t data[4] = { 0 };
        uint8_t length = 0;
    };

    double recordingSampleRate = 44100.0;
    double recordingBpm = 120.0;
    int timeNumerator = 4;
    int timeDenominator = 4;

    std::atomic<bool> recordingActive { false };
    std::atomic<int64_t> startSampleTime { 0 };
    std::atomic<size_t> eventCount { 0 };

    static constexpr size_t maxEvents = 65536;
    std::vector<RawMidiEvent> eventBuffer;
    mutable std::mutex bufferMutex;

    juce::File lastExportedFile;
};

} // namespace glorp
