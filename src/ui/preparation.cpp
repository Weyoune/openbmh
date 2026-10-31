// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/preparation.hpp"

#include <type_traits>
#include <algorithm>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"

namespace bmh::ui {

void validate_preparation_context(const PreparationState& s) {
    validate_desk_state(s.desktop);
    const bool national = s.desktop.national_team;
    const auto same = [&](const auto& view) {
        return view.manager_slot == s.desktop.manager_slot && view.runtime_club == s.desktop.runtime_club &&
               view.revision == s.desktop.revision && view.source == s.desktop.source &&
               view.club_name == s.desktop.club_name;
    };
    if (!same(s.lineup) || !same(s.tactics) || (s.training &&
        (!same(*s.training) || s.training->mode != s.desktop.options.mode)) ||
        bool(s.training) != (s.desktop.options.mode >= 2 && !national) ||
        s.lineup.automatic_formation != s.desktop.automation[0] ||
        s.tactics.assistant_attitude != s.desktop.automation[2] ||
        s.tactics.manager_name != s.desktop.manager_name ||
        (s.training && s.training->automation != s.desktop.automation[4]) ||
        (s.training_camp && (s.training_camp->manager_slot != s.desktop.manager_slot ||
            s.training_camp->runtime_club != s.desktop.runtime_club ||
            s.training_camp->revision != s.desktop.revision || s.training_camp->source != s.desktop.source ||
            s.training_camp->club_name != s.desktop.club_name)) ||
        (s.bank && (s.bank->manager_slot != s.desktop.manager_slot || s.bank->runtime_club != s.desktop.runtime_club ||
            s.bank->revision != s.desktop.revision || s.bank->source != s.desktop.source ||
            s.bank->manager_name != s.desktop.manager_name || s.bank->club_name != s.desktop.club_name)) ||
        (s.stadium && (s.stadium->manager_slot != s.desktop.manager_slot || s.stadium->runtime_club != s.desktop.runtime_club ||
            s.stadium->revision != s.desktop.revision || s.stadium->source != s.desktop.source ||
            s.stadium->club_name != s.desktop.club_name)) ||
        (s.toto && (s.toto->manager_slot != s.desktop.manager_slot || s.toto->runtime_club != s.desktop.runtime_club ||
            s.toto->revision != s.desktop.revision || s.toto->source != s.desktop.source ||
            s.toto->club_name != s.desktop.club_name)) ||
        (s.transfer_market && (s.transfer_market->manager_slot != s.desktop.manager_slot ||
            s.transfer_market->runtime_club != s.desktop.runtime_club ||
            s.transfer_market->revision != s.desktop.revision || s.transfer_market->source != s.desktop.source ||
            s.transfer_market->manager_name != s.desktop.manager_name ||
            s.transfer_market->club_name != s.desktop.club_name)))
        throw FormatError("preparation: inconsistent manager/club/source/revision across views");
    if (national && (s.training || s.training_camp || s.bank || s.stadium || s.pitch))
        throw FormatError("preparation: national coach has no supported club-only training/finance/pitch context");
    if (s.pitch && (s.pitch->manager_slot != s.desktop.manager_slot ||
        s.pitch->runtime_club != s.desktop.runtime_club || s.pitch->revision != s.desktop.revision ||
        s.pitch->source != s.desktop.source || s.pitch->mode != s.desktop.options.mode ||
        s.pitch->badge != s.desktop.badge))
        throw FormatError("preparation: inconsistent pitch context");
    std::vector<int> card_rows;
    for (const auto& card : s.player_cards) {
        if (!same(card.lineup) || card.badge != s.desktop.badge || card.money_denominator <= 0 ||
            card.money_numerator != s.desktop.money_numerator || card.money_denominator != s.desktop.money_denominator ||
            card.row < 0 || size_t(card.row) >= s.lineup.players.size() ||
            card.lineup.players.size() != s.lineup.players.size() ||
            card.lineup.players.at(card.row).player_id != s.lineup.players.at(card.row).player_id ||
            card.lineup.players.at(card.row).slot != s.lineup.players.at(card.row).slot ||
            std::find(card_rows.begin(), card_rows.end(), card.row) != card_rows.end())
            throw FormatError("preparation: inconsistent player card context");
        card_rows.push_back(card.row);
    }
}

PreparationState preparation_from_game(const GameState& game, const LeImage& exe, uint64_t revision) {
    PreparationState result;
    result.desktop = desk_from_game(game, exe, revision);
    const bool national = result.desktop.national_team;
    if (!national) {
        result.training_camp = training_camp_from_game(game, exe, result.desktop.manager_slot, revision);
        result.bank = bank_from_game(game, exe, revision);
        result.stadium = stadium_from_game(game, exe, revision);
        result.stadium->source = result.desktop.source;
        result.stadium->club_name = result.desktop.club_name;
        result.toto = toto_from_game(game, revision);
        result.toto->source = result.desktop.source;
        result.toto->club_name = result.desktop.club_name;
        result.transfer_market = transfer_market_from_game(game, revision);
        result.transfer_market->source = result.desktop.source;
    }
    result.lineup = lineup_from_game(game, exe, revision);
    if (result.desktop.options.mode >= 2 && !national) result.training = training_from_game(game, exe, revision);
    result.tactics = tactics_from_game(game, exe, revision);
    if (!national) result.pitch = pitch_extras_from_game(game, exe, revision);
    for (const auto& player : result.lineup.players) if (player.player_id >= 0)
        result.player_cards.push_back(player_card_from_game(game, exe, player.row, revision));
    validate_preparation_context(result);
    return result;
}

PreparationState load_preparation_reference(const std::filesystem::path& path, const LeImage& exe) {
    const auto fbm = load_savegame(path);
    if (!fbm.checksum_ok || fbm.version != (exe.expanded_profile() ? "O1.1" : "V2.24")) throw FormatError("preparation: unsupported/damaged FBM");
    const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4), uint32_t(exe.u16(layout::CareerSession_DispatchLineup_RuntimeField)), 30};
    const SaveState save(fbm.stream, counts);
    PreparationState result;
    result.desktop = desk_from_save(save, exe);
    GameState game = GameState::from_save(save, counts);
    const bool national = result.desktop.national_team;
    if (!national) {
        result.training_camp = training_camp_from_game(game, exe, result.desktop.manager_slot, result.desktop.revision);
        result.training_camp->source = result.desktop.source;
        result.bank = bank_from_game(game, exe, result.desktop.revision);
        result.bank->source = result.desktop.source;
        result.stadium = stadium_from_game(game, exe, result.desktop.revision);
        result.stadium->source = result.desktop.source;
        result.stadium->club_name = result.desktop.club_name;
        result.toto = toto_from_game(game, result.desktop.revision);
        result.toto->source = result.desktop.source;
        result.toto->club_name = result.desktop.club_name;
        result.transfer_market = transfer_market_from_game(game, result.desktop.revision);
        result.transfer_market->source = result.desktop.source;
    }
    result.lineup = lineup_from_save(save, exe);
    if (result.desktop.options.mode >= 2 && !national) result.training = training_from_save(save, exe);
    result.tactics = tactics_from_save(save, exe);
    if (!national) result.pitch = pitch_extras_from_save(save, exe);
    for (const auto& player : result.lineup.players) if (player.player_id >= 0)
        result.player_cards.push_back(player_card_from_save(save, exe, player.row));
    validate_preparation_context(result);
    return result;
}

std::string preparation_request_json(const PreparationRequest& request) {
    return std::visit([](const auto& action) -> std::string {
        using T = std::decay_t<decltype(action)>;
        if constexpr (std::is_same_v<T, LineupRequest>) return lineup_request_json(action);
        else if constexpr (std::is_same_v<T, TrainingRequest>) return training_request_json(action);
        else if constexpr (std::is_same_v<T, TrainingCampRequest>) return training_camp_request_json(action);
        else if constexpr (std::is_same_v<T, BankRequest>) return bank_request_json(action);
        else if constexpr (std::is_same_v<T, StadiumRequest>) return stadium_request_json(action);
        else if constexpr (std::is_same_v<T, TotoRequest>) return toto_request_json(action);
        else if constexpr (std::is_same_v<T, TacticsRequest>) return tactics_request_json(action);
        else if constexpr (std::is_same_v<T, PitchRequest>) return pitch_request_json(action);
        else if constexpr (std::is_same_v<T, TransferMarketRequest>) return transfer_market_request_json(action);
        else if constexpr (std::is_same_v<T, OutgoingTransferRequest>) return outgoing_transfer_request_json(action);
        else return "null";
    }, request);
}

} // namespace bmh::ui
