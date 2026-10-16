// SPDX-License-Identifier: GPL-3.0-only
// The squad windows of docs/CLI_CONCEPT/06-verein.md 1: Lineup (menu 3) with Lineup.Pitch,
// the player card PlayerCard (+ PlayerCard.Statistics) and the club information TeamInfo.
//
// Opening the line-up is an engine step: the original arranges the eleven through the human
// path when the screen opens (PitchScreen -> Int2, CareerSession::dispatch_lineup
// prepare_lineup); the console does the same when Open(Lineup) runs, never on a read command.

#include <algorithm>

#include "app/window_actions.hpp"
#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "ui/club_info.hpp"
#include "ui/lineup.hpp"
#include "game/lineup.hpp"
#include "ui/pitch.hpp"
#include "ui/player_card.hpp"

namespace bmh::cli::console {

using namespace detail;

namespace {

// TEXTD indices (docs/formats/textd_inventory.csv)
constexpr int kPositionNames = 629;  // positionGoalkeeperCaps "TOR", ABW, MIT, STU
constexpr int kSkillNames = 135;     // skillTeamplay, Speed, Shot, Pass, Tackling, FreeKick, Corner
constexpr int kRatings = 142;        // ratingLousy, Fair, Good, Great

// The squad slot flags: 0x8000 on the pitch (starting eleven), 0x4000 on the bench.
std::string slot_state(uint16_t slot) { return (slot & 0x8000) ? "pitch" : (slot & 0x4000) ? "bench" : "-"; }

// Why a player cannot play: w364 bits 16-19 hold the remaining duration, bit 20 marks a ban
// instead of an injury (Absence). The SDL line-up shows the same as a red cross / card.
std::string absence(const Player& a) {
    const int duration = int(a.w364 >> 16 & 0xF);
    if (duration == 0) return "-";
    return std::string(a.w364 & 0x100000 ? "ban " : "injury ") + std::to_string(duration);
}

}  // namespace

void Console::register_squad_commands() {
    auto lineup = [this] { return prep().lineup; };
    auto refs_of = [](const ui::LineupState& l) {
        std::vector<PlayerRef> refs;
        for (const auto& p : l.players)
            if (p.player_id >= 0) refs.push_back({p.row, p.player_id, utf8(p.name)});
        return refs;
    };
    auto player_at = [refs_of](const ui::LineupState& l, const std::string& reference) -> ui::LineupPlayer {
        const auto refs = refs_of(l);
        const int row = refs[resolve_player(reference, refs)].row;
        return l.players[size_t(row)];
    };
    // DATEN-A / DATEN-B as the line-up screen draws them (src/ui/lineup.cpp, headers ClubInfo_Render_TextPointer3 / ClubInfo_Render_TextPointer4):
    // A = position, KO/TE/FO/strength and this season's games, goals, assists, yellow and red
    // cards (w364 >> 26, w364 bits 2-7, w368 bits 18-23, 14-17, 24-26); B = age, the seven
    // skills and the career numbers: internationals (bonus & 127), league and UEFA games (board
    // +7, +9), total games, goals and assists (board words +0x48, +0x4a, +0x4c).
    auto show_squad = [this, lineup] {
        const ui::LineupState l = lineup();
        std::vector<std::vector<std::string>> rows;
        for (const auto& p : l.players) {
            if (p.player_id < 0) continue;
            const auto& a = p.attributes;
            std::vector<std::string> row{"row:" + std::to_string(p.row), "id:" + std::to_string(p.player_id), utf8(p.name)};
            if (!squad_view_b_) {
                for (std::string v : {text(kPositionNames + a.position()), std::to_string(a.condition()), std::to_string(a.technique()),
                                      std::to_string(a.form()), std::to_string(a.strength()), std::to_string(a.age()),
                                      std::to_string(a.w364 >> 26), std::to_string((a.w364 >> 2) & 63),
                                      std::to_string((a.w368 >> 18) & 63), std::to_string((a.w368 >> 14) & 15),
                                      std::to_string((a.w368 >> 24) & 7)})
                    row.push_back(v);
            } else {
                row.push_back(std::to_string(a.age()));
                std::string skills;
                for (int i = 0; i < 7; ++i) skills += std::to_string(a.skill(Player::Skill(i)));
                row.push_back(skills);
                const auto word = [&](size_t at) { return std::to_string(p.board[at] | p.board[at + 1] << 8); };
                for (std::string v : {std::to_string(p.bonus & 127), std::to_string(p.board[7]), std::to_string(p.board[9]),
                                      word(0x48), word(0x4a), word(0x4c)})
                    row.push_back(v);
            }
            row.push_back(slot_state(p.slot));
            row.push_back(absence(a));
            rows.push_back(std::move(row));
        }
        if (!squad_view_b_)
            emit_table({"Row", "Id", "Player", "Pos", "K", "T", "F", "Str", "Age", "Gm", "Gls", "Ast", "YC", "RC", "Place", "Out"}, rows);
        else
            emit_table({"Row", "Id", "Player", "Age", "Skills(TSShPTaFkC 0-3)", "Int", "LgGm", "EuGm", "TotGm", "TotGls", "TotAst",
                        "Place", "Out"}, rows);
        // The starting eleven are the first rows of the squad list; a sale or the injury swap
        // moves the list up, so the reserve keeper can end up on the pitch (as in the
        // original). Point it out instead of leaving it to the match.
        int keepers = 0;
        for (const auto& p : l.players)
            if (p.player_id >= 0 && (p.slot & 0x8000) && p.attributes.position() == 0) ++keepers;
        if (keepers != 1) emit('i', "warning: " + std::to_string(keepers) + " goalkeepers on the pitch");
    };

    // --- Lineup (06-verein 1) -------------------------------------------------------------
    openers_[3] = [this, lineup, show_squad] {
        const ui::LineupState l = lineup();
        if (!l.entry_prepared) {
            const auto r = app::LineupActions::prepare(l);
            std::string error;
            require_engine(app::LineupActions(*session_).lineup(r, revision_, error), "line-up", error);
        }
        open_window("Lineup");
        show_squad();
    };
    add({"Lineup", "ShowSquad", {choice("view", {"A", "B"}, true)}, "the squad: values, strength, place", false},
        [show_squad](const Values&) { show_squad(); });
    add({"Lineup", "SetSquadView", {choice("view", {"A", "B"})}, "DATEN-A / DATEN-B", false}, [this, show_squad](const Values& v) {
        squad_view_b_ = v.choice("view") == 1;
        show_squad();
    });
    Param first = detail::text("first"), second = detail::text("second");
    first.display = "first:Player";
    second.display = "second:Player";
    // A click on one row and then on another exchanges the two places (bench, pitch, stands).
    auto swap = [this, lineup, player_at, show_squad](const std::string& a, const std::string& b) {
        const ui::LineupState l = lineup();
        const auto& p = player_at(l, a);
        const auto& q = player_at(l, b);
        if (p.row == q.row) fail("E_BAD_ARGUMENT", "two different players are needed");
        const auto r = app::LineupActions::swap(l, p, q);
        std::string error;
        require_engine(app::LineupActions(*session_).lineup(r, revision_, error), "line-up", error);
        emit('+', utf8(p.name) + " <-> " + utf8(q.name));
        show_squad();
    };
    add({"Lineup", "SwapPlayers", {first, second}, "exchange two players' places", true},
        [swap](const Values& v) { swap(v.text("first"), v.text("second")); });
    Param player = detail::text("player");
    player.display = "player:Player";
    add({"Lineup", "OpenPlayer", {player}, "the player card (PlayerCard)", false}, [this, lineup, player_at](const Values& v) {
        const auto& p = player_at(lineup(), v.text("player"));
        card_row_ = p.row;
        open_window("PlayerCard");
        show_player_card();
    });
    for (const char* name : {"OpenTeamList", "ShowForeigners"})
        add({"Lineup", name, {}, "not in the console yet", false},
            [name](const Values&) { fail("E_ENGINE_GAP", std::string(name) + " has no projection yet"); });

    // --- Lineup.Pitch ---------------------------------------------------------------------
    add({"Lineup.Pitch", "ShowPitch", {}, "the eleven with depth/lane, formations, automatic weights", false},
        [this](const Values&) { show_pitch(); });
    add({"Lineup.Pitch", "SwapPitch", {first, second}, "exchange two players on the pitch / bench", true},
        [this, player_at](const Values& v) {
            const auto projection = prep();
            if (!projection.pitch || !projection.pitch->indoor.has_value())
                fail("E_ENGINE_GAP", "no pitch mode projection");
            const ui::LineupState& l = projection.lineup;
            const auto& p = player_at(l, v.text("first"));
            const auto& q = player_at(l, v.text("second"));
            if (p.row == q.row) fail("E_BAD_ARGUMENT", "two different players are needed");
            const auto r = app::LineupActions::swap_pitch(projection, p, q);
            std::string error;
            require_engine(app::LineupActions(*session_).pitch_swap(r, revision_, error), "pitch", error);
            emit('+', utf8(p.name) + " <-> " + utf8(q.name));
        });
    // Moving a player, the formations and the automatic weights: the requests the SDL pitch
    // screen builds (src/ui/pitch.cpp click / save_formation), dispatched through
    // CareerSession::dispatch_pitch (PitchScreen pitch branch, Codex's source port).
    auto pitch_context = [this] {
        const auto p = prep();
        if (!p.pitch || !p.pitch->indoor.has_value()) fail("E_ENGINE_GAP", "no pitch mode projection");
        if (!p.lineup.entry_prepared) fail("E_RULE", "the line-up entry has not been prepared");
        return p;
    };
    auto send_pitch = [this](const ui::PitchRequest& r) {
        std::string error;
        require_engine(app::LineupActions(*session_).pitch(r, revision_, error), "pitch", error);
    };
    auto team_page_request = [this] {
        const auto p=prep();
        if(!p.pitch||!p.pitch->indoor.has_value()||!p.lineup.entry_prepared)
            fail("E_ENGINE_GAP","no prepared Source pitch projection");
        return app::LineupActions::page(p);
    };
    add({"Lineup", "OpenPitch", {}, "the pitch (Lineup.Pitch)", false}, [this, team_page_request, send_pitch](const Values&) {
        send_pitch(team_page_request());
        open_window("Lineup.Pitch");
        show_pitch();
    });
    closers_["Lineup.Pitch"]=[this,team_page_request,send_pitch,show_squad] {
        send_pitch(team_page_request());
        close_window();
        show_squad();
    };
    add({"Lineup.Pitch", "SetFormation", {integer("slot", 1, 8)}, "one of the eight formations (1-4 given, 5-8 own); the automatic line-up takes over", true},
        [this, pitch_context, send_pitch](const Values& v) {
            const int f = int(v.integer("slot")) - 1;
            if (!(prep().pitch->formation_mask & (1 << f))) fail("E_RULE", "formation slot " + std::to_string(f + 1) + " is empty");
            const auto r = app::LineupActions::formation(pitch_context(), ui::PitchRequestKind::apply_formation, f);
            send_pitch(r);
            show_pitch();
        });
    add({"Lineup.Pitch", "SaveFormation", {integer("slot", 5, 8)}, "save the current positions in an own formation slot (long press)", true},
        [this, pitch_context, send_pitch](const Values& v) {
            const auto p = pitch_context();
            for (int row = 0; row < 11; ++row) {
                const auto& pl = p.lineup.players.at(size_t(row));
                if (pl.player_id < 0 || !(pl.slot & 0x8000)) fail("E_RULE", "saving a formation needs eleven nominated starters");
            }
            const auto r = app::LineupActions::formation(p, ui::PitchRequestKind::save_formation, int(v.integer("slot")) - 1);
            send_pitch(r);
            emit('+', "formation " + std::to_string(r.formation + 1) + " saved");
        });
    add({"Lineup.Pitch", "MovePlayer", {first, integer("depth", 0, 19), integer("lane", 0, 8)},
         "place a starter freely (switches the automatic line-up off)", true},
        [this, lineup, player_at, pitch_context, send_pitch](const Values& v) {
            const ui::LineupState l = lineup();
            const auto& p = player_at(l, v.text("first"));
            if (p.row >= 11 || !(p.slot & 0x8000)) fail("E_RULE", "only a starter can be placed");
            const auto r = app::LineupActions::move(pitch_context(), p, int(v.integer("depth")), int(v.integer("lane")));
            send_pitch(r);
            show_pitch();
        });
    add({"Lineup.Pitch", "SetAutoWeights", {choice("kind", {"Position", "SideAndRole", "Fatigue"}), integer("value", 0, 3)},
         "an automatic line-up weight (the cup is clicked until the value); then SetFormation again", true},
        [this, pitch_context, send_pitch](const Values& v) {
            if (desk().options.mode == 1) fail("E_MODE_LOCKED", "no automatic weights in mode I");
            const int index = v.choice("kind"), shift = 2 * index, want = int(v.integer("value"));
            for (int guard = 0; guard < 4 && ((prep().pitch->automatic_weights >> shift) & 3) != want; ++guard) {
                const auto r = app::LineupActions::weight(pitch_context(), index);
                send_pitch(r);
            }
            emit('+', std::string("weight ") + v.text("kind") + " = " + std::to_string((prep().pitch->automatic_weights >> shift) & 3));
        });
    add({"Lineup.Pitch", "ShowAutoWeights", {}, "the three automatic line-up weights", false}, [this](const Values&) {
        const auto p = prep();
        if (!p.pitch) fail("E_ENGINE_GAP", "no pitch projection");
        const char* names[3] = {"Position", "SideAndRole", "Fatigue"};
        std::vector<std::vector<std::string>> rows;
        for (int i = 0; i < 3; ++i) rows.push_back({names[i], std::to_string((p.pitch->automatic_weights >> (2 * i)) & 3)});
        emit_table({"Weight", "Value"}, rows);
    });
    add({"Lineup.Pitch", "SetSquadView", {choice("view", {"A", "B"})}, "DATEN-A / DATEN-B", false}, [this](const Values&) { show_pitch(); });
    // --- Lineup.Order: ORDER (06-verein 1.5): the four instructions of each starter ----------
    auto show_orders = [this, lineup] {
        const ui::LineupState l = lineup();
        static const char* const kHead[5] = {"Row", "Player", "Offense", "Running", "Commitment"};
        std::vector<std::vector<std::string>> rows;
        for (const auto& pl : l.players) {
            if (pl.player_id < 0 || !(pl.slot & 0x8000)) continue;
            rows.push_back({"row:" + std::to_string(pl.row), utf8(pl.name), std::to_string(pl.board[2]), std::to_string(pl.board[3]),
                            std::to_string(pl.board[4]), std::to_string(pl.board[5])});
        }
        emit_table({kHead[0], kHead[1], kHead[2], kHead[3], kHead[4], "Fighting"}, rows);
        emit('i', "values " + std::to_string(kOrderMin) + ".." + std::to_string(kOrderMax));
    };
    add({"Lineup.Pitch", "OpenOrder", {}, "ORDER: the instructions of the starters (Lineup.Order)", false}, [this, show_orders](const Values&) {
        open_window("Lineup.Order");
        show_orders();
    });
    add({"Lineup.Order", "ShowOrders", {}, "offense, running, commitment, fighting of every starter", false},
        [show_orders](const Values&) { show_orders(); });
    add({"Lineup.Order", "SetOrder", {first, choice("aspect", {"Offense", "Run", "Commitment", "Fighting"}), integer("value", kOrderMin, kOrderMax)},
         "one instruction of a starter (as a click in its bar)", true},
        [this, lineup, player_at, show_orders](const Values& v) {
            const ui::LineupState l = lineup();
            const auto& p = player_at(l, v.text("first"));
            if (!app::LineupActions(*session_).order(l, p, v.choice("aspect"), int(v.integer("value")), revision_))
                fail("E_RULE", "only a starter on the pitch gets instructions");
            show_orders();
        });
    add({"Lineup.Pitch", "ShowPairs", {}, "the Hall partners stored on the tactics board", false},
        [this](const Values&) { show_pairs(); });
    add({"Lineup.Pitch", "PairPlayers", {first, second, integer("depth", 0, 19), integer("lane", 0, 8)},
         "place the first Hall starter beside the second at the explicit target coordinates", true},
        [this, player_at, pitch_context, send_pitch](const Values& v) {
            const auto projection = prep();
            if (!projection.pitch || projection.pitch->indoor != std::optional<bool>(true))
                fail("E_RULE", "pairs are only available for a Hall match");
            const auto first_player = player_at(projection.lineup, v.text("first"));
            const auto second_player = player_at(projection.lineup, v.text("second"));
            if (first_player.row == second_player.row)
                fail("E_BAD_ARGUMENT", "two different players are needed");
            if (first_player.row >= 11 || second_player.row >= 11 ||
                !(first_player.slot & 0x8000) || !(second_player.slot & 0x8000))
                fail("E_RULE", "only two nominated Hall starters can form a pair");
            const auto r = app::LineupActions::move(pitch_context(), first_player,
                int(v.integer("depth")), int(v.integer("lane")));
            const auto target = app::LineupActions(*session_).pair_target(r, second_player.row);
            if (target == app::LineupActions::PairTarget::invalid)
                fail("E_RULE", "the target is not a valid Hall placement");
            if (target != app::LineupActions::PairTarget::requested)
                fail("E_RULE", "the target must form the requested pair; use a free place beside the second player");
            send_pitch(r);
            show_pairs();
        });
    for (const char* name : {"OpenTeamList", "OpenMarking", "ShowZoneCoverage", "OpenSetPieces"})
        add({"Lineup.Pitch", name, {}, "not in the console yet", false},
            [name](const Values&) { fail("E_ENGINE_GAP", std::string(name) + " has no projection/request yet"); });

    // --- PlayerCard (06-verein 1.6) -------------------------------------------------------
    add({"PlayerCard", "ShowPlayer", {detail::text("player", 0, true)}, "the card (another player of the squad by name)", false},
        [this, lineup, player_at](const Values& v) {
            if (v.has("player")) card_row_ = player_at(lineup(), v.text("player")).row;
            show_player_card();
        });
    add({"PlayerCard", "OpenPlayer", {player}, "another player's card", false}, [this, lineup, player_at](const Values& v) {
        card_row_ = player_at(lineup(), v.text("player")).row;
        show_player_card();
    });
    add({"PlayerCard", "OpenPlayerStatistics", {}, "the development charts (own players)", false}, [this](const Values&) {
        open_window("PlayerCard.Statistics");
        show_player_statistics(0, true);
    });
    for (const char* name : {"Negotiate", "NationalSquadAdd", "NationalSquadRemove"})
        add({"PlayerCard", name, {}, "not in the console yet", false},
            [name](const Values&) { fail("E_ENGINE_GAP", std::string(name) + " comes with the transfer offers (stage 3b)"); });
    const std::vector<std::string> charts = {"Value", "Form", "Strength", "Total"};
    add({"PlayerCard.Statistics", "ShowPlayerStatistics", {choice("chart", charts, true), choice("mode", {"Absolute", "Relative"}, true)},
         "a development chart as a series", false},
        [this](const Values& v) {
            if (v.has("chart")) chart_ = v.choice("chart");
            if (v.has("mode")) chart_absolute_ = v.choice("mode") == 0;
            show_player_statistics(chart_, chart_absolute_);
        });
    add({"PlayerCard.Statistics", "SetChart", {choice("chart", charts)}, "SPIELER-WERT / FORM / STÄRKE / GESAMT", false}, [this](const Values& v) {
        chart_ = v.choice("chart");
        show_player_statistics(chart_, chart_absolute_);
    });
    add({"PlayerCard.Statistics", "SetMode", {choice("mode", {"Absolute", "Relative"})}, "ABS / REL", false}, [this](const Values& v) {
        chart_absolute_ = v.choice("mode") == 0;
        show_player_statistics(chart_, chart_absolute_);
    });
    add({"PlayerCard.Statistics", "BackToPlayerInfo", {}, "back to the card", false}, [this](const Values&) {
        close_window();
        show_player_card();
    });
}

void Console::show_player_card() {
    const auto p = prep();
    if (card_row_ < 0 || size_t(card_row_) >= p.player_cards.size()) fail("E_INTERNAL", "no such player card");
    const ui::PlayerCardState& card = p.player_cards[size_t(card_row_)];
    const auto& player = card.lineup.players.at(size_t(card.row));
    const auto& a = player.attributes;
    const auto le32 = [&](size_t at) {
        return uint32_t(player.board[at]) | uint32_t(player.board[at + 1]) << 8 | uint32_t(player.board[at + 2]) << 16 |
               uint32_t(player.board[at + 3]) << 24;
    };
    const auto value = ui::player_card_market_value(a, exe_);
    emit_table({"Card", "Value"},
               {{"name", utf8(player.name)},
                {"id", "id:" + std::to_string(player.player_id)},
                {"age", std::to_string(a.age())},
                {"nationality", utf8(card.nationality_name)},
                {"position", text(kPositionNames + a.position())},
                {"place", slot_state(player.slot)},
                {"out", absence(a)},
                {"condition/technique/form", std::to_string(a.condition()) + "/" + std::to_string(a.technique()) + "/" + std::to_string(a.form())},
                {"strength", std::to_string(a.strength())},
                {"contractMonths", player.board[0xc] == 255 ? "-" : std::to_string(player.board[0xc])},
                {"salary", money(le32(0x50))},
                {"purchasePrice", le32(0x54) ? money(le32(0x54)) : "-"},
                {"marketValue", value ? money(*value) : "?"},
                {"monthsAtClub", std::to_string(player.board[0x11] + 1)}});  // "Seit N Monaten im Verein" (ClubPlayerDetail_RenderCard_ExecutableField..ClubPlayerDetail_RenderCard_ExecutableField4)
    std::vector<std::vector<std::string>> skills;
    for (int i = 0; i < 7; ++i) skills.push_back({text(kSkillNames + i), text(kRatings + a.skill(Player::Skill(i)))});
    emit_table({"Skill", "Rating"}, skills);
    if (!card.unsupported_record.empty()) emit('i', "not shown: " + card.unsupported_record);
}

void Console::show_player_statistics(int chart, bool absolute) {
    const auto p = prep();
    if (card_row_ < 0 || size_t(card_row_) >= p.player_cards.size()) fail("E_INTERNAL", "no such player card");
    const ui::PlayerCardState& card = p.player_cards[size_t(card_row_)];
    if (!card.statistics) fail("E_RULE", "statistics only for own players");
    const auto points = ui::player_statistics_points(card, exe_, chart, absolute);
    const char* names[4] = {"Value", "Form", "Strength", "Total"};
    std::string series;
    for (const auto& [month, v] : points) series += (series.empty() ? "" : " ") + std::to_string(month) + ":" + std::to_string(v);
    emit('i', std::string(names[chart]) + (absolute ? " (absolute)" : " (relative)") + ": " + (series.empty() ? "-" : series));
}

void Console::show_pairs() {
    const auto p = prep();
    if (!p.pitch || p.pitch->indoor != std::optional<bool>(true))
        fail("E_RULE", "pairs are only available for a Hall match");
    std::vector<std::vector<std::string>> rows;
    int pairs = 0;
    for (const auto& pl : p.lineup.players) {
        if (pl.row >= 11 || pl.player_id < 0 || !(pl.slot & 0x8000)) continue;
        const size_t partner = pl.board[0x3f];
        std::string partner_row = "-", partner_name = "-";
        if (partner < 11 && partner != size_t(pl.row) && partner < p.lineup.players.size()) {
            const auto& other = p.lineup.players[partner];
            if (other.player_id >= 0 && (other.slot & 0x8000) && other.board[0x3f] == pl.row) {
                partner_row = "row:" + std::to_string(other.row);
                partner_name = utf8(other.name);
                if (pl.row < other.row) ++pairs;
            }
        }
        rows.push_back({"row:" + std::to_string(pl.row), utf8(pl.name), partner_row, partner_name});
    }
    emit_table({"Row", "Player", "PartnerRow", "Partner"}, rows);
    emit('i', "Hall pairs: " + std::to_string(pairs));
}

void Console::show_pitch() {
    const auto p = prep();
    std::vector<std::vector<std::string>> rows;
    for (const auto& pl : p.lineup.players) {
        if (pl.player_id < 0 || !(pl.slot & 0xc000)) continue;
        rows.push_back({"row:" + std::to_string(pl.row), utf8(pl.name), slot_state(pl.slot),
                        (pl.slot & 0x8000) ? std::to_string(int8_t(pl.board[0x41])) : "-",
                        (pl.slot & 0x8000) ? std::to_string(int8_t(pl.board[0x42])) : "-"});
    }
    emit_table({"Row", "Player", "Place", "Depth", "Lane"}, rows);
    if (p.pitch) {
        std::string saved;
        for (int f = 0; f < 8; ++f)
            if (p.pitch->formation_mask & (1 << f)) saved += (saved.empty() ? "" : " ") + std::to_string(f + 1);
        emit('i', "formations available: " + (saved.empty() ? std::string("-") : saved) + "; automatic formation " +
                      std::to_string(p.lineup.automatic_formation));
        if (p.pitch->indoor == std::optional<bool>(true)) show_pairs();
    }
}

}  // namespace bmh::cli::console
