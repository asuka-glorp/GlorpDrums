#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>

namespace glorp
{

/**
 * Lock-free, real-time safe diagnostic logger for audio, MIDI, and kit events.
 *
 * Audio-thread calls enqueue fixed-size char buffers into an SPSC AbstractFifo
 * with zero heap allocations or mutexes. The message thread drains the queue via
 * flushToFile() and can write structured environment/kit/MIDI-map snapshots.
 */
class DebugLog
{
  public:
    static DebugLog &instance() noexcept;

    void setLogFile(const juce::File &file);
    juce::File getLogFile() const;

    /// Real-time safe (no locks, no heap allocations): logs a MIDI note/CC/learn event.
    void pushRealtimeEvent(const char *category, const char *detail) noexcept;

    /// Message/background-thread logging (thread-safe).
    void logInfo(const juce::String &category, const juce::String &message);

    /// Drains pending events from the lock-free ring buffer and appends to disk.
    void flushToFile();

    /// Replaces or appends a diagnostic snapshot block and flushes pending events.
    bool writeDiagnosticReport(const juce::String &headerReport);

    /// Returns true if new un-flushed entries are waiting.
    bool hasPendingEntries() const noexcept { return fifo.getNumReady() > 0; }

  private:
    DebugLog() = default;
    ~DebugLog() = default;

    DebugLog(const DebugLog &) = delete;
    DebugLog &operator=(const DebugLog &) = delete;

    static constexpr int kCapacity = 512;
    static constexpr int kMaxLineBytes = 192;
    static constexpr juce::int64 kMaxLogFileBytes = 512 * 1024;

    struct Entry
    {
        std::array<char, kMaxLineBytes> text {};
        juce::int64 timestampMs = 0;
    };

    juce::AbstractFifo fifo {kCapacity};
    std::array<Entry, kCapacity> ring {};
    std::atomic<bool> audioPushBusy {false};

    mutable juce::CriticalSection fileLock;
    juce::File logFile;
    juce::StringArray pendingNonRtLines;

    static juce::String formatTimestamp(juce::int64 msSinceEpoch);
};

} // namespace glorp
