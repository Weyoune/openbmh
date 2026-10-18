// SPDX-License-Identifier: GPL-3.0-only
#include "content/package.hpp"
#include "import/detail.hpp"
#include <chrono>
#include <fstream>
namespace bmh::content {
namespace fs = std::filesystem;
using namespace obw::detail;
namespace {
constexpr const char *inventory = "x-classic/seal.json";
O hashes(const Documents &docs) {
    O result;
    for (const auto &[name, value] : docs)
        if (name != inventory) {
            if (!safe_path(name))
                fail("invalid content entry");
            result[name] = sha256(value);
        }
    return result;
}
std::string identity(const Documents &docs) {
    auto entries = hashes(docs);
    entries.erase("x-classic/inventory.json");
    auto manifest = parse(docs, "manifest.json");
    manifest.object()->erase("generator");
    manifest.object()->erase("title");
    entries["manifest.json"] = sha256(cli::dump_json(manifest));
    return sha256(cli::dump_json(entries));
}
} // namespace
Documents read_archive(const fs::path &file) {
    if (fs::is_symlink(fs::symlink_status(file)) || !fs::is_regular_file(file) ||
        fs::file_size(file) > 128ull * 1024 * 1024)
        fail("invalid native archive");
    const auto input = read_file(file);
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, input.data(), input.size(), 0))
        fail("invalid ZIP archive");
    struct End {
        mz_zip_archive *zip;
        ~End() { mz_zip_reader_end(zip); }
    } end{&zip};
    Documents out;
    uint64_t total = 0;
    const auto count = mz_zip_reader_get_num_files(&zip);
    if (count > 8192)
        fail("too many archive entries");
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            fail("cannot read entry metadata");
        if (stat.m_is_directory)
            continue;
        const std::string name = stat.m_filename;
        if (!safe_path(name) || stat.m_uncomp_size > 32ull * 1024 * 1024 ||
            (total += stat.m_uncomp_size) > 128ull * 1024 * 1024)
            fail("unsafe or oversized archive entry");
        std::string value(size_t(stat.m_uncomp_size), '\0');
        if (!mz_zip_reader_extract_to_mem(&zip, i, value.data(), value.size(), 0))
            fail("damaged ZIP entry");
        if (!out.emplace(name, std::move(value)).second)
            fail("duplicate archive entry");
    }
    return out;
}
void write_archive(const Documents &out, const fs::path &file, bool overwrite) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_heap(&zip, 0, 0))
        fail("cannot initialize ZIP writer");
    struct End {
        mz_zip_archive *zip;
        ~End() { mz_zip_writer_end(zip); }
    } end{&zip};
    uint64_t total = 0;
    if (out.size() > 8192)
        fail("too many archive entries");
    for (const auto &[name, data] : out) {
        if (!safe_path(name) || data.size() > 32ull * 1024 * 1024 ||
            (total += data.size()) > 128ull * 1024 * 1024)
            fail("invalid archive size or path");
        if (!mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_BEST_SPEED))
            fail("cannot add ZIP entry");
    }
    void *buffer = nullptr;
    size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size))
        fail("cannot finalize ZIP archive");
    struct Free {
        void *data;
        ~Free() { mz_free(data); }
    } free{buffer};
    write_atomic(file, std::string_view(static_cast<const char *>(buffer), size), overwrite);
}
std::string seal(Documents &docs) {
    obw::validate(docs);
    auto entries = hashes(docs);
    const auto id = identity(docs);
    put(docs, inventory,
        O{{"schema", "classic-seal@1.0"}, {"content_id", id}, {"entries", std::move(entries)}});
    return id;
}
std::string verify(const Documents &docs) {
    const auto stored = parse(docs, inventory);
    if (str(get(stored, "schema")) != "classic-seal@1.0")
        fail("unsupported Classic seal");
    const auto actual = hashes(docs);
    const auto encoded = cli::dump_json(actual);
    if (cli::dump_json(get(stored, "entries")) != encoded || str(get(stored, "content_id")) != identity(docs))
        fail("Classic contents were changed; import the installation again");
    return identity(docs);
}
std::shared_ptr<const Classic> open(const fs::path &path) {
    auto docs = read_archive(path);
    const auto id = verify(docs);
    auto value = std::make_shared<Classic>(obw::load_classic(docs));
    value->content_id = id;
    const auto manifest = parse(docs, "manifest.json");
    value->start_year = std::stoi(str(get(manifest, "start_date")).substr(0, 4));
    value->executable.set_display_start_year(value->start_year);
    value->documents = std::move(docs);
    return value;
}
} // namespace bmh::content
