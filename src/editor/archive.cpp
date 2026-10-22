// SPDX-License-Identifier: GPL-3.0-only
#include "editor/archive.hpp"
#include "content/digest.hpp"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <fstream>
#include <miniz.h>
#include <mutex>
#include <random>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace bmh::editor {
namespace {
constexpr uint64_t max_entry = 64ull * 1024 * 1024;
constexpr uint64_t max_total = 2ull * 1024 * 1024 * 1024;
constexpr size_t max_entries = 100000;
void publish(const fs::path &temporary, const fs::path &destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw Error("save_failed", {{"path", utf8(destination)}, {"system", GetLastError()}});
#else
    std::error_code ec;
    fs::rename(temporary, destination, ec);
    if (ec)
        throw Error("save_failed", {{"path", utf8(destination)}, {"system", ec.message()}});
#endif
}
struct Remove {
    fs::path target;
    ~Remove() {
        if (!target.empty()) {
            std::error_code ec;
            fs::remove_all(target, ec);
        }
    }
};
} // namespace
std::string utf8(const fs::path &value) {
    const auto data = value.generic_u8string();
    return {data.begin(), data.end()};
}
fs::path path(std::string_view value) { return fs::path(std::u8string(value.begin(), value.end())); }
bool safe_name(std::string_view value) {
    if (value.empty() || value.front() == '/' || value.find('\\') != value.npos || value.find(':') != value.npos ||
        value.find('\0') != value.npos || value.back() == '/')
        return false;
    const auto relative = path(value);
    for (const auto &part : relative)
        if (part == "." || part == ".." || part.empty())
            return false;
    return !relative.is_absolute();
}
std::string new_id(const std::string &prefix) {
    static std::mutex lock;
    static std::mt19937_64 random(std::random_device{}());
    std::lock_guard guard(lock);
    static const char *digits = "0123456789abcdef";
    std::string result = prefix;
    for (int n = 0; n < 2; ++n) {
        auto number = random();
        for (int i = 0; i < 16; ++i) {
            result += digits[number & 15];
            number >>= 4;
        }
    }
    return result;
}
std::string folded(std::string value) {
    // UTF-8 byte equality is retained; the small UI search additionally handles
    // ASCII case without changing authored spelling or source identity.
    for (char &c : value)
        if (c >= 'A' && c <= 'Z')
            c = char(c + ('a' - 'A'));
    return value;
}
std::string read_file(const fs::path &file, uint64_t maximum) {
    std::ifstream source(file, std::ios::binary);
    if (!source)
        throw Error("open_failed", {{"path", utf8(file)}});
    source.seekg(0, std::ios::end);
    auto size = source.tellg();
    if (size < 0 || uint64_t(size) > maximum)
        throw Error("size_limit", {{"path", utf8(file)}});
    source.seekg(0);
    std::string bytes(size_t(size), '\0');
    source.read(bytes.data(), std::streamsize(bytes.size()));
    if (!source && !bytes.empty())
        throw Error("open_failed", {{"path", utf8(file)}});
    return bytes;
}
void write_atomic(const fs::path &file, std::string_view data, bool overwrite) {
    if (!overwrite && fs::exists(file))
        throw Error("destination_exists");
    if (fs::is_symlink(fs::symlink_status(file)))
        throw Error("unsafe_path");
    const auto parent = file.has_parent_path() ? file.parent_path() : fs::path(".");
    if (!fs::is_directory(parent))
        throw Error("output_parent_missing");
    auto temporary = parent / new_id(".openbmh-editor-");
    Remove cleanup{temporary};
    {
        std::ofstream output(temporary, std::ios::binary);
        output.write(data.data(), std::streamsize(data.size()));
        output.close();
        if (!output)
            throw Error("save_failed");
    }
    publish(temporary, file);
}

