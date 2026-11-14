#include "DebugLog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace glorp
{

DebugLog &DebugLog::instance() noexcept
{
    static DebugLog inst;
    return inst;
}

void DebugLog::setLogFile(const juce::File &file)
{
    const juce::ScopedLock sl(fileLock);
    logFile = file;
}

juce::File DebugLog::getLogFile() const
{
    const juce::ScopedLock sl(fileLock);
    if (logFile != juce::File {})
        return logFile;

    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("GlorpDrums")
        .getChildFile("glorpdrums_debug.log");
}

void DebugLog::pushRealtimeEvent(const char *category, const char *detail) noexcept
{
    // Guard against concurrent non-audio callers; audio thread stays lock-free.
    bool expected = false;
    if (!audioPushBusy.compare_exchange_strong(expected, true, std::memory_order_acquire))
        return;

    const auto scope = fifo.write(1);
    if (scope.blockSize1 > 0)
    {
        auto &entry = ring[static_cast<size_t>(scope.startIndex1)];
        entry.timestampMs = juce::Time::currentTimeMillis();
        std::snprintf(entry.text.data(),
                      entry.text.size(),
                      "[%s] %s",
                      category != nullptr ? category : "RT",
                      detail != nullptr ? detail : "");
    }

    audioPushBusy.store(false, std::memory_order_release);
}

void DebugLog::logInfo(const juce::String &category, const juce::String &message)
{
    const auto line = formatTimestamp(juce::Time::currentTimeMillis()) + " [" + category + "] " + message;

    const juce::ScopedLock sl(fileLock);
    pendingNonRtLines.add(line);
}

juce::String DebugLog::formatTimestamp(juce::int64 msSinceEpoch)
{
    return juce::Time(msSinceEpoch).formatted("%Y-%m-%d %H:%M:%S");
}

void DebugLog::flushToFile()
{
    juce::StringArray lines;

    // Drain SPSC FIFO from consumer side
    const int ready = fifo.getNumReady();
    if (ready > 0)
    {
        const auto scope = fifo.read(ready);
        for (int i = 0; i < scope.blockSize1; ++i)
        {
            const auto &entry = ring[static_cast<size_t>(scope.startIndex1 + i)];
            lines.add(formatTimestamp(entry.timestampMs) + " " + juce::String(entry.text.data()));
        }
        for (int i = 0; i < scope.blockSize2; ++i)
        {
            const auto &entry = ring[static_cast<size_t>(scope.startIndex2 + i)];
            lines.add(formatTimestamp(entry.timestampMs) + " " + juce::String(entry.text.data()));
        }
    }

    const juce::ScopedLock sl(fileLock);

    for (const auto &s : pendingNonRtLines)
        lines.add(s);
    pendingNonRtLines.clear();

    if (lines.isEmpty())
        return;

    const auto target = getLogFile();
    target.getParentDirectory().createDirectory();

    if (target.existsAsFile() && target.getSize() > kMaxLogFileBytes)
    {
        // Rotate old log to keep size bounded
        const auto backup = target.getSiblingFile(target.getFileName() + ".prev");
        target.moveFileTo(backup);
    }

    target.appendText(lines.joinIntoString("\n") + "\n");
}

bool DebugLog::writeDiagnosticReport(const juce::String &headerReport)
{
    flushToFile();

    const juce::ScopedLock sl(fileLock);
    const auto target = getLogFile();
    target.getParentDirectory().createDirectory();

    juce::String block;
    block << "\n==================== DIAGNOSTIC SNAPSHOT (" << formatTimestamp(juce::Time::currentTimeMillis())
          << ") ====================\n"
          << headerReport << "\n================================================================================\n";

    return target.appendText(block);
}

} // namespace glorp
