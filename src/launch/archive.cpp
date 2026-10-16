// SPDX-License-Identifier: GPL-3.0-only
#include "launch/archive.hpp"

#include <cstring>
#include <fstream>
#include <limits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

#include "miniz.h"

extern "C" {
#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
}

namespace bmh::launch {

namespace {

std::vector<uint8_t> read_whole(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw ArchiveError("cannot open " + path_to_utf8(file));
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) throw ArchiveError("cannot read " + path_to_utf8(file));
    // The largest real package (a box with three editions) has 55 MB; anything far beyond
    // that is no BMH package.
    if (uint64_t(size) > 512ull * 1024 * 1024) throw ArchiveError(path_to_utf8(file) + " is too large for a BMH package");
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()), std::streamsize(data.size()));
    if (!in) throw ArchiveError("cannot read " + path_to_utf8(file));
    return data;
}

// Code page 437 (the DOS code page old ZIP tools wrote names in) to UTF-8, for names
// without the ZIP UTF-8 flag. Only the upper half differs from ASCII.
std::string cp437_to_utf8(const std::string& text) {
    static const char16_t kHigh[128] = {
        0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
        0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
        0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
        0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
        0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};
    std::string out;
    for (unsigned char c : text) {
        const unsigned u = c < 0x80 ? c : kHigh[c - 0x80];
        if (u < 0x80) out += char(u);
        else if (u < 0x800) out += char(0xC0 | (u >> 6)), out += char(0x80 | (u & 0x3F));
        else out += char(0xE0 | (u >> 12)), out += char(0x80 | ((u >> 6) & 0x3F)), out += char(0x80 | (u & 0x3F));
    }
    return out;
}

std::string utf16_to_utf8(const std::vector<uint16_t>& text) {
    std::string out;
    for (size_t i = 0; i < text.size() && text[i] != 0; ++i) {
        uint32_t u = text[i];
        if (u >= 0xD800 && u < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000)
            u = 0x10000 + ((u - 0xD800) << 10) + (text[++i] - 0xDC00);
        if (u < 0x80) out += char(u);
        else if (u < 0x800) out += char(0xC0 | (u >> 6)), out += char(0x80 | (u & 0x3F));
        else if (u < 0x10000) out += char(0xE0 | (u >> 12)), out += char(0x80 | ((u >> 6) & 0x3F)), out += char(0x80 | (u & 0x3F));
        else
            out += char(0xF0 | (u >> 18)), out += char(0x80 | ((u >> 12) & 0x3F)), out += char(0x80 | ((u >> 6) & 0x3F)),
                out += char(0x80 | (u & 0x3F));
    }
    return out;
}

std::string normalise(std::string name) {
    for (char& c : name)
        if (c == '\\') c = '/';
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    return name;
}

// The LZMA SDK reads through an ISeekInStream; this one serves the archive from memory.
struct MemoryStream {
    ISeekInStream vt;  // first member: the SDK passes &vt around
    const uint8_t* data = nullptr;
    size_t size = 0, pos = 0;
};

SRes memory_read(ISeekInStreamPtr p, void* buf, size_t* size) {
    auto* s = reinterpret_cast<MemoryStream*>(const_cast<ISeekInStream*>(p));
    const size_t n = std::min(*size, s->size - s->pos);
    std::memcpy(buf, s->data + s->pos, n);
    s->pos += n;
    *size = n;
    return SZ_OK;
}

SRes memory_seek(ISeekInStreamPtr p, Int64* pos, ESzSeek origin) {
    auto* s = reinterpret_cast<MemoryStream*>(const_cast<ISeekInStream*>(p));
    Int64 base = origin == SZ_SEEK_SET ? 0 : origin == SZ_SEEK_CUR ? Int64(s->pos) : Int64(s->size);
    const Int64 target = base + *pos;
    if (target < 0 || uint64_t(target) > s->size) return SZ_ERROR_PARAM;
    s->pos = size_t(target);
    *pos = target;
    return SZ_OK;
}

}  // namespace

std::filesystem::path path_from_utf8(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string u = path.u8string();
    return std::string(u.begin(), u.end());
}

std::vector<std::string> utf8_arguments(int argc, char** argv) {
    std::vector<std::string> out;
#ifdef _WIN32
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 0; i < count; ++i) out.push_back(path_to_utf8(std::filesystem::path(wide[i])));
        LocalFree(wide);
        if (count == argc) return out;
        out.clear();
    }
#endif
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i]);
    return out;
}

ArchiveFormat archive_format(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    unsigned char head[8] = {};
    in.read(reinterpret_cast<char*>(head), sizeof head);
    if (in.gcount() < 4) return ArchiveFormat::none;
    if (head[0] == 'P' && head[1] == 'K' && (head[2] == 3 || head[2] == 5) && (head[3] == 4 || head[3] == 6))
        return ArchiveFormat::zip;
    if (in.gcount() >= 6 && std::memcmp(head, "7z\xBC\xAF\x27\x1C", 6) == 0) return ArchiveFormat::seven_zip;
    if (head[0] == 0x60 && head[1] == 0xEA) return ArchiveFormat::arj;
    if (std::memcmp(head, "Rar!", 4) == 0) return ArchiveFormat::rar;
    return ArchiveFormat::none;
}

