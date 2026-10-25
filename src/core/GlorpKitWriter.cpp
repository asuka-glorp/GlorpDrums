/*
    GlorpKitWriter.cpp - Pure C++ uncompressed PKZIP (ZIP_STORED) archive writer for .glorpkit packages.
*/

#include "GlorpKitWriter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace glorp
{

namespace
{
    void writeU16LE (std::ofstream& out, uint16_t v)
    {
        const uint8_t b[2] = {
            static_cast<uint8_t> (v & 0xFF),
            static_cast<uint8_t> ((v >> 8) & 0xFF)
        };
        out.write (reinterpret_cast<const char*> (b), 2);
    }

    void writeU32LE (std::ofstream& out, uint32_t v)
    {
        const uint8_t b[4] = {
            static_cast<uint8_t> (v & 0xFF),
            static_cast<uint8_t> ((v >> 8) & 0xFF),
            static_cast<uint8_t> ((v >> 16) & 0xFF),
            static_cast<uint8_t> ((v >> 24) & 0xFF)
        };
        out.write (reinterpret_cast<const char*> (b), 4);
    }

    void writeU64LE (std::ofstream& out, uint64_t v)
    {
        const uint8_t b[8] = {
            static_cast<uint8_t> (v & 0xFF),
            static_cast<uint8_t> ((v >> 8) & 0xFF),
            static_cast<uint8_t> ((v >> 16) & 0xFF),
            static_cast<uint8_t> ((v >> 24) & 0xFF),
            static_cast<uint8_t> ((v >> 32) & 0xFF),
            static_cast<uint8_t> ((v >> 40) & 0xFF),
            static_cast<uint8_t> ((v >> 48) & 0xFF),
            static_cast<uint8_t> ((v >> 56) & 0xFF)
        };
        out.write (reinterpret_cast<const char*> (b), 8);
    }

    std::string normalizeArcPath (std::string p)
    {
        std::replace (p.begin(), p.end(), '\\', '/');
        while (p.rfind ("./", 0) == 0)
            p = p.substr (2);
        while (! p.empty() && p.front() == '/')
            p = p.substr (1);
        return p;
    }

    const std::array<uint32_t, 256>& crcTable() noexcept
    {
        static const auto table = []()
        {
            std::array<uint32_t, 256> t {};
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[i] = c;
            }
            return t;
        }();
        return table;
    }
} // namespace

//==============================================================================
GlorpKitWriter::~GlorpKitWriter()
{
    if (isOpen_ && ! isFinalized_)
        finalize();
}

