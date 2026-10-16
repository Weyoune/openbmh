// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Read-only access to the archives a player may hand to OpenBMH instead of an installed
// game directory: ZIP (miniz) and 7z (the LZMA SDK's C decoder). The archive is read into
// memory once, so paths with characters outside the ANSI code page (the box names contain
// an en dash) work on Windows too. ARJ (the 1994 installation disk) and RAR are recognised
// only to give a clear message.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace bmh::launch {

class ArchiveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class ArchiveFormat { none, zip, seven_zip, arj, rar };

// The format by the first bytes of the file (none: not an archive we know).
ArchiveFormat archive_format(const std::filesystem::path& file);

struct ArchiveEntry {
    std::string name;    // UTF-8, '/' as separator, no leading '/'
    uint64_t size = 0;   // uncompressed size
    bool directory = false;
};

class Archive {
public:
    // Opens a ZIP or 7z archive; throws ArchiveError for anything else or a damaged file.
    static Archive open(const std::filesystem::path& file);
    Archive(Archive&&) noexcept;
    Archive& operator=(Archive&&) noexcept;
    ~Archive();

    ArchiveFormat format() const;
    const std::vector<ArchiveEntry>& entries() const;
    // The uncompressed bytes of entry `index`.
    std::vector<uint8_t> read(size_t index);

    struct Impl;

private:
    explicit Archive(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// A path from UTF-8 text (std::filesystem::path(std::string) would use the ANSI code page on
// Windows).
std::filesystem::path path_from_utf8(const std::string& text);
std::string path_to_utf8(const std::filesystem::path& path);

// The program arguments as UTF-8. On Windows `argv` is in the ANSI code page, which cannot
// hold every file name (an en dash, other alphabets); there the wide command line is used.
std::vector<std::string> utf8_arguments(int argc, char** argv);

}  // namespace bmh::launch
