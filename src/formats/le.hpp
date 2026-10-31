// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// The original program itself (BMH.exe, a DOS/4GW "LE" linear executable) as a data file.
//
// Many tables and all message texts of the game live in the data object of BMH.exe. Like
// the other original files it is read at runtime, never shipped: load_le() loads every
// object at its link address and applies the internal fixups, so addresses and stored
// pointers are the "LE addresses" used throughout the port (e.g. MatchdaySetup_Attendance_ExecutableField), exactly as in
// the disassembly (runtime verification produces the same image for Ghidra).

#include <cstdint>
#include <filesystem>
#include <string>
#include <map>
#include <optional>

#include "formats/binary.hpp"

namespace bmh {

// Resolved edition policies, not copied machine instructions.
struct RuntimePolicy {
    uint32_t year=0, weekday=0, badge_end=0, environment_verified=0;
    std::map<uint32_t,std::string> labels;
};
class LeImage {
public:
    uint32_t base() const { return base_; }
    uint32_t size() const { return uint32_t(bytes_.size()); }
    bool contains(uint32_t address) const { return address >= base_ && address - base_ < bytes_.size(); }
    uint8_t u8(uint32_t address) const { return bytes_.at(address - base_); }
    uint16_t u16(uint32_t address) const { return uint16_t(u8(address) | u8(address + 1) << 8); }
    uint32_t u32(uint32_t address) const { return uint32_t(u16(address)) | uint32_t(u16(address + 2)) << 16; }
    // A NUL terminated string at `address` (ISO 646-DE bytes, see textd.hpp).
    std::string cstring(uint32_t address) const;
    // The string a pointer variable at `address` points to (e.g. Weekly_AdvertisingOffers_TextPointer).
    std::string string_at_pointer(uint32_t address) const { return cstring(u32(address)); }
    const uint8_t* data(uint32_t address) const { return bytes_.data() + (address - base_); }
    // Overwrites bytes inside a prepared image for verified address translation.
    void patch(uint32_t address, const uint8_t* bytes, size_t count) {
        for (size_t i = 0; i < count; ++i) bytes_.at(address - base_ + i) = bytes[i];
    }

    // An image built by OpenBMH itself (the English FLTD.EXE translated into the German
    // address space, launch/fltd.hpp).
    static LeImage from_bytes(uint32_t base, Bytes bytes) {
        LeImage img;
        img.base_ = base;
        img.bytes_ = std::move(bytes);
        return img;
    }
    const Bytes& bytes() const { return bytes_; }
    bool expanded_profile() const { return expanded_profile_; }
    const RuntimePolicy* runtime_policy() const { return runtime_policy_ ? &*runtime_policy_ : nullptr; }

    friend LeImage load_le(const std::filesystem::path& path);

private:
    std::optional<RuntimePolicy> runtime_policy_;
    bool expanded_profile_ = false;
    uint32_t base_ = 0;
    Bytes bytes_;
};

// Loads the original executable image without changing edition-specific rule tables.
// Throws FormatError for files that are not LE executables or use external fixups.
LeImage load_le(const std::filesystem::path& path);

// The OpenBMH image format: "OPENBMH-IMAGE" NUL, u32 base, u32 size, the bytes. load_le()
// reads it as well, so a translated image can stand in for BMH.exe in a data directory.
void save_image(const LeImage& image, const std::filesystem::path& path);
// Exports only the data segment and resolved policy values, never DOS/LE/code objects.
void save_runtime_image(const LeImage&, const RuntimePolicy&, const std::filesystem::path&);

}  // namespace bmh
