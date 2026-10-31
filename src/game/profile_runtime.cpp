// SPDX-License-Identifier: GPL-3.0-only
#include "game/profile_runtime.hpp"
#include "formats/data_profile.hpp"
namespace bmh {
void install_runtime_profile(const DataProfile& profile) {
    profile.validate();
    RuntimeDimensions next;
    next.league_teams=int(profile.counts.league_teams);
    next.cup_teams=int(profile.counts.cup_teams);
    next.big_squad_teams=int(profile.big_squad_teams);
    next.record_teams=int(profile.counts.n42);
    next.national_first=int(profile.national_first);
    next.pool_first=int(profile.pool_first());
    next.market_special_first=next.pool_first+40;
    next.free_first=next.pool_first+44;
    next.foreign_free_first=next.pool_first+124;
    next.national_slots_first=int(profile.national_slots_first());
    next.extra_slots_first=int(profile.extra_slots_first());
    next.total_slots=int(profile.total_slots());
    next.expanded=profile.extended();
    runtime_dimensions=next;
}
}