uint32_t GlorpKitWriter::computeCrc32 (const uint8_t* data, size_t length, uint32_t previousCrc) noexcept
{
    const auto& tbl = crcTable();
    uint32_t c = previousCrc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
        c = tbl[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool GlorpKitWriter::open (const std::string& outputArchivePath)
{
    entries_.clear();
    isFinalized_ = false;
    outputPath_ = outputArchivePath;

    const fs::path p (outputArchivePath);
    if (p.has_parent_path())
    {
        std::error_code ec;
        fs::create_directories (p.parent_path(), ec);
    }

    outStream_.open (outputArchivePath, std::ios::binary | std::ios::trunc);
    isOpen_ = outStream_.is_open();
    return isOpen_;
}

bool GlorpKitWriter::addMemoryString (const std::string& relativeArchivePath, const std::string& content)
{
    return addMemoryBytes (relativeArchivePath, content.data(), content.size());
}

bool GlorpKitWriter::addMemoryBytes (const std::string& relativeArchivePath, const void* data, size_t size)
{
    if (! isOpen_ || isFinalized_)
        return false;

    const auto normName = normalizeArcPath (relativeArchivePath);
    if (normName.empty())
        return false;

    const auto* bytes = static_cast<const uint8_t*> (data);
    const uint32_t crc = (bytes != nullptr && size > 0) ? computeCrc32 (bytes, size) : 0u;
    return writeLocalHeaderAndData (normName, crc, static_cast<uint64_t> (size), nullptr, 0, bytes);
}

bool GlorpKitWriter::addFileFromDisk (const std::string& relativeArchivePath, const std::string& diskFilePath)
{
    if (! isOpen_ || isFinalized_)
        return false;

    std::ifstream in (diskFilePath, std::ios::binary | std::ios::ate);
    if (! in.is_open())
        return false;

    const auto fileSize = static_cast<int64_t> (in.tellg());
    if (fileSize < 0)
        return false;

    return addSliceFromArchive (relativeArchivePath, diskFilePath, 0, fileSize);
}

bool GlorpKitWriter::addSliceFromArchive (const std::string& relativeArchivePath,
                                          const std::string& sourceArchivePath,
                                          int64_t offset, int64_t length)
{
    if (! isOpen_ || isFinalized_ || offset < 0 || length < 0)
        return false;

    const auto normName = normalizeArcPath (relativeArchivePath);
    if (normName.empty())
        return false;

    std::ifstream in (sourceArchivePath, std::ios::binary);
    if (! in.is_open())
        return false;

    in.seekg (offset);
    if (! in.good())
        return false;

    // Validate WAV header integrity if writing a .wav entry: must begin with "RIFF"
    if (normName.size() >= 4 && (normName.compare (normName.size() - 4, 4, ".wav") == 0 ||
                                 normName.compare (normName.size() - 4, 4, ".WAV") == 0))
    {
        char riffHeader[4] = {};
        in.read (riffHeader, 4);
        if (in.gcount() < 4 || std::memcmp (riffHeader, "RIFF", 4) != 0)
            return false;
        in.seekg (offset);
    }

    // Pass 1: compute CRC32 over the slice
    uint32_t crc = 0;
    constexpr size_t kChunkSize = 65536;
    std::vector<uint8_t> buf (kChunkSize);
    int64_t remaining = length;

    while (remaining > 0)
    {
        const size_t toRead = static_cast<size_t> (std::min<int64_t> (remaining, static_cast<int64_t> (kChunkSize)));
        in.read (reinterpret_cast<char*> (buf.data()), static_cast<std::streamsize> (toRead));
        const auto got = static_cast<size_t> (in.gcount());
        if (got == 0)
            return false;
        crc = computeCrc32 (buf.data(), got, crc);
        remaining -= static_cast<int64_t> (got);
    }

    return writeLocalHeaderAndData (normName, crc, static_cast<uint64_t> (length), &in, offset, nullptr);
}

bool GlorpKitWriter::writeLocalHeaderAndData (const std::string& normName, uint32_t crc, uint64_t size,
                                              std::ifstream* srcStream, int64_t srcOffset, const uint8_t* memData)
{
    const uint64_t localHeaderOffset = static_cast<uint64_t> (outStream_.tellp());
    const uint16_t nameLen = static_cast<uint16_t> (normName.size());
    const uint32_t size32 = (size >= 0xFFFFFFFFull) ? 0xFFFFFFFFu : static_cast<uint32_t> (size);

    // Local file header signature (PK\x03\x04)
    outStream_.write ("PK\x03\x04", 4);
    writeU16LE (outStream_, 20);      // Version needed to extract (2.0)
    writeU16LE (outStream_, 0);       // General purpose bit flag
    writeU16LE (outStream_, 0);       // Compression method: 0 = ZIP_STORED (uncompressed!)
    writeU16LE (outStream_, 0);       // Last mod file time
    writeU16LE (outStream_, 0);       // Last mod file date
    writeU32LE (outStream_, crc);     // CRC-32
    writeU32LE (outStream_, size32);  // Compressed size
    writeU32LE (outStream_, size32);  // Uncompressed size
    writeU16LE (outStream_, nameLen); // File name length
    writeU16LE (outStream_, 0);       // Extra field length
    outStream_.write (normName.data(), nameLen);

    if (memData != nullptr && size > 0)
    {
        outStream_.write (reinterpret_cast<const char*> (memData), static_cast<std::streamsize> (size));
    }
    else if (srcStream != nullptr && size > 0)
    {
        srcStream->clear();
        srcStream->seekg (srcOffset);
        constexpr size_t kChunkSize = 65536;
        std::vector<char> buf (kChunkSize);
        uint64_t rem = size;
        while (rem > 0)
        {
            const size_t toRead = static_cast<size_t> (std::min<uint64_t> (rem, kChunkSize));
            srcStream->read (buf.data(), static_cast<std::streamsize> (toRead));
            const auto got = static_cast<size_t> (srcStream->gcount());
            if (got == 0)
                return false;
            outStream_.write (buf.data(), static_cast<std::streamsize> (got));
            rem -= got;
        }
    }

    if (! outStream_.good())
        return false;

    WrittenEntry we;
    we.name = normName;
    we.crc32 = crc;
    we.size = size;
    we.localHeaderOffset = localHeaderOffset;
    entries_.push_back (std::move (we));
    return true;
}

bool GlorpKitWriter::finalize()
{
    if (! isOpen_ || isFinalized_)
        return false;

    const uint64_t centralDirOffset = static_cast<uint64_t> (outStream_.tellp());
    bool needsZip64 = (entries_.size() >= 0xFFFFu) || (centralDirOffset >= 0xFFFFFFFFull);

    for (const auto& e : entries_)
    {
        const bool entryZip64 = (e.size >= 0xFFFFFFFFull) || (e.localHeaderOffset >= 0xFFFFFFFFull);
        if (entryZip64)
            needsZip64 = true;

        const uint16_t nameLen = static_cast<uint16_t> (e.name.size());
        const uint16_t extraLen = entryZip64 ? 28 : 0;

        outStream_.write ("PK\x01\x02", 4);
        writeU16LE (outStream_, entryZip64 ? 45 : 20); // Version made by
        writeU16LE (outStream_, entryZip64 ? 45 : 20); // Version needed
        writeU16LE (outStream_, 0);                    // Flags
        writeU16LE (outStream_, 0);                    // Method 0 = ZIP_STORED
        writeU16LE (outStream_, 0);                    // Time
        writeU16LE (outStream_, 0);                    // Date
        writeU32LE (outStream_, e.crc32);
        writeU32LE (outStream_, entryZip64 ? 0xFFFFFFFFu : static_cast<uint32_t> (e.size));
        writeU32LE (outStream_, entryZip64 ? 0xFFFFFFFFu : static_cast<uint32_t> (e.size));
        writeU16LE (outStream_, nameLen);
        writeU16LE (outStream_, extraLen);
        writeU16LE (outStream_, 0); // Comment length
        writeU16LE (outStream_, 0); // Disk number start
        writeU16LE (outStream_, 0); // Internal attrs
        writeU32LE (outStream_, 0); // External attrs
        writeU32LE (outStream_, entryZip64 ? 0xFFFFFFFFu : static_cast<uint32_t> (e.localHeaderOffset));
        outStream_.write (e.name.data(), nameLen);

        if (entryZip64)
        {
            writeU16LE (outStream_, 0x0001); // ZIP64 extra field tag
            writeU16LE (outStream_, 24);     // 3 * 8 bytes
            writeU64LE (outStream_, e.size);
            writeU64LE (outStream_, e.size);
            writeU64LE (outStream_, e.localHeaderOffset);
        }
    }

    const uint64_t centralDirEnd = static_cast<uint64_t> (outStream_.tellp());
    const uint64_t centralDirSize = centralDirEnd - centralDirOffset;
    if (centralDirSize >= 0xFFFFFFFFull)
        needsZip64 = true;

    if (needsZip64)
    {
        const uint64_t zip64EocdOffset = centralDirEnd;
        // ZIP64 End of Central Directory Record (PK\x06\x06)
        outStream_.write ("PK\x06\x06", 4);
        writeU64LE (outStream_, 44); // Size of remaining record
        writeU16LE (outStream_, 45); // Version made by
        writeU16LE (outStream_, 45); // Version needed
        writeU32LE (outStream_, 0);  // Disk number
        writeU32LE (outStream_, 0);  // Disk with central dir
        writeU64LE (outStream_, entries_.size());
        writeU64LE (outStream_, entries_.size());
        writeU64LE (outStream_, centralDirSize);
        writeU64LE (outStream_, centralDirOffset);

        // ZIP64 End of Central Directory Locator (PK\x06\x07)
        outStream_.write ("PK\x06\x07", 4);
        writeU32LE (outStream_, 0);
        writeU64LE (outStream_, zip64EocdOffset);
        writeU32LE (outStream_, 1);
    }

    // Standard End of Central Directory Record (PK\x05\x06)
    outStream_.write ("PK\x05\x06", 4);
    writeU16LE (outStream_, 0);
    writeU16LE (outStream_, 0);
    const uint16_t count16 = (entries_.size() >= 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t> (entries_.size());
    writeU16LE (outStream_, count16);
    writeU16LE (outStream_, count16);
    writeU32LE (outStream_, (centralDirSize >= 0xFFFFFFFFull) ? 0xFFFFFFFFu : static_cast<uint32_t> (centralDirSize));
    writeU32LE (outStream_, (centralDirOffset >= 0xFFFFFFFFull) ? 0xFFFFFFFFu : static_cast<uint32_t> (centralDirOffset));
    writeU16LE (outStream_, 0);

    outStream_.flush();
    const bool ok = outStream_.good();
    outStream_.close();
    isFinalized_ = true;
    isOpen_ = false;
    return ok;
}

} // namespace glorp
