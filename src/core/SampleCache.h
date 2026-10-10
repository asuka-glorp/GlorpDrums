/*
    SampleCache.h - memory-mapped access to the wav files inside a drumkit.

    DrumGizmo kits are large. A decent kit is several GB of multi-channel 24-bit
    wav, so decoding samples into RAM is not an option and neither is handing the
    audio thread a lock to wait on while it reads.

    So the wav files are mapped and read in place. The mapping costs nothing but
    address space; the pages are faulted in from the page cache the first time
    each sample is played and cost nothing thereafter. Reading a sample is then
    just an index and a multiply, with no allocation, no lock and no syscall on
    the audio thread.

    The RIFF parser here covers what DrumGizmo kits actually contain - PCM and
    IEEE float, 8/16/24/32 bit - and falls back to JUCE's decoder for anything
    more exotic.
*/

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace glorp
{

class SampleCache
{
private:
    struct Detail;

public:
    SampleCache();
    ~SampleCache();

    /** A handle to one channel of one wav file. Safe to read from the audio
        thread once `isValid()`. Holds a reference to the cache's storage, so the
        underlying memory cannot be freed while a voice is using it.
    */
    struct Handle
    {
        bool valid = false;
        int numChannels = 0;
        int64_t numFrames = 0;
        double sampleRate = 44100.0;

        /// Reads `numFrames` frames of `channel` starting at `startFrame` into
        /// `dest`. Frames past the end of the file are filled with silence.
        void read (int64_t startFrame, int numFrames, int channel, float* dest) const noexcept;

    private:
        friend class SampleCache;
        std::shared_ptr<const Detail> detail;
    };

    /** Opens a wav file, or returns an invalid handle. Blocking; call off the
        audio thread. Safe to call repeatedly for the same path.
    */
    Handle open (const std::string& path);

    /// Drops mapped files that nothing holds a handle to.
    void trim();

    /// Number of distinct files currently mapped. Used by tests and diagnostics.
    int mappedFileCount() const;

private:
    struct Detail
    {
        std::shared_ptr<juce::MemoryMappedFile> mapping;
        const float* interleaved = nullptr;   ///< null when the mapping needs conversion
        const std::uint8_t* raw = nullptr;

        int numChannels = 0;
        int64_t numFrames = 0;
        double sampleRate = 44100.0;

        int bytesPerSample = 0;   ///< container width, e.g. 3 for 24-bit
        int format = 0;           ///< 0 = integer PCM, 1 = IEEE float
    };

    struct CacheEntry
    {
        std::shared_ptr<const Detail> detail;
        std::weak_ptr<const Detail> lastReturned;   ///< for trim()
    };

    bool parseWav (const std::string& path, std::shared_ptr<const Detail>& out, std::string& error) const;

    mutable std::mutex mutex;
    std::map<std::string, CacheEntry> cache;
    juce::AudioFormatManager formatManager;
};

/** Converts one sample of `bytesPerSample` bytes in the given format to float.
    Exposed for testing.
*/
float convertSample (const std::uint8_t* bytes, int bytesPerSample, int format) noexcept;

} // namespace glorp
