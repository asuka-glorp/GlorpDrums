/*
    GlorpKitWriter.h - Pure C++ uncompressed PKZIP (ZIP_STORED) archive writer for .glorpkit packages.
    Writes uncompressed entries so SampleCache can memory-map audio slices directly with zero extraction.
*/

#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace glorp
{

class GlorpKitWriter
{
public:
    GlorpKitWriter() = default;
    ~GlorpKitWriter();

    /// Opens an output .glorpkit file for writing.
    bool open (const std::string& outputArchivePath);

    /// Adds an in-memory string (e.g. kit.xml, instrument.xml, zone.xml) at relativeArchivePath.
    bool addMemoryString (const std::string& relativeArchivePath, const std::string& content);

    /// Adds an in-memory binary buffer (e.g. PNG graphic or WAV bytes) at relativeArchivePath.
    bool addMemoryBytes (const std::string& relativeArchivePath, const void* data, size_t size);

    /// Streams a file from disk into the archive uncompressed (ZIP_STORED).
    bool addFileFromDisk (const std::string& relativeArchivePath, const std::string& diskFilePath);

    /// Streams a byte slice [offset, offset + length) from an existing archive file into the new archive.
    bool addSliceFromArchive (const std::string& relativeArchivePath,
                              const std::string& sourceArchivePath,
                              int64_t offset, int64_t length);

    /// Writes the Central Directory and EOCD (and ZIP64 EOCD if >= 4GB) and closes the file.
    bool finalize();

    static uint32_t computeCrc32 (const uint8_t* data, size_t length, uint32_t previousCrc = 0) noexcept;

private:
    struct WrittenEntry
    {
        std::string name;
        uint32_t crc32 = 0;
        uint64_t size = 0;
        uint64_t localHeaderOffset = 0;
    };

    std::ofstream outStream_;
    std::string outputPath_;
    bool isOpen_ = false;
    bool isFinalized_ = false;
    std::vector<WrittenEntry> entries_;

    bool writeLocalHeaderAndData (const std::string& normName, uint32_t crc, uint64_t size,
                                  std::ifstream* srcStream, int64_t srcOffset, const uint8_t* memData);
};

} // namespace glorp
