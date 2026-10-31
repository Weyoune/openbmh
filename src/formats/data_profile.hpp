// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <filesystem>
#include <string>
#include "formats/m3d.hpp"
namespace bmh {
// Installation metadata; original files retain their implicit Classic profile.
struct DataProfile {
    std::string id = "classic";
    std::array<uint8_t,9> leagues{18,18,18,18,18,12,12,12,12};
    DbCounts counts{};
    uint32_t big_squad_teams = 90;
    uint32_t national_first = 357;
    uint32_t pool_first() const { return big_squad_teams*24+(counts.league_teams-big_squad_teams)*16; }
    uint32_t national_slots_first() const { return pool_first()+144; }
    uint32_t extra_slots_first() const { return national_slots_first()+24; }
    uint32_t total_slots() const { return extra_slots_first()+54; }
    bool extended() const { return id != "classic"; }
    void validate() const;
    static DataProfile international_11();
};
DataProfile load_data_profile(const std::filesystem::path& directory);
}
