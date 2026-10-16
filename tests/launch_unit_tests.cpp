// SPDX-License-Identifier: GPL-3.0-only
#include "launch/archive.hpp"
#include "miniz.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace bmh::launch;
int main() try {
    const std::string unicode = "Unit-Ä-测试 folder/archive.zip";
    if (path_to_utf8(path_from_utf8(unicode)) != unicode)
        throw std::runtime_error("UTF-8 filesystem path roundtrip failed");
    const auto path = std::filesystem::temp_directory_path() /
        path_from_utf8("openbmh-unit-Ä-测试-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".zip");
    // A synthetic one-file ZIP, not a game or copied asset.
    const std::string payload = "Authored archive unit-test payload";
    mz_zip_archive writer{};
    if (!mz_zip_writer_init_heap(&writer, 0, 0) ||
        !mz_zip_writer_add_mem(&writer, "unit.txt", payload.data(), payload.size(), 0))
        throw std::runtime_error("Synthetic ZIP writer failed");
    void* bytes = nullptr; size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&writer, &bytes, &size))
        throw std::runtime_error("Synthetic ZIP finalization failed");
    mz_zip_writer_end(&writer);
    {
        std::ofstream output(path, std::ios::binary);
        output.write(static_cast<const char*>(bytes), std::streamsize(size));
        if (!output) throw std::runtime_error("Synthetic ZIP write failed");
    }
    mz_free(bytes);
    if (archive_format(path) != ArchiveFormat::zip) throw std::runtime_error("ZIP recognition failed");
    auto archive = Archive::open(path);
    if (archive.entries().size() != 1 || archive.entries()[0].name != "unit.txt")
        throw std::runtime_error("ZIP entry metadata mismatch");
    const auto read = archive.read(0);
    if (std::string(read.begin(), read.end()) != payload) throw std::runtime_error("ZIP payload mismatch");
    std::filesystem::remove(path);
    std::cout << "Synthetic ZIP and Unicode path checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
