// SPDX-License-Identifier: GPL-3.0-only
#include "formats/content.hpp"
#include <algorithm>
#include <cctype>
namespace bmh::content {
namespace {
std::mutex mutex;
struct Binding {
    std::shared_ptr<const Classic> content;
    int year;
};
std::map<std::filesystem::path, Binding> mounts;
auto normalized(const std::filesystem::path &p) { return std::filesystem::absolute(p).lexically_normal(); }
} // namespace
std::string key(const std::filesystem::path &p) {
    auto result = p.generic_string();
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return result;
}
Mount::Mount(std::filesystem::path root, std::shared_ptr<const Classic> value) : root_(normalized(root)) {
    if (!value)
        throw FormatError("content: empty mount");
    std::lock_guard lock(mutex);
    const auto year = value->start_year;
    if (!mounts.emplace(root_, Binding{std::move(value), year}).second)
        throw FormatError("content: already mounted");
}
Mount::~Mount() {
    std::lock_guard lock(mutex);
    mounts.erase(root_);
}
std::optional<Resource> find(const std::filesystem::path &p) {
    const auto full = normalized(p);
    std::lock_guard lock(mutex);
    for (const auto &[root, data] : mounts) {
        const auto relative = full.lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..")
            continue;
        return Resource{data.content, key(relative), data.year};
    }
    return std::nullopt;
}
void set_display_start_year(const std::filesystem::path &root, int year) {
    if (year < 1900 || year > 9998)
        throw FormatError("Invalid Classic display origin");
    std::lock_guard lock(mutex);
    const auto it = mounts.find(normalized(root));
    if (it == mounts.end())
        throw FormatError("Content is not mounted");
    it->second.year = year;
}
Bytes bytes(const Resource &r) {
    if (r.key == "m3.men")
        return r.content->desktops;
    if (r.key == "m3.dat")
        return r.content->formations;
    if (r.key == "textd.blk.header")
        return r.content->text_header;
    if (r.key.starts_with("s/")) {
        const auto it = r.content->audio.find(r.key.substr(2));
        if (it != r.content->audio.end())
            return it->second;
    }
    if (const auto it = r.content->supplemental.find(r.key); it != r.content->supplemental.end())
        return it->second;
    throw FormatError("content: no byte resource: " + r.key);
}
} // namespace bmh::content
