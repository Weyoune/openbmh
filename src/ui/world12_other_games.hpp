// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ui/other_games_state.hpp"
#include "game/world12/save.hpp"

namespace bmh::ui {
enum class World12OtherPage:uint8_t {
    german_cup,uefa_cup,champions_league,cup_winners_cup,national_qualifiers,supercups,fa_cup,league_cup
};
struct World12OtherSelection {
    World12OtherPage page=World12OtherPage::german_cup;
    int stage=-1,round=-1,participant_offset=0;
    OtherGamesDisplayMode mode=OtherGamesDisplayMode::detail;
};
World12OtherSelection default_world12_other_selection(const bmh::world12::World&);
OtherGamesState world12_other_games_state(const bmh::world12::CareerSnapshot&,
    const World12OtherGamesIdentity&,World12OtherSelection);
// Read-only navigation. Team-info/close are returned to the typed window owner;
// this helper never writes the world or interprets local renderer tokens as IDs.
World12OtherSelection navigate_world12_other_games(const bmh::world12::CareerSnapshot&,
    const World12OtherGamesIdentity&,World12OtherSelection,const OtherGamesRequest&);
}
