// SPDX-License-Identifier: GPL-3.0-only
#include "formats/native_file.hpp"
#include "formats/binary.hpp"
#include <chrono>
#include <fstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace bmh::content {
namespace fs = std::filesystem;
namespace {
[[noreturn]] void fail(const std::string &error) { throw FormatError(error); }
} // namespace
void write_atomic(const fs::path &file, std::string_view data, bool overwrite) {
    const auto parent = file.has_parent_path() ? file.parent_path() : fs::path(".");
    if (!fs::is_directory(parent))
        fail("destination directory unavailable");
    const auto status = fs::symlink_status(file);
    if (fs::is_symlink(status) || fs::is_directory(status) || (!overwrite && fs::exists(status)))
        fail("destination already exists or is unsafe");
    fs::path staging;
    for (int i = 0; i < 100; ++i) {
        auto candidate =
            parent /
            (".openbmh-write-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(i));
        if (fs::create_directory(candidate)) {
            staging = std::move(candidate);
            break;
        }
    }
    if (staging.empty())
        fail("cannot reserve staging directory");
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    } cleanup{staging};
    const auto temp = staging / "payload";
    std::ofstream stream(temp, std::ios::binary);
    stream.write(data.data(), std::streamsize(data.size()));
    stream.close();
    if (!stream)
        fail("cannot write temporary file");
    const auto current = fs::symlink_status(file);
    if (fs::is_symlink(current) || fs::is_directory(current) || (!overwrite && fs::exists(current)))
        fail("destination changed while writing");
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), file.c_str(),
                     MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
        fail("cannot publish file");
#else
    if (overwrite)
        fs::rename(temp, file);
    else {
        fs::create_hard_link(temp, file);
        fs::remove(temp);
    }
#endif
}
} // namespace bmh::content