struct ArchiveStore::Impl {
    fs::path cache;
    fs::path root;
    mutable std::ifstream input;
    mutable mz_zip_archive zip{};
    struct Item {
        uint64_t size;
        mz_uint index;
        fs::path file;
    };
    std::map<std::string, Item> entries;
    std::map<std::string, std::shared_ptr<const std::string>> changed;
    mutable std::mutex mutex;
    bool zip_open = false;
    static size_t read_at(void *opaque, mz_uint64 offset, void *data, size_t count) {
        auto &in = static_cast<Impl *>(opaque)->input;
        in.clear();
        in.seekg(std::streamoff(offset));
        in.read(static_cast<char *>(data), std::streamsize(count));
        return size_t(in.gcount());
    }
    ~Impl() {
        if (zip_open)
            mz_zip_reader_end(&zip);
        input.close();
        if (!cache.empty()) {
            std::error_code ec;
            fs::remove_all(cache, ec);
        }
    }
};
ArchiveStore::ArchiveStore() : impl_(std::make_unique<Impl>()) {}
ArchiveStore::~ArchiveStore() = default;
std::shared_ptr<ArchiveStore> ArchiveStore::open(const fs::path &source) {
    auto result = std::make_shared<ArchiveStore>();
    auto &s = *result->impl_;
    uint64_t total = 0;
    auto add = [&](std::string name, Impl::Item item) {
        if (!safe_name(name) || item.size > max_entry || total + item.size > max_total ||
            s.entries.size() >= max_entries)
            throw Error("archive_limit", {{"entry", name}});
        if (!s.entries.emplace(name, std::move(item)).second)
            throw Error("duplicate_entry", {{"entry", name}});
        total += item.size;
    };
    if (fs::is_symlink(fs::symlink_status(source)))
        throw Error("unsafe_path");
    if (fs::is_directory(source)) {
        const auto original = fs::canonical(source);
        s.cache = fs::temp_directory_path() / new_id("openbmh-editor-");
        s.root = s.cache / "world";
        fs::create_directories(s.root);
        for (const auto &entry : fs::recursive_directory_iterator(original)) {
            if (entry.is_symlink())
                throw Error("unsafe_path");
            if (entry.is_regular_file()) {
                const auto relative = entry.path().lexically_relative(original);
                const auto snapshot = s.root / relative;
                add(utf8(relative), {entry.file_size(), 0, snapshot});
                fs::create_directories(snapshot.parent_path());
                fs::copy_file(entry.path(), snapshot);
            }
        }
    } else {
        if (!fs::is_regular_file(source) || fs::file_size(source) > max_total)
            throw Error("archive_limit");
        s.cache = fs::temp_directory_path() / new_id("openbmh-editor-");
        fs::create_directory(s.cache);
        const auto snapshot = s.cache / "source.obw";
        fs::copy_file(source, snapshot);
        s.input.open(snapshot, std::ios::binary);
        s.zip.m_pRead = Impl::read_at;
        s.zip.m_pIO_opaque = &s;
        if (!mz_zip_reader_init(&s.zip, fs::file_size(snapshot), 0))
            throw Error("invalid_archive");
        s.zip_open = true;
        auto count = mz_zip_reader_get_num_files(&s.zip);
        if (count > max_entries)
            throw Error("archive_limit");
        for (mz_uint index = 0; index < count; ++index) {
            mz_zip_archive_file_stat stat{};
            if (!mz_zip_reader_file_stat(&s.zip, index, &stat))
                throw Error("invalid_archive");
            if (stat.m_is_directory)
                continue;
            if (((stat.m_external_attr >> 16) & 0170000) == 0120000)
                throw Error("unsafe_path");
            if (stat.m_is_encrypted || !stat.m_is_supported)
                throw Error("unsupported_archive");
            add(stat.m_filename, {stat.m_uncomp_size, index, {}});
        }
    }
    if (!s.entries.contains("manifest.json"))
        throw Error("missing_manifest");
    return result;
}
bool ArchiveStore::contains(const std::string &name) const {
    return impl_->changed.contains(name) || impl_->entries.contains(name);
}
std::vector<std::string> ArchiveStore::names() const {
    std::set<std::string> all;
    for (const auto &[key, _] : impl_->entries)
        all.insert(key);
    for (const auto &[key, _] : impl_->changed)
        all.insert(key);
    return {all.begin(), all.end()};
}
uint64_t ArchiveStore::size(const std::string &name) const {
    if (auto i = impl_->changed.find(name); i != impl_->changed.end())
        return i->second->size();
    if (auto i = impl_->entries.find(name); i != impl_->entries.end())
        return i->second.size;
    throw Error("missing_asset", {{"path", name}});
}
std::string ArchiveStore::read(const std::string &name) const {
    if (auto i = impl_->changed.find(name); i != impl_->changed.end())
        return *i->second;
    const auto entry = impl_->entries.find(name);
    if (entry == impl_->entries.end())
        throw Error("missing_asset", {{"path", name}});
    if (!entry->second.file.empty()) {
        if (fs::is_symlink(fs::symlink_status(entry->second.file)))
            throw Error("unsafe_path");
        return read_file(entry->second.file, max_entry);
    }
    std::lock_guard guard(impl_->mutex);
    std::string bytes(size_t(entry->second.size), '\0');
    if (!mz_zip_reader_extract_to_mem(&impl_->zip, entry->second.index, bytes.data(), bytes.size(), 0))
        throw Error("invalid_archive", {{"entry", name}});
    return bytes;
}
void ArchiveStore::set(const std::string &name, std::shared_ptr<const std::string> bytes) {
    if (!safe_name(name) || !bytes || bytes->size() > max_entry)
        throw Error("unsafe_asset");
    impl_->changed[name] = std::move(bytes);
}
std::shared_ptr<const std::string> ArchiveStore::overlay(const std::string &name) const {
    auto i = impl_->changed.find(name);
    return i == impl_->changed.end() ? nullptr : i->second;
}
void ArchiveStore::restore(const std::string &name, std::shared_ptr<const std::string> value) {
    if (value)
        set(name, std::move(value));
    else
        impl_->changed.erase(name);
}
void ArchiveStore::save(const fs::path &target, const std::map<std::string, std::string> &docs,
                        const std::set<std::string> &excluded) const {
    if (target.extension() != ".obw")
        throw Error("obw_only");
    if (fs::is_symlink(fs::symlink_status(target)))
        throw Error("unsafe_path");
    auto parent = target.has_parent_path() ? target.parent_path() : fs::path(".");
    if (!fs::is_directory(parent))
        throw Error("output_parent_missing");
    auto temporary = parent / new_id(".openbmh-editor-");
    Remove cleanup{temporary};
    std::ofstream output(temporary, std::ios::binary);
    if (!output)
        throw Error("save_failed");
    mz_zip_archive writer{};
    writer.m_pIO_opaque = &output;
    writer.m_pWrite = [](void *opaque, mz_uint64 position, const void *data, size_t count) -> size_t {
        auto &stream = *static_cast<std::ofstream *>(opaque);
        stream.seekp(std::streamoff(position));
        stream.write(static_cast<const char *>(data), std::streamsize(count));
        return stream ? count : 0;
    };
    if (!mz_zip_writer_init_v2(&writer, 0, MZ_ZIP_FLAG_WRITE_ZIP64))
        throw Error("save_failed");
    struct Close {
        mz_zip_archive *p;
        ~Close() { mz_zip_writer_end(p); }
    } closer{&writer};
    std::set<std::string> all;
    for (auto &name : names())
        all.insert(name);
    for (auto &[name, _] : docs)
        all.insert(name);
    if (all.size() > max_entries)
        throw Error("archive_limit");
    uint64_t total = 0;
    for (const auto &name : all) {
        if (excluded.contains(name))
            continue;
        if (!safe_name(name))
            throw Error("unsafe_path");
        auto replacement = docs.find(name);
        const auto bytes_size = replacement != docs.end() ? replacement->second.size() : size(name);
        if (bytes_size > max_entry || (total += bytes_size) > max_total)
            throw Error("archive_limit");
        auto existing = impl_->entries.find(name);
        if (replacement == docs.end() && !impl_->changed.contains(name) && impl_->zip_open &&
            existing != impl_->entries.end()) {
            std::lock_guard guard(impl_->mutex);
            if (!mz_zip_writer_add_from_zip_reader(&writer, &impl_->zip, existing->second.index))
                throw Error("save_failed");
        } else {
            auto bytes = replacement != docs.end() ? replacement->second : read(name);
            const auto level = name.ends_with(".json") ? MZ_BEST_SPEED : MZ_NO_COMPRESSION;
            if (!mz_zip_writer_add_mem(&writer, name.c_str(), bytes.data(), bytes.size(), level))
                throw Error("save_failed");
        }
    }
    if (!mz_zip_writer_finalize_archive(&writer))
        throw Error("save_failed");
    output.close();
    if (!output)
        throw Error("save_failed");
    publish(temporary, target);
}
} // namespace bmh::editor