struct Archive::Impl {
    ArchiveFormat format = ArchiveFormat::none;
    std::vector<uint8_t> bytes;
    std::vector<ArchiveEntry> entries;
    std::vector<uint32_t> source_index;  // entry -> index in the archive's own list
    // ZIP
    mz_zip_archive zip{};
    bool zip_open = false;
    // 7z
    MemoryStream stream;
    CLookToRead2 look{};
    CSzArEx db{};
    ISzAlloc alloc{SzAlloc, SzFree};
    bool db_open = false;
    UInt32 block_index = 0xFFFFFFFF;  // the solid block cached in out_buffer
    Byte* out_buffer = nullptr;
    size_t out_buffer_size = 0;

    ~Impl() {
        if (zip_open) mz_zip_reader_end(&zip);
        if (db_open) SzArEx_Free(&db, &alloc);
        if (out_buffer) ISzAlloc_Free(&alloc, out_buffer);
        if (look.buf) ISzAlloc_Free(&alloc, look.buf);
    }

    void open_zip() {
        if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0))
            throw ArchiveError(std::string("damaged ZIP archive: ") + mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        zip_open = true;
        const mz_uint count = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < count; ++i) {
            mz_zip_archive_file_stat st{};
            if (!mz_zip_reader_file_stat(&zip, i, &st)) throw ArchiveError("damaged ZIP directory");
            // bit 11 of the general purpose flags: the name is UTF-8
            const bool utf8 = (st.m_bit_flag & 0x800) != 0;
            std::string name = st.m_filename;
            entries.push_back({normalise(utf8 ? name : cp437_to_utf8(name)), st.m_uncomp_size, st.m_is_directory != 0});
            source_index.push_back(i);
        }
    }

    void open_7z() {
        static const bool crc_ready = [] { CrcGenerateTable(); return true; }();
        (void)crc_ready;
        stream.vt.Read = memory_read;
        stream.vt.Seek = memory_seek;
        stream.data = bytes.data();
        stream.size = bytes.size();
        LookToRead2_CreateVTable(&look, False);
        constexpr size_t kBuffer = size_t(1) << 18;
        look.buf = static_cast<Byte*>(ISzAlloc_Alloc(&alloc, kBuffer));
        if (!look.buf) throw ArchiveError("out of memory");
        look.bufSize = kBuffer;
        look.realStream = &stream.vt;
        LookToRead2_INIT(&look);
        SzArEx_Init(&db);
        if (SzArEx_Open(&db, &look.vt, &alloc, &alloc) != SZ_OK) throw ArchiveError("damaged 7z archive");
        db_open = true;
        for (UInt32 i = 0; i < db.NumFiles; ++i) {
            const size_t len = SzArEx_GetFileNameUtf16(&db, i, nullptr);
            std::vector<uint16_t> name(len);
            SzArEx_GetFileNameUtf16(&db, i, name.data());
            const bool dir = SzArEx_IsDir(&db, i) != 0;
            entries.push_back({normalise(utf16_to_utf8(name)), dir ? 0 : SzArEx_GetFileSize(&db, i), dir});
            source_index.push_back(i);
        }
    }
};

Archive::Archive(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Archive::Archive(Archive&&) noexcept = default;
Archive& Archive::operator=(Archive&&) noexcept = default;
Archive::~Archive() = default;

Archive Archive::open(const std::filesystem::path& file) {
    const ArchiveFormat format = archive_format(file);
    if (format != ArchiveFormat::zip && format != ArchiveFormat::seven_zip)
        throw ArchiveError(path_to_utf8(file) + " is no ZIP or 7z archive");
    auto impl = std::make_unique<Impl>();
    impl->format = format;
    impl->bytes = read_whole(file);
    if (format == ArchiveFormat::zip) impl->open_zip();
    else impl->open_7z();
    return Archive(std::move(impl));
}

ArchiveFormat Archive::format() const { return impl_->format; }
const std::vector<ArchiveEntry>& Archive::entries() const { return impl_->entries; }

std::vector<uint8_t> Archive::read(size_t index) {
    Impl& a = *impl_;
    if (index >= a.entries.size()) throw ArchiveError("no such archive entry");
    if (a.entries[index].directory) return {};
    if (a.format == ArchiveFormat::zip) {
        size_t size = 0;
        void* p = mz_zip_reader_extract_to_heap(&a.zip, a.source_index[index], &size, 0);
        if (!p) throw ArchiveError("cannot unpack " + a.entries[index].name);
        std::vector<uint8_t> out(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + size);
        mz_free(p);
        return out;
    }
    size_t offset = 0, processed = 0;
    if (SzArEx_Extract(&a.db, &a.look.vt, a.source_index[index], &a.block_index, &a.out_buffer, &a.out_buffer_size,
                       &offset, &processed, &a.alloc, &a.alloc) != SZ_OK)
        throw ArchiveError("cannot unpack " + a.entries[index].name);
    return std::vector<uint8_t>(a.out_buffer + offset, a.out_buffer + offset + processed);
}

}  // namespace bmh::launch
