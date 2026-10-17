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
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace glorp
{

//==============================================================================
/** Converts one sample of `bytesPerSample` bytes in the given format to float. */
inline float convertSample (const std::uint8_t* b, int bytesPerSample, int format) noexcept
{
    if (format == 1) // IEEE float
    {
        if (bytesPerSample == 4)
        {
            float v;
            std::memcpy (&v, b, 4);
            return v;
        }

        if (bytesPerSample == 8)
        {
            double v;
            std::memcpy (&v, b, 8);
            return static_cast<float> (v);
        }

        return 0.0f;
    }

    // Integer PCM. Always sign-extend from the container's top bit.
    switch (bytesPerSample)
    {
        case 1:
            // 8-bit wav is unsigned with a 128 offset.
            return (static_cast<float> (b[0]) - 128.0f) / 128.0f;

        case 2:
        {
            std::int16_t v;
            std::memcpy (&v, b, 2);
            return static_cast<float> (v) / 32768.0f;
        }

        case 3:
        {
            std::int32_t v = (static_cast<std::int32_t> (b[0]) << 8)
                           | (static_cast<std::int32_t> (b[1]) << 16)
                           | (static_cast<std::int32_t> (b[2]) << 24);
            return static_cast<float> (v >> 8) / 8388608.0f;
        }

        case 4:
        {
            std::int32_t v;
            std::memcpy (&v, b, 4);
            return static_cast<float> (v) / 2147483648.0f;
        }

        default:
            return 0.0f;
    }
}

//==============================================================================
class SampleCache
{
public:
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

        /// Single-sample fast inline read for realtime synthesis & resampling.
        /// Returns 0.0f when out of bounds or invalid.
        inline float readSample (int64_t frame, int channel) const noexcept
        {
            if (detail == nullptr || frame < 0 || frame >= detail->numFrames)
                return 0.0f;

            const auto* d = detail.get();
            if (channel < 0 || channel >= d->numChannels)
                return 0.0f;

            if (d->interleaved != nullptr)
                return d->interleaved[frame * d->numChannels + channel];

            const auto frameBytes = static_cast<std::size_t> (d->bytesPerSample) * static_cast<std::size_t> (d->numChannels);
            const auto offset = static_cast<std::size_t> (frame) * frameBytes
                              + static_cast<std::size_t> (channel) * static_cast<std::size_t> (d->bytesPerSample);

            return convertSample (d->raw + offset, d->bytesPerSample, d->format);
        }

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

} // namespace glorp
