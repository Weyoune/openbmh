// SPDX-License-Identifier: GPL-3.0-only
#pragma once
namespace bmh {
struct DataProfile;
// Explicitly distinct concepts: German clubs, league clubs and large rosters.
// One installation is active per process, like the existing fixture/rule tables.
struct RuntimeDimensions {
    int domestic_teams=90, league_teams=138, cup_teams=30, big_squad_teams=90;
    int record_teams=168;
    int national_first=357, pool_first=2928, market_special_first=2968;
    int free_first=2972, foreign_free_first=3052, national_slots_first=3072;
    int extra_slots_first=3096, total_slots=3150;
    bool expanded=false;
};
inline RuntimeDimensions runtime_dimensions{};
void install_runtime_profile(const DataProfile& profile);
}
