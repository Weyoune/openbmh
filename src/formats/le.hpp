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

#include "formats/binary.hpp"

namespace bmh {

class LeImage {
public:
  int display_start_year() const { return display_start_year_; }
  void set_display_start_year(int year) { display_start_year_ = year; }
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
  const uint8_t *data(uint32_t address) const { return bytes_.data() + (address - base_); }
  // Overwrites bytes inside the image (the rule base, formats/rule_base.hpp).
  void patch(uint32_t address, const uint8_t *bytes, size_t count) {
      for (size_t i = 0; i < count; ++i)
          bytes_.at(address - base_ + i) = bytes[i];
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

    friend LeImage load_le(const std::filesystem::path& path, bool apply_supported_rules);

private:
  int display_start_year_ = 2026;
  uint32_t base_ = 0;
  Bytes bytes_;
};

// OpenBMH deviates from the original: applies a shared supported rule profile when loading editions with different rule-table values.
// Throws FormatError for files that are not LE executables or use external fixups.
// A German BMH.EXE is loaded with the OpenBMH rule base applied (formats/rule_base.hpp):
// every edition plays by the supported rule profile rule values.
// The installation importer passes false to preserve source tables separately;
// ordinary engine callers retain the default behavior.
LeImage load_le(const std::filesystem::path& path, bool apply_supported_rules = true);

// The OpenBMH image format: "OPENBMH-IMAGE" NUL, u32 base, u32 size, the bytes. load_le()
// reads it as well, so a translated image can stand in for BMH.exe in a data directory.
void save_image(const LeImage& image, const std::filesystem::path& path);

}  // namespace bmh
