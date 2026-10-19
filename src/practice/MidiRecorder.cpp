/*
    MidiRecorder.cpp - Sample-accurate Standard MIDI File (SMF Type 1) recorder for GlorpDrums.
*/

#include "MidiRecorder.h"

#include <algorithm>

namespace glorp
{

//==============================================================================
MidiRecorder::MidiRecorder()
{
    eventBuffer.resize (maxEvents);
}

void MidiRecorder::startRecording (double sampleRate, double bpm, int timeSigNum, int timeSigDenom)
{
    recordingSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    recordingBpm = bpm > 0.0 ? bpm : 120.0;
    timeNumerator = std::max (1, timeSigNum);
    timeDenominator = std::max (1, timeSigDenom);

    eventCount.store (0, std::memory_order_relaxed);
    startSampleTime.store (-1, std::memory_order_relaxed);
    recordingActive.store (true, std::memory_order_release);
}

void MidiRecorder::stopRecording()
{
    recordingActive.store (false, std::memory_order_release);
}

int MidiRecorder::getEventCount() const noexcept
{
    return static_cast<int> (std::min (eventCount.load (std::memory_order_relaxed), maxEvents));
}

//==============================================================================
// Real-time audio callback - ZERO ALLOCATION
//==============================================================================
void MidiRecorder::logMessage (const juce::MidiMessage& message, int64_t currentGlobalSample)
{
    if (! recordingActive.load (std::memory_order_relaxed))
        return;

    int64_t expected = -1;
    startSampleTime.compare_exchange_strong (expected, currentGlobalSample);

    const int64_t base = startSampleTime.load (std::memory_order_relaxed);
    const int64_t relSample = std::max<int64_t> (0, currentGlobalSample - base);

    const size_t rawSize = static_cast<size_t> (message.getRawDataSize());
    if (rawSize == 0 || rawSize > 4)
        return;

    const size_t idx = eventCount.fetch_add (1, std::memory_order_relaxed);
    if (idx < maxEvents)
    {
        auto& ev = eventBuffer[idx];
        ev.sampleOffset = relSample;
        ev.length = static_cast<uint8_t> (rawSize);
        const auto* bytes = message.getRawData();
        for (size_t i = 0; i < rawSize; ++i)
            ev.data[i] = bytes[i];
    }
}

//==============================================================================
// Export to SMF Type 1
//==============================================================================
bool MidiRecorder::exportToFile (const juce::File& destinationFile) const
{
    if (destinationFile.exists())
        destinationFile.deleteFile();

    auto outStream = destinationFile.createOutputStream();
    if (outStream == nullptr)
        return false;

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (defaultPpq);

    const double ticksPerSecond = (recordingBpm / 60.0) * static_cast<double> (defaultPpq);
    const double ticksPerSample = (recordingSampleRate > 0.0) ? (ticksPerSecond / recordingSampleRate) : 1.0;

    // Track 0: Conductor track (Tempo & Time Signature)
    juce::MidiMessageSequence track0;
    const int microsPerQuarter = static_cast<int> (60000000.0 / std::max (10.0, recordingBpm));
    track0.addEvent (juce::MidiMessage::tempoMetaEvent (microsPerQuarter), 0.0);
    track0.addEvent (juce::MidiMessage::timeSignatureMetaEvent (timeNumerator, timeDenominator), 0.0);
    track0.addEvent (juce::MidiMessage::endOfTrack(), 0.0);
    midiFile.addTrack (track0);

    // Track 1: Performance Track (Drum notes, chokes, continuous CC#4 pedal)
    juce::MidiMessageSequence track1;
    track1.addEvent (juce::MidiMessage::textMetaEvent (3, "GlorpDrums Take"), 0.0);

    const size_t count = std::min (eventCount.load (std::memory_order_relaxed), maxEvents);
    for (size_t i = 0; i < count; ++i)
    {
        const auto& ev = eventBuffer[i];
        if (ev.length > 0 && ev.length <= 4)
        {
            juce::MidiMessage msg (ev.data, ev.length);
            const double tick = static_cast<double> (ev.sampleOffset) * ticksPerSample;
            track1.addEvent (msg, tick);
        }
    }

    track1.updateMatchedPairs();
    track1.addEvent (juce::MidiMessage::endOfTrack(), track1.getEndTime());
    midiFile.addTrack (track1);

    const bool success = midiFile.writeTo (*outStream);
    return success;
}

} // namespace glorp
