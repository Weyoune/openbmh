// SPDX-License-Identifier: GPL-3.0-only
#include "formats/content.hpp"
#include "formats/executable_layout.hpp"
// Information windows of docs/CLI_CONCEPT/05-information.md that the engine can already
// project: TeamInfo (club information, from LeagueTable / Fixtures), HighScore (menu 26) and
// TopScorers (menu 20).
//
// TeamInfo opens like the SDL app's club information (src/app/main.cpp
// present_club_information): CareerSession::enter_club_information prepares the entry
// (OpenTransferEditor's entry step, prepare_club_info_entry) for league teams, then the pure
// ui::club_info_from_game projection is read.
//
// Strengths, Achievements, OtherGames, TopScorers, FairPlay, Statistics and Calendar have no
// projection in the engine/UI yet; Open() answers E_ENGINE_GAP for them (windows_desk.cpp).

#include <algorithm>
#include <memory>

#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"
#include "game/highscore.hpp"
#include "game/schedule.hpp"
#include "game/top_scorers.hpp"
#include "ui/club_info.hpp"
#include "ui/league_table.hpp"

namespace bmh::cli::console {

using namespace detail;

namespace {

constexpr int kPositionNames = 629;   // positionGoalkeeperCaps "TOR", ABW, MIT, STU
constexpr int kHighscorePrefix = 889;  // the high score file prefix ("high/h.") of TEXTD

}  // namespace

void Console::register_info_commands() {
    // --- TopScorers (05-information 7, menu 20) -------------------------------------------
    // Codex's read-only port of TopScorersFromGame (game/top_scorers.hpp): the best ten of the own
    // league by goals, assists or both (one point each), for the whole league or only the
    // managers' clubs. No state changes and no random draws, so the console reads it directly.
    struct ScorerView {
        game::TopScorersMetric metric = game::TopScorersMetric::goals;
        game::TopScorersScope scope = game::TopScorersScope::league;
    };
    auto scorers = std::make_shared<ScorerView>();
    auto show_scorers = [this, scorers] {
        const game::TopScorersProjection t = game::top_scorers_from_game(session_->state(), active_slot(), scorers->metric, scorers->scope);
        static const char* const kMetric[3] = {"goals", "assists", "scorer points"};
        emit('i', std::string("top ") + kMetric[int(scorers->metric)] +
                      (scorers->scope == game::TopScorersScope::league ? ", whole league" : ", managers' clubs only"));
        if (t.no_results) {
            emit('i', "no results yet");
            return;
        }
        std::vector<std::vector<std::string>> rows;
        for (const auto& r : t.rows)
            rows.push_back({std::to_string(r.rank), "id:" + std::to_string(r.player), utf8(r.player_name), utf8(r.team_name),
                            std::to_string(r.goals), std::to_string(r.assists), std::to_string(r.score)});
        emit_table({"Rank", "Id", "Player", "Club", "Goals", "Assists", "Points"}, rows);
    };
    openers_[20] = [this, scorers, show_scorers] {
        *scorers = ScorerView{};
        open_window("TopScorers");
        show_scorers();
    };
    Param list = choice("list", {"Goals", "Assists", "Score"}, true), scope = choice("scope", {"League", "Managers"}, true);
    add({"TopScorers", "ShowScorers", {list, scope}, "the best ten: goals, assists or both; whole league or managers' clubs", false},
        [scorers, show_scorers](const Values& v) {
            if (v.has("list")) scorers->metric = game::TopScorersMetric(v.choice("list"));
            if (v.has("scope")) scorers->scope = game::TopScorersScope(v.choice("scope"));
            show_scorers();
        });
    Param list_needed = choice("list", {"Goals", "Assists", "Score"}), scope_needed = choice("scope", {"League", "Managers"});
    add({"TopScorers", "SetList", {list_needed}, "TORSCHÜTZEN / VORLAGEN / SCORER", false}, [scorers, show_scorers](const Values& v) {
        scorers->metric = game::TopScorersMetric(v.choice("list"));
        show_scorers();
    });
    add({"TopScorers", "SetScope", {scope_needed}, "GESAMTE LIGA / NUR MANAGER", false}, [scorers, show_scorers](const Values& v) {
        scorers->scope = game::TopScorersScope(v.choice("scope"));
        show_scorers();
    });

    // --- TeamInfo (05-information 4) ------------------------------------------------------
    // A club of the nine leagues by name (unique part) or club:<runtime id>.
    auto team_of = [this](const std::string& reference) {
        std::vector<std::pair<int, std::string>> teams;
        for (int league = 0; league < kLeagueCount; ++league)
            for (const auto& row : ui::league_table_from_game(session_->state(), active_slot(), league).rows)
                teams.push_back({row.runtime_team_id, row.name});
        std::string number = lower(reference).rfind("club:", 0) == 0 ? reference.substr(5) : reference;
        Arg a;
        a.text = number;
        if (const auto id = to_int(a)) {
            for (const auto& t : teams)
                if (t.first == *id) return t.first;
            fail("E_BAD_ARGUMENT", "no club " + reference + " in the leagues");
        }
        std::vector<int> exact, partial;
        for (const auto& t : teams) {
            const std::string name = fold(t.second);
            if (name == fold(reference)) exact.push_back(t.first);
            else if (name.find(fold(reference)) != std::string::npos) partial.push_back(t.first);
        }
        const auto& found = exact.empty() ? partial : exact;
        if (found.size() == 1) return found.front();
        if (found.empty()) fail("E_BAD_ARGUMENT", "no club named \"" + reference + "\" in the leagues");
        std::string candidates;
        for (size_t i = 0; i < found.size() && i < 8; ++i)
            candidates += (i ? ", " : "") + club_text(session_->state(), found[i]) + " = club:" + std::to_string(found[i]);
        fail("E_AMBIGUOUS", "\"" + reference + "\" fits several clubs: " + candidates);
    };
    auto show_team = [this] {
        if (!team_info_) fail("E_INTERNAL", "no club information");
        const auto& t = *team_info_;
        emit('i', utf8(t.original_club_name) + (t.has_squad ? "" : " (no squad data)"));
        std::vector<std::vector<std::string>> rows;
        for (const auto& p : t.players) {
            if (p.player_id < 0) continue;  // free squad places
            const auto& a = p.attributes;
            rows.push_back({"id:" + std::to_string(p.player_id), utf8(p.original_name), text(kPositionNames + a.position()),
                            std::to_string(a.age()), std::to_string(a.condition()), std::to_string(a.technique()),
                            std::to_string(a.form()), std::to_string(a.strength()), p.borrowed ? "[loan]" : ""});
        }
        emit_table({"Id", "Player", "Pos", "Age", "K", "T", "F", "Str", ""}, rows);
    };
    auto open_team = [this, team_of, show_team](const std::string& reference) {
        const int team = team_of(reference);
        // The club-info entry rewrites the squad flags of the club (PrepareLineupEntry), which makes the
        // engine's pairings draft stale: the fixtures page could neither act nor close any
        // more. So the fixtures page closes first; GoBack from TEAM-INFO then returns to the
        // window the fixtures were opened from.
        if (window_ == "Fixtures" && pairings_) fixtures_action(LeaguePairingsAction::close);
        std::string error;
        ClubInfoEntry entry;
        if (team < int(exe_.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField))) {  // league teams get the entry step (as the app does)
            const auto prepared = session_->enter_club_information(team, error);
            if (!prepared) fail("E_RULE", "club information: " + error);
            entry = *prepared;
        }
        team_info_ = ui::club_info_from_game(session_->state(), team, entry, exe_, texts_);
        // from TEAM-INFO itself the page is replaced: one club information at a time, so
        // GoBack/CloseTeamInfo leads back to where the first one was opened
        if (window_ != "TeamInfo") open_window("TeamInfo");
        show_team();
    };
    Param club = detail::text("club");
    club.display = "club:Club";
    for (const char* window : {"LeagueTable", "Fixtures", "TeamInfo"})
        add({window, "OpenTeamInfo", {club}, "TEAM-INFO of a club: club:<n> or a name", false},
            [open_team](const Values& v) { open_team(v.text("club")); });
    add({"TeamInfo", "ShowTeamSquad", {choice("view", {"A", "B"}, true)}, "the club's squad", false}, [show_team](const Values&) { show_team(); });
    add({"TeamInfo", "ShowPlayer", {detail::text("player")}, "a player of this club", false}, [this](const Values& v) {
        if (!team_info_) fail("E_INTERNAL", "no club information");
        // free squad places (player -1) are no players; `index` maps refs back to the rows
        std::vector<PlayerRef> refs;
        std::vector<size_t> index;
        for (size_t i = 0; i < team_info_->players.size(); ++i) {
            const auto& p = team_info_->players[i];
            if (p.player_id < 0) continue;
            refs.push_back({p.row, p.player_id, utf8(p.original_name)});
            index.push_back(i);
        }
        const auto& p = team_info_->players[index[resolve_player(v.text("player"), refs)]];
        const auto& a = p.attributes;
        emit_table({"Player", "Value"},
                   {{"name", utf8(p.original_name)},
                    {"id", "id:" + std::to_string(p.player_id)},
                    {"position", text(kPositionNames + a.position())},
                    {"age", std::to_string(a.age())},
                    {"condition/technique/form", std::to_string(a.condition()) + "/" + std::to_string(a.technique()) + "/" + std::to_string(a.form())},
                    {"strength", std::to_string(a.strength())}});
    });
    add({"TeamInfo", "CloseTeamInfo", {}, "back to where it was opened", false}, [this](const Values&) {
        team_info_.reset();
        close_window();
        emit('+', "club information closed");
    });
    for (const char* name : {"ShowTeamStatistics", "SetBroadcast"})
        add({"TeamInfo", name, {}, "not in the console yet", false},
            [name](const Values&) { fail("E_ENGINE_GAP", std::string(name) + " is not in the console yet"); });

