// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "import/classic.hpp"
#include "content/digest.hpp"
#include "formats/native_file.hpp"
namespace bmh::content {
using Documents = obw::Documents;
// Generic bounded ZIP documents; format-specific validation belongs to its reader.
Documents read_archive(const std::filesystem::path &file);
void write_archive(const Documents &, const std::filesystem::path &, bool overwrite = false);
// Sealed Classic contents: hashes cover every entry, including the original importer inventory.
std::string seal(Documents &);
std::string verify(const Documents &, ModType type = ModType::Classic);
// Missing descriptor means Classic. Presentation never changes the world identity.
ModType mod_type(const std::filesystem::path &);
void write_mod_type(const std::filesystem::path &, ModType);
std::shared_ptr<const Classic> open(const std::filesystem::path &);
std::shared_ptr<const Classic> open(const std::filesystem::path &, ModType);
} // namespace bmh::content
