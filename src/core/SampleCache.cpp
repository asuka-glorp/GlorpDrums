#include "SampleCache.h"

#include <cstring>
#include <fstream>

#if defined(__linux__) || defined(__unix__)
#include <sys/mman.h>
#endif

namespace glorp
{

//==============================================================================
void SampleCache::Handle::read (int64_t startFrame, int numFrames, int channel, float* dest) const noexcept
{
    std::fill (dest, dest + numFrames, 0.0f);

    if (detail == nullptr || ! valid)
        return;

    if (channel < 0 || channel >= detail->numChannels)
        return;

    // Clamp to the file, and let the caller hear silence for the remainder.
    if (startFrame < 0)
    {
        const auto skip = static_cast<int> (std::min<int64_t> (-startFrame, numFrames));
        startFrame = 0;
        dest += skip;
        numFrames -= skip;

        if (numFrames <= 0)
            return;
    }

    if (startFrame >= detail->numFrames)
        return;

    numFrames = static_cast<int> (std::min<int64_t> (numFrames, detail->numFrames - startFrame));

    if (numFrames <= 0)
        return;

    const auto d = detail.get();
    const auto frameBytes = static_cast<std::size_t> (d->bytesPerSample) * static_cast<std::size_t> (d->numChannels);
    const auto offset = static_cast<std::size_t> (startFrame) * frameBytes
                      + static_cast<std::size_t> (channel) * static_cast<std::size_t> (d->bytesPerSample);

    const auto* src = d->raw + offset;

    if (d->interleaved != nullptr)
    {
        // Native float: a strided read, no conversion.
        for (int i = 0; i < numFrames; ++i)
            dest[i] = d->interleaved[(startFrame + i) * d->numChannels + channel];

        return;
    }

    for (int i = 0; i < numFrames; ++i)
        dest[i] = convertSample (src + static_cast<std::size_t> (i) * frameBytes, d->bytesPerSample, d->format);
}

//==============================================================================
SampleCache::SampleCache()
{
    formatManager.registerBasicFormats();
}

SampleCache::~SampleCache() = default;

int SampleCache::mappedFileCount() const
{
    std::lock_guard<std::mutex> lock (mutex);
    return static_cast<int> (cache.size());
}

SampleCache::Handle SampleCache::open (const std::string& path)
{
    Handle handle;

    std::shared_ptr<const Detail> detail;

    {
        std::lock_guard<std::mutex> lock (mutex);

        auto it = cache.find (path);

        if (it != cache.end())
        {
            detail = it->second.detail;
        }
        else
        {
            std::string error;

            if (! parseWav (path, detail, error))
            {
                // Give up quietly rather than spamming: a missing file just means
                // that instrument stays silent.
                cache[path] = CacheEntry {};
                return handle;
            }

            cache[path] = CacheEntry { detail, detail };
        }
    }

    handle.detail = detail;
    handle.valid = detail != nullptr && detail->numFrames > 0;
    handle.numChannels = detail != nullptr ? detail->numChannels : 0;
    handle.numFrames = detail != nullptr ? detail->numFrames : 0;
    handle.sampleRate = detail != nullptr ? detail->sampleRate : 44100.0;

    return handle;
}

void SampleCache::trim()
{
    std::lock_guard<std::mutex> lock (mutex);

    for (auto it = cache.begin(); it != cache.end();)
    {
        if (it->second.detail == nullptr)
            it = cache.erase (it);           // never resolved
        else
            ++it;
    }
}

//==============================================================================
bool SampleCache::parseWav (const std::string& path, std::shared_ptr<const Detail>& out,
                            std::string& error) const
{
    auto mapping = std::make_shared<juce::MemoryMappedFile> (
        juce::File (path), juce::MemoryMappedFile::readOnly);

    if (mapping == nullptr || mapping->getData() == nullptr || mapping->getSize() == 0)
    {
        error = "could not map '" + path + "'";
        return false;
    }

    const auto* data = static_cast<const std::uint8_t*> (mapping->getData());
    const auto size = mapping->getSize();

    if (size < 44 || std::memcmp (data, "RIFF", 4) != 0 || std::memcmp (data + 8, "WAVE", 4) != 0)
    {
        error = "'" + path + "' is not a RIFF/WAVE file";
        return false;
    }

    // Advise kernel to preload pages and pre-touch to eliminate audio thread page faults
   #if defined(__linux__) || defined(__unix__)
    madvise (const_cast<void*> (mapping->getData()), static_cast<size_t> (size), MADV_WILLNEED);
   #endif

    volatile std::uint8_t touchSink = 0;
    for (std::size_t p = 0; p < static_cast<std::size_t> (size); p += 4096)
        touchSink += data[p];
    (void) touchSink;

    auto readU32 = [] (const std::uint8_t* p) -> std::uint32_t
    {
        return static_cast<std::uint32_t> (p[0])
             | (static_cast<std::uint32_t> (p[1]) << 8)
             | (static_cast<std::uint32_t> (p[2]) << 16)
             | (static_cast<std::uint32_t> (p[3]) << 24);
    };

    auto readU16 = [] (const std::uint8_t* p) -> std::uint16_t
    {
        return static_cast<std::uint16_t> (static_cast<std::uint16_t> (p[0])
                                         | (static_cast<std::uint16_t> (p[1]) << 8));
    };

    const std::uint8_t* fmt = nullptr;
    std::uint32_t fmtSize = 0;
    const std::uint8_t* samples = nullptr;
    std::uint32_t samplesSize = 0;

    std::size_t pos = 12;

    while (pos + 8 <= static_cast<std::size_t> (size))
    {
        const auto* chunk = data + pos;
        const auto chunkSize = readU32 (chunk + 4);
        const auto body = pos + 8;

        if (body + chunkSize > static_cast<std::size_t> (size))
            break;   // truncated final chunk

        if (std::memcmp (chunk, "fmt ", 4) == 0 && chunkSize >= 16)
        {
            fmt = data + body;
            fmtSize = chunkSize;
        }
        else if (std::memcmp (chunk, "data", 4) == 0)
        {
            samples = data + body;
            samplesSize = chunkSize;
        }

        // Chunks are word aligned.
        pos = body + chunkSize + (chunkSize & 1u);
    }

    if (fmt == nullptr || samples == nullptr)
    {
        error = "'" + path + "' has no fmt or data chunk";
        return false;
    }

    const auto audioFormat = readU16 (fmt);
    const auto numChannels = static_cast<int> (readU16 (fmt + 2));
    auto sampleRate = static_cast<double> (readU32 (fmt + 4));
    const auto bitsPerSample = static_cast<int> (readU16 (fmt + 14));

    // WAVE_FORMAT_EXTENSIBLE (0xFFFE) keeps the real tag in the GUID's first
    // two bytes; kits occasionally use it for 24-bit.
    int effectiveFormat = audioFormat;

    if (audioFormat == 0xFFFE && fmtSize >= 40)
        effectiveFormat = readU16 (fmt + 24);

    int containerFormat;

    switch (effectiveFormat)
    {
        case 1:  containerFormat = 0; break;   // PCM
        case 3:  containerFormat = 1; break;   // IEEE float
        default:
            error = "'" + path + "' uses unsupported wav format tag " + std::to_string (audioFormat);
            return false;
    }

    int bytesPerSample;

    switch (bitsPerSample)
    {
        case 8:  bytesPerSample = 1; break;
        case 16: bytesPerSample = 2; break;
        case 24: bytesPerSample = 3; break;
        case 32: bytesPerSample = 4; break;
        case 64:
            if (containerFormat != 1)
            {
                error = "'" + path + "' is 64-bit integer PCM, which nothing supports";
                return false;
            }
            bytesPerSample = 8;
            break;

        default:
            error = "'" + path + "' has " + std::to_string (bitsPerSample) + "-bit samples";
            return false;
    }

    if (numChannels <= 0 || numChannels > 64)
    {
        error = "'" + path + "' has an implausible channel count";
        return false;
    }

    if (sampleRate <= 0.0)
        sampleRate = 44100.0;

    const auto frameBytes = static_cast<std::size_t> (bytesPerSample) * static_cast<std::size_t> (numChannels);
    const auto numFrames = static_cast<int64_t> (samplesSize / frameBytes);

    if (numFrames <= 0)
    {
        error = "'" + path + "' contains no audio frames";
        return false;
    }

    auto detail = std::make_shared<Detail>();
    detail->mapping = std::move (mapping);
    detail->raw = data;
    detail->numChannels = numChannels;
    detail->numFrames = numFrames;
    detail->sampleRate = sampleRate;
    detail->bytesPerSample = bytesPerSample;
    detail->format = containerFormat;

    // Native float data can be handed out directly with no conversion at all,
    // which is the common case for 32-bit float kits.
    if (containerFormat == 1 && bytesPerSample == 4)
        detail->interleaved = reinterpret_cast<const float*> (samples);
    else
        detail->raw = samples;

    out = std::move (detail);
    return true;
}

} // namespace glorp
