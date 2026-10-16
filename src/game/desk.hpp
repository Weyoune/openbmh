// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Entering a human manager's desk (DeskDate, the part before the desk screen).

#include "game/random.hpp"
#include "game/state.hpp"
#include <functional>

namespace bmh {

// DeskDate up to the screen: the desk buttons (HasNoEquivalentCondition), the random stir when a
// screen is entered (StirRandom), the table prediction with its season goal messages
// (Void5 mode 1), the autosave every Desk_EnterDesk_RuntimeField months and the manager's
// prestige (UpdatePrestige).
// Source save operation at the desk boundary. A successful writer owns exactly
// the normal four Save draws; failure before opening the file owns none.
using DeskAutosaveHook = std::function<void(const GameState&, WatcomRandom&)>;
void enter_desk(GameState& g, int manager, GameRandom& game, WatcomRandom& rand,
                const DeskAutosaveHook& autosave = {});

// The draws of saving a game (MakeSavegame): the savegame id (two WatcomRandom(0, 0x8fff))
// and two bytes WatcomRandom(0, 0xff). Everything else the save writes is not saved itself.
void save_game_draws(WatcomRandom& rand);

// UpdatePrestige: prestige of a human manager (record +0x211), a weighted sum of his two
// manager ratings (+0x8b2 25 %, +0x8aa 50 %), his money (15 %) and record +0x26f (10 %).
void update_prestige(GameState& g, int manager);

}  // namespace bmh
