// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Read-only queries of the Modern session for the new interface, one per menu
// (docs/new_engine/ui-abfragen.md). Each takes a JSON request with a `kind` and answers
// {ok: true, ...} or {ok: false, error}; none changes the game, draws random numbers or
// moves the revision. The game statistics (statistics.hpp) answer through the same entry.
//
//   {kind: "status"}                         the desk's status bar: club, manager, date, money
//   {kind: "leagues"}                        the leagues with size, matchdays and progress
//   {kind: "table", league}                  a league's current table
//   {kind: "fixtures", league, matchday?}    a matchday's pairings and results
//   {kind: "squad", club?}                   a squad with names, values and fitness

#include "cli/json.hpp"
#include "modern/game/modern_state.hpp"

namespace bmh::modern {

cli::Json query_projection(const ModernState& g, int manager, const cli::Json& request);

}  // namespace bmh::modern
