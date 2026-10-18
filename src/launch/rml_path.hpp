// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <cstdio>
#include <RmlUi/Core/FileInterface.h>

namespace bmh::rml {
// RmlUI parses document locations as URLs. Native Windows backslashes make
// URL::GetPath() empty, so relative image sources lose their asset directory.
inline std::string asset_path(const std::filesystem::path &path) {
    const auto utf8 = path.generic_u8string();
    return {utf8.begin(), utf8.end()};
}
// RmlUI paths are UTF-8. The Windows narrow fopen API uses the system code page.
class AssetFiles : public Rml::FileInterface {
  public:
    Rml::FileHandle Open(const Rml::String &path) override {
#ifdef _WIN32
        const std::u8string utf8(path.begin(), path.end());
        return reinterpret_cast<Rml::FileHandle>(_wfopen(std::filesystem::path(utf8).c_str(), L"rb"));
#else
        return reinterpret_cast<Rml::FileHandle>(std::fopen(path.c_str(), "rb"));
#endif
    }
    void Close(Rml::FileHandle file) override { std::fclose(reinterpret_cast<FILE *>(file)); }
    size_t Read(void *buffer, size_t size, Rml::FileHandle file) override {
        return std::fread(buffer, 1, size, reinterpret_cast<FILE *>(file));
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        return std::fseek(reinterpret_cast<FILE *>(file), offset, origin) == 0;
    }
    size_t Tell(Rml::FileHandle file) override { return size_t(std::ftell(reinterpret_cast<FILE *>(file))); }
};
} // namespace bmh::rml
