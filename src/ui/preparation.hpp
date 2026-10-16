// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <variant>

#include "ui/desk_state.hpp"
#include "ui/lineup.hpp"
#include "ui/training.hpp"
#include "ui/tactics.hpp"
#include "ui/pitch.hpp"
#include "ui/player_card.hpp"
#include "ui/training_camp.hpp"
#include "ui/bank.hpp"
#include "ui/stadium.hpp"
#include "ui/toto.hpp"
#include "ui/transfer_market.hpp"
#include "ui/outgoing_transfer.hpp"

namespace bmh::ui {

// One authoritative revision for all pre-match views. No engine execution.
struct PreparationState {
    DeskState desktop;
    LineupState lineup;
    std::optional<TrainingState> training; // individual training is unavailable in Modus I
    std::optional<TrainingCampState> training_camp;
    std::optional<BankState> bank;
    std::optional<StadiumState> stadium;
    std::optional<TotoState> toto;
    std::optional<TransferMarketState> transfer_market;
    TacticsState tactics;
    std::optional<PitchExtras> pitch;
    std::vector<PlayerCardState> player_cards;
};
using PreparationRequest = std::variant<std::monostate, LineupRequest, TrainingRequest, TrainingCampRequest, BankRequest, StadiumRequest, TotoRequest, TacticsRequest, PitchRequest, TransferMarketRequest, OutgoingTransferRequest>;
PreparationState preparation_from_game(const GameState&, const LeImage&, uint64_t revision);
PreparationState load_preparation_reference(const std::filesystem::path&, const LeImage&);
void validate_preparation_context(const PreparationState&);
std::string preparation_request_json(const PreparationRequest&);

} // namespace bmh::ui