    // --- HighScore (menu 26, H2 p. 94) ----------------------------------------------------
    auto show_highscore = [this] {
        const GameState& g = session_->state();
        const uint16_t signature = g.memory.has(::bmh::layout::CareerSession_FormatError_RuntimeField7) ? g.memory.u16(::bmh::layout::CareerSession_FormatError_RuntimeField7) : 0;
        if (signature != 0) {
            // the ranked list of this game's level, stored next to the saved games
            const auto file = highscore_file(save_directory(), signature, texts_.strings.at(kHighscorePrefix),
                                             bool(content::find(options_.data)));
            HighscoreTable rows{};
            std::error_code ec;
            if (std::filesystem::exists(file, ec)) rows = load_highscore_table(file);
            std::vector<std::vector<std::string>> table;
            int rank = 0;
            for (const auto& r : rows)
                if (!r.manager_name.empty())
                    table.push_back({std::to_string(++rank), utf8(r.manager_name), utf8(r.club_name), std::to_string(r.score)});
            if (table.empty()) emit('i', "the high score list is empty");
            else emit_table({"Rank", "Manager", "Club", "Score"}, table);
        } else {
            emit('i', "no ranked high score list in this game (level changed or signature 0)");
        }
        const HighscoreScore s = highscore_score(g, active_slot());
        emit_table({"Score", "Points"}, {{"assets", std::to_string(s.assets)},
                                          {"patience", std::to_string(s.patience)},
                                          {"facilities", std::to_string(s.facilities)},
                                          {"team", std::to_string(s.team)},
                                          {"history", std::to_string(s.history)},
                                          {"titles", std::to_string(s.titles)},
                                          {"total", std::to_string(s.total)}});
    };
    openers_[26] = [this, show_highscore] {
        open_window("HighScore");
        show_highscore();
    };
    add({"HighScore", "ShowHighScore", {}, "the high score list and your current score", false},
        [show_highscore](const Values&) { show_highscore(); });
}

}  // namespace bmh::cli::console
