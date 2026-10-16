// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "cli/protocol.hpp"
#include "ui/league_table.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "app/career_session.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "ui/desktop.hpp"
#include "ui/mailbox_state.hpp"
#include "ui/manager.hpp"

namespace {
using namespace bmh;
using cli::Json;
using Object = Json::Object;
int counted_rng_draws = 0;
void count_rng_draw(int) { ++counted_rng_draws; }

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Json request(Object fields) { return Json(std::move(fields)); }
std::string error(const Json& response) {
    auto it = response.object()->find("error");
    return it == response.object()->end() || !it->second.string() ? "" : *it->second.string();
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        const std::filesystem::path data(argv[1]);
        constexpr std::uint32_t seed = 8128;
        cli::Protocol protocol(data, seed);

        const Json help = protocol.handle(request({{"cmd", "help"}}));
        check(*help.object()->at("ok").boolean(), "help should succeed");
        check(help.object()->at("help").object()->at("commands").array()->size() >= 10, "help must list commands");
        check(error(protocol.handle(request({{"cmd", "__protocol_error"}, {"detail", "bad json"}}))) == "invalid_request",
              "parse error sentinel should be accepted");
        check(error(protocol.handle(request({{"cmd", "status"}, {"amount", 1}}))) == "invalid_request",
              "command-specific unexpected fields must be rejected");

        const Json started = protocol.handle(request({
            {"cmd", "new"}, {"options", Json(Object{{"managers", 1}})},
            {"managers", Json(Json::Array{Json(Object{{"name", "SIM"}, {"master_club", 0}, {"strength", 2}})})}}));
        check(*started.object()->at("ok").boolean(), "new career failed");
        check(protocol.revision() == 1 && protocol.active(), "new career envelope incorrect");
        const auto before = protocol.handle(request({{"cmd", "desk"}}));
        const auto previous_draw_hook = draw_hook;
        const int draws_before_mail = counted_rng_draws;
        draw_hook = count_rng_draw;

        const Json mail_help = protocol.handle(request({{"cmd", "help"}}));
        const std::string mail_help_json = cli::dump_json(mail_help.object()->at("help"));
        check(mail_help_json.find("mail") != std::string::npos &&
              mail_help_json.find("read-only owned mailbox projection") != std::string::npos,
              "help describes the read-only mail projection");
        const Json mail_json = protocol.handle(request({{"cmd", "mail"}, {"manager", 0}}));
        check(*mail_json.object()->at("ok").boolean() && protocol.revision() == 1,
              "JSON mail projection succeeds without changing revision");
        const Json& mail_state = mail_json.object()->at("state");
        check(*mail_state.object()->at("manager_slot").integer() == 0,
              "mail projection identifies the explicitly selected manager");
        for (const Json& message : *mail_state.object()->at("messages").array()) {
            const auto& fields = *message.object();
            check(fields.find("slot_snapshot_id") != fields.end() && fields.find("recipient_mask") != fields.end() &&
                  fields.find("keep_mask") != fields.end() && fields.find("date_counter") != fields.end() &&
                  fields.find("text") != fields.end() && fields.find("kind") != fields.end() &&
                  *fields.at("kind").string() == "unclassified" && fields.find("kept_for_manager") != fields.end(),
                  "mail message contains owned snapshot and mask metadata without a read-state claim");
        }
        const Json mail_text = protocol.handle(cli::text_request("mail 0", &protocol));
        check(*mail_text.object()->at("ok").boolean() &&
              cli::dump_json(mail_text.object()->at("state")) == cli::dump_json(mail_state),
              "text mail command returns the same read-only projection");
        check(error(protocol.handle(request({{"cmd", "mail"}}))) == "invalid_request" &&
              error(protocol.handle(request({{"cmd", "mail"}, {"manager", 4}}))) == "invalid_request" &&
              error(protocol.handle(request({{"cmd", "mail"}, {"manager", 1}}))) == "invalid_manager_slot",
              "mail requires an existing manager slot in the original 0..3 range");
        check(protocol.revision() == 1 && cli::dump_json(before) ==
              cli::dump_json(protocol.handle(request({{"cmd", "desk"}}))) &&
              cli::dump_json(mail_json) == cli::dump_json(protocol.handle(request({{"cmd", "mail"}, {"manager", 0}}))),
              "repeated/invalid mail reads do not mutate revision or desk/mail projection");
        check(counted_rng_draws == draws_before_mail, "mail projection consumes no RNG draws");
        draw_hook = previous_draw_hook;

        const auto save_path = std::filesystem::temp_directory_path() / "openbmh-cli-protocol-test.fbm";
        std::filesystem::remove(save_path);
        const Json saved = protocol.handle(request({{"cmd", "save"}, {"manager", 0}, {"revision", 1},
            {"path", save_path.string()}}));
        check(*saved.object()->at("ok").boolean() && std::filesystem::exists(save_path),
              "CareerSession save failed: " + cli::dump_json(saved));

        const Json stale = protocol.handle(request({{"cmd", "kickoff"}, {"manager", 1}, {"revision", 1}}));
        check(error(stale) == "stale_manager_or_revision", "foreign manager must be rejected");
        check(protocol.revision() == 1, "foreign manager changed revision");
        const auto after = protocol.handle(request({{"cmd", "desk"}}));
        check(cli::dump_json(before) == cli::dump_json(after), "foreign request changed projected state");

        const Json stale_save = protocol.handle(request({{"cmd", "save"}, {"manager", 0}, {"revision", 0},
            {"path", (save_path.string() + ".stale")}}));
        check(error(stale_save) == "stale_manager_or_revision", "stale save must be rejected");
        check(!std::filesystem::exists(save_path.string() + ".stale"), "stale save wrote a file");
        const auto missing_path = save_path.string() + ".missing";
        const Json failed_load = protocol.handle(request({{"cmd", "load"}, {"path", missing_path}}));
        check(!*failed_load.object()->at("ok").boolean() && protocol.revision() == 1,
              "failed load changed active career or revision");
        check(cli::dump_json(before) == cli::dump_json(protocol.handle(request({{"cmd", "desk"}}))),
              "failed load changed the projected state");
        const Json loaded = protocol.handle(request({{"cmd", "load"}, {"path", save_path.string()}}));
        check(*loaded.object()->at("ok").boolean() && protocol.revision() == 2, "valid CareerSession load failed");
        // Compare a fresh CLI career with the same season_sim setup and kickoff sequence.
        app::CareerSession reference(data, seed);
        ui::SetupOptions options;
        ui::ManagerChoice choice;
        choice.name = "SIM";
        choice.master_club = 0;
        choice.strength = 2;
        std::string reason;
        check(reference.confirm_manager(options, choice, reason), "reference manager setup failed");
        ui::CareerInitializationResult init;
        check(reference.finish(init, reason), "reference career start failed");
        cli::Protocol progression(data, seed);
        const Json progression_start = progression.handle(request({
            {"cmd", "new"}, {"options", Json(Object{{"managers", 1}})},
            {"managers", Json(Json::Array{Json(Object{{"name", "SIM"}, {"master_club", 0}, {"strength", 2}})})}}));
        check(*progression_start.object()->at("ok").boolean(), "CLI progression setup failed");
        int cli_manager = static_cast<int>(*progression_start.object()->at("state").object()->at("manager_slot").integer());
        const Json before_foreign = progression.handle(request({{"cmd", "desk"}}));
        const Json foreign = progression.handle(request({{"cmd", "kickoff"}, {"manager", 1}, {"revision", 1}}));
        check(error(foreign) == "stale_manager_or_revision" && progression.revision() == 1,
              "foreign manager was not rejected without advancing revision");
        check(cli::dump_json(before_foreign) == cli::dump_json(progression.handle(request({{"cmd", "desk"}}))),
              "foreign manager changed projection before deterministic comparison");

        for (int turn = 0; turn < 60; ++turn) {
            const Json advanced = progression.handle(request({{"cmd", "kickoff"}, {"manager", cli_manager},
                {"revision", static_cast<std::int64_t>(progression.revision())}}));
            check(*advanced.object()->at("ok").boolean(), "CLI kickoff failed at turn " + std::to_string(turn + 1) + ": " + cli::dump_json(advanced));
            ui::DeskRequest kickoff;
            kickoff.kind = ui::DeskRequestKind::advance_turn;
            kickoff.manager_slot = int(reference.state().memory.u8(::bmh::layout::ActiveManager));
            kickoff.revision = init.desktop.revision;
            const auto reference_turn = reference.dispatch_kickoff(kickoff, init.desktop.revision, reason);
            check(reference_turn.kind != TurnDeskKind::Failed, "reference kickoff failed at turn " + std::to_string(turn + 1));
            Json expected_desk = cli::parse_json(ui::desk_json(reference.desktop_projection(init.desktop.revision)));
            expected_desk.object()->insert_or_assign("engine_attached", true);  // every protocol desk is the engine's
            check(cli::dump_json(advanced.object()->at("state")) == cli::dump_json(expected_desk),
                  "CLI career progression differs from season_sim at turn " + std::to_string(turn + 1));
            const auto result_view = progression.handle(request({{"cmd", "results"}}));
            check(*result_view.object()->at("ok").boolean() &&
                  cli::dump_json(result_view.object()->at("state")) == cli::dump_json(advanced.object()->at("results")),
                  "read-only results agree with the completed kickoff response");
            check(*result_view.object()->at("state").object()->at("valid").boolean() ==
                  bool(reference.completed_matchday_results()), "CLI result presence agrees with the shared session path");
            if (reference.completed_matchday_results()) {
                const auto& results = *reference.completed_matchday_results();
                const auto& json = *result_view.object()->at("state").object();
                check(*json.at("date").integer() == results.date && json.at("groups").array()->size() == results.groups.size(),
                      "CLI returns the played event date and all result groups");
                check(*json.at("groups").array()->at(0).object()->at("matches").array()->at(0).object()->at("home").string() ==
                      results.groups[0].matches[0].home, "CLI preserves the owned UTF-8 team name");
            }
            cli_manager = static_cast<int>(*advanced.object()->at("state").object()->at("manager_slot").integer());
        }

        const auto table_manager = static_cast<int>(*progression.handle(request({{"cmd", "desk"}}))
            .object()->at("state").object()->at("manager_slot").integer());
        const int table_league = team_league(reference.state().manager_teams[table_manager]);
        check(table_league >= 0 && table_league < kLeagueCount, "reference manager has a league for table query");
        const auto table_revision = progression.revision();
        const auto table_desk = progression.handle(request({{"cmd", "desk"}}));
        const auto table_query = [&] (int league) {
            return progression.handle(request({{"cmd", "table"}, {"manager", table_manager}, {"league", league}}));
        };
        const Json standings = table_query(table_league);
        check(*standings.object()->at("ok").boolean() && progression.revision() == table_revision,
              "table query is read-only and keeps the current revision");
        const auto& table_state = *standings.object()->at("state").object();
        check(*table_state.at("manager_slot").integer() == table_manager &&
              *table_state.at("league_id").integer() == table_league &&
              table_state.at("rows").array()->size() == size_t(kLeagueSizes[table_league]),
              "table query returns the explicitly selected league and its full roster");
        const auto expected_table = compute_league_table(reference.state(), table_league, 0,
            league_matchdays(table_league), 7, 1);
        std::array<int, 20> expected_order{};
        for (int i = 0; i < expected_table.size; ++i) expected_order[i] = i;
        std::sort(expected_order.begin(), expected_order.begin() + expected_table.size,
            [&](int lhs, int rhs) { return expected_table.rank(lhs) < expected_table.rank(rhs); });
        const auto& table_rows = *table_state.at("rows").array();
        for (int i = 0; i < expected_table.size; ++i) {
            const int club = expected_order[i];
            const int runtime = league_first_team(table_league) + club;
            const auto& row = *table_rows[size_t(i)].object();
            check(*row.at("rank").integer() == expected_table.rank(club) + 1 &&
                  *row.at("runtime_team_id").integer() == runtime &&
                  *row.at("club_identity_key").integer() == reference.state().club_ids[runtime] &&
                  *row.at("name").string() == ui::original_to_utf8(reference.state().club_name(runtime)) &&
                  *row.at("games").integer() == expected_table.games(club) &&
                  *row.at("wins").integer() == expected_table.wins[club] &&
                  *row.at("losses").integer() == expected_table.defeats[club] &&
                  *row.at("points").integer() == expected_table.points[club],
                  "CLI table preserves the engine's rank, identity, UTF-8 name, and statistics");
            check(*row.at("rank").integer() == i + 1, "table rows follow engine-computed rank order");
        }
        check(cli::dump_json(standings) == cli::dump_json(table_query(table_league)) &&
              cli::dump_json(table_desk) == cli::dump_json(progression.handle(request({{"cmd", "desk"}}))) &&
              progression.revision() == table_revision,
              "repeated table reads leave the projected career and revision unchanged");
        check(*table_state.at("points_mode").integer() == 1 &&
              *table_state.at("league_name").string() ==
                  ui::league_table_from_game(reference.state(), table_manager, table_league).league_name,
              "table uses the current game point rule and the table screen's league name");
        const Json other_table = progression.handle(cli::text_request("table " + std::to_string(table_manager) + " " +
            std::to_string((table_league + 1) % kLeagueCount), &progression));
        check(*other_table.object()->at("ok").boolean() &&
              *other_table.object()->at("state").object()->at("manager_slot").integer() == table_manager &&
              *other_table.object()->at("state").object()->at("league_id").integer() ==
                  (table_league + 1) % kLeagueCount && progression.revision() == table_revision,
              "text table request dispatches the explicit manager and league selectors read-only");
        const Json invalid_league = table_query(kLeagueCount);
        check(error(invalid_league) == "invalid_league" && progression.revision() == table_revision &&
              cli::dump_json(table_desk) == cli::dump_json(progression.handle(request({{"cmd", "desk"}}))),
              "invalid league is rejected without changing career state");
        check(error(progression.handle(request({{"cmd", "table"}, {"manager", table_manager + 1},
                                                {"league", table_league}}))) == "stale_manager_or_revision" &&
              error(progression.handle(request({{"cmd", "table"}, {"league", table_league}}))) == "invalid_request" &&
              progression.revision() == table_revision,
              "table requests require the active manager and explicit selectors");

        const auto expected_mail = ui::mailbox_state_from_game(reference.state(), table_manager);
        check(!expected_mail.messages.empty(), "played career produces actual manager messages");
        const auto played_mail = progression.handle(request({{"cmd", "mail"}, {"manager", table_manager}}));
        check(*played_mail.object()->at("ok").boolean(), "played career mailbox is available");
        const auto& mail_rows = *played_mail.object()->at("state").object()->at("messages").array();
        check(mail_rows.size() == expected_mail.messages.size(), "CLI exposes all actual Original mailbox entries");
        for (size_t i = 0; i < mail_rows.size(); ++i) {
            const auto& actual = *mail_rows[i].object();
            const auto& expected = expected_mail.messages[i];
            check(*actual.at("slot_snapshot_id").integer() == expected.entry_index &&
                  *actual.at("recipient_mask").integer() == expected.recipient_mask &&
                  *actual.at("keep_mask").integer() == expected.kept_by_mask &&
                  *actual.at("kept_for_manager").boolean() == expected.kept_for_manager &&
                  *actual.at("date_counter").integer() == expected.date_counter &&
                  *actual.at("text").string() == expected.text,
                  "CLI preserves owned text, chronological order and Original mailbox masks after actual games");
        }
        check(progression.revision() == table_revision, "played mailbox remains read-only");

        const auto tactics_before = protocol.handle(request({{"cmd", "tactics"}}));
        check(*tactics_before.object()->at("ok").boolean() && protocol.revision() == 2,
              "read-only tactics are available after loading without changing revision");
        const auto empty_results = protocol.handle(cli::text_request("results", &protocol));
        check(*empty_results.object()->at("ok").boolean() &&
              !*empty_results.object()->at("state").object()->at("valid").boolean(),
              "loaded desk without a newly completed event returns an explicit empty result view");
        const auto tactic_changed = protocol.handle(cli::text_request("tactics.set 0 3", &protocol));
        check(*tactic_changed.object()->at("ok").boolean() && protocol.revision() == 3 &&
              *tactic_changed.object()->at("state").object()->at("values").array()->at(0).integer() == 3 &&
              *tactic_changed.object()->at("state").object()->at("assistant_attitude").integer() == 0,
              "text command dispatches tactic and its original assistant side effect");
        const auto tactics_after = protocol.handle(cli::text_request("tactics", &protocol));
        check(cli::dump_json(tactics_after.object()->at("state")) == cli::dump_json(tactic_changed.object()->at("state")),
              "read-only tactics return the authoritative changed values");
        check(error(protocol.handle(request({{"cmd", "tactics.set"}, {"manager", 0}, {"revision", 2},
                                             {"index", 5}, {"value", 1}}))) == "action_rejected",
              "stale CLI tactic is rejected");
        check(error(protocol.handle(request({{"cmd", "tactics.set"}, {"manager", 1}, {"revision", 3},
                                             {"index", 5}, {"value", 1}}))) == "action_rejected",
              "foreign-manager CLI tactic is rejected");
        check(error(protocol.handle(request({{"cmd", "tactics.set"}, {"manager", 0}, {"revision", 3},
                                             {"index", 4}, {"value", 11}}))) == "action_rejected",
              "CLI respects the individual original tactic limits");
        check(protocol.revision() == 3 && cli::dump_json(protocol.handle(request({{"cmd", "tactics"}}))) == cli::dump_json(tactics_after),
              "invalid tactic commands leave revision and projected state untouched");

        bool malformed_rejected = false;
        try { (void)cli::parse_json("{\"cmd\":\"status\",}"); }
        catch (const cli::JsonError&) { malformed_rejected = true; }
        check(malformed_rejected, "malformed JSON accepted");

        // Training CLI actions run only after all same-seed kickoff comparisons above.
        // They use a separate protocol instance, so earlier progression assertions stay pristine.
        cli::Protocol training_protocol(data, seed);
        const Json training_started = training_protocol.handle(request({
            {"cmd", "new"}, {"options", Json(Object{{"managers", 1}})},
            {"managers", Json(Json::Array{Json(Object{{"name", "SIM"}, {"master_club", 0}, {"strength", 2}})})}}));
        check(*training_started.object()->at("ok").boolean(), "training test career setup failed");
        const Json training_help = cli::Protocol::help();
        const auto help_training = cli::dump_json(training_help);
        check(help_training.find("training.initialize") != std::string::npos &&
              help_training.find("training.focus") != std::string::npos,
              "help lists read-only and mutating training commands");
        const auto& unavailable = *training_help.object()->at("not_available").array();
        check(std::none_of(unavailable.begin(), unavailable.end(), [](const Json& item) {
                  return item.string() && *item.string() == "training";
              }), "training is no longer reported unavailable");

        const auto training_read = [&] {
            return training_protocol.handle(cli::text_request("training", &training_protocol));
        };
        const auto read_one = training_read();
        check(*read_one.object()->at("ok").boolean() && training_protocol.revision() == 1,
              "read-only training projection succeeds without revision change");
        check(*read_one.object()->at("state").object()->at("engine_attached").boolean(),
              "CLI training state reports its real CareerSession adapter");
        const auto read_two = training_read();
        check(cli::dump_json(read_one.object()->at("state")) == cli::dump_json(read_two.object()->at("state")) &&
              training_protocol.revision() == 1,
              "repeated training reads are identical and do not initialize or revise state");
        const Json foreign_training = training_protocol.handle(request({{"cmd", "training.type"}, {"manager", 1},
            {"revision", 1}, {"index", 0}, {"value", 1}}));
        check(error(foreign_training) == "stale_manager_or_revision" && training_protocol.revision() == 1 &&
              cli::dump_json(read_one.object()->at("state")) ==
                  cli::dump_json(training_read().object()->at("state")),
              "foreign training manager is rejected without changing state");
        const Json missing_training_envelope = training_protocol.handle(request({{"cmd", "training.auto"},
            {"index", 0}, {"value", 1}}));
        check(error(missing_training_envelope) == "invalid_request" && training_protocol.revision() == 1,
              "mutating training JSON requires manager and revision");
        const auto state_types = [](const Json& state) -> const Json::Array& {
            return *state.object()->at("types").array();
        };
        const auto state_areas = [](const Json& state) -> const Json::Array& {
            return *state.object()->at("areas").array();
        };
        const auto state_players = [](const Json& state) -> const Json::Array& {
            return *state.object()->at("players").array();
        };
        const auto integer_at = [](const Json::Array& values, size_t at) {
            return int(*values.at(at).integer());
        };
        const auto send_text = [&](std::string text) {
            return training_protocol.handle(cli::text_request(text, &training_protocol));
        };

        int training_draws_before = counted_rng_draws;
        auto old_draw_hook = draw_hook;
        draw_hook = count_rng_draw;
        if (*read_one.object()->at("state").object()->at("needs_initialization").boolean()) {
            const auto initialized_training = send_text("training.initialize");
            check(*initialized_training.object()->at("ok").boolean() && training_protocol.revision() == 2 &&
                  !*initialized_training.object()->at("state").object()->at("needs_initialization").boolean(),
                  "explicit training.initialize dispatches and returns initialized state");
        }
        check(counted_rng_draws == training_draws_before, "training initialization performs no hidden RNG draws");

        auto training_now = training_read();
        const auto& types = state_types(training_now.object()->at("state"));
        int type_index = -1, type_value = -1, type_total = 0;
        for (const auto& item : types) type_total += int(*item.integer());
        for (int i = 0; i < int(types.size()); ++i) {
            const int value = integer_at(types, size_t(i));
            if (value > 1) { type_index = i; type_value = value - 1; break; }
            if (value < 3 && type_total < 8) { type_index = i; type_value = value + 1; break; }
        }
        check(type_index >= 0, "training career has a valid type edit");
        const auto type_changed = send_text("training.type " + std::to_string(type_index) + " " + std::to_string(type_value));
        check(*type_changed.object()->at("ok").boolean() &&
              integer_at(state_types(type_changed.object()->at("state")), size_t(type_index)) == type_value,
              "text training.type updates only the selected type");
        check(counted_rng_draws == training_draws_before, "training.type is drawless");

        training_now = training_read();
        const auto& areas = state_areas(training_now.object()->at("state"));
        int area_index = -1, area_value = -1, fitness_total = 0, skill_total = 0;
        for (int i = 0; i < int(areas.size()); ++i) {
            const int value = integer_at(areas, size_t(i));
            if (i < 2) fitness_total += value; else skill_total += value;
        }
        for (int i = 0; i < int(areas.size()); ++i) {
            const int value = integer_at(areas, size_t(i));
            if (value > 1) { area_index = i; area_value = value - 1; break; }
            if (value < 3 && (i < 2 ? fitness_total < 4 : skill_total < 18)) {
                area_index = i; area_value = value + 1; break;
            }
        }
        check(area_index >= 0, "training career has a valid area edit");
        const auto area_changed = training_protocol.handle(request({{"cmd", "training.area"}, {"manager", 0},
            {"revision", static_cast<std::int64_t>(training_protocol.revision())}, {"index", area_index}, {"value", area_value}}));
        check(*area_changed.object()->at("ok").boolean() &&
              integer_at(state_areas(area_changed.object()->at("state")), size_t(area_index)) == area_value,
              "JSON training.area updates only the selected area");

        training_now = training_read();
        const auto& players = state_players(training_now.object()->at("state"));
        check(!players.empty(), "training projection has player rows");
        const Json& first_player = players.front();
        const int player_row = int(*first_player.object()->at("row").integer());
        const int player_id = int(*first_player.object()->at("player_id").integer());
        const int intensity_before = int(*first_player.object()->at("intensity").integer());
        const int intensity_value = intensity_before == 3 ? 2 : intensity_before + 1;
        const auto intensity_changed = send_text("training.intensity " + std::to_string(player_row) + " " +
                                                 std::to_string(intensity_value));
        check(*intensity_changed.object()->at("ok").boolean(), "text training.intensity accepted");
        training_now = training_read();
        const auto intensity_player = std::find_if(state_players(training_now.object()->at("state")).begin(),
            state_players(training_now.object()->at("state")).end(), [&](const Json& player) {
                return int(*player.object()->at("row").integer()) == player_row;
            });
        check(intensity_player != state_players(training_now.object()->at("state")).end() &&
              int(*intensity_player->object()->at("player_id").integer()) == player_id &&
              int(*intensity_player->object()->at("intensity").integer()) == intensity_value,
              "training.intensity derives the current player ID for the selected row");

        const int focus_before = int(*intensity_player->object()->at("focus").integer());
        const auto focus_changed = send_text("training.focus " + std::to_string(player_row));
        check(*focus_changed.object()->at("ok").boolean(), "text training.focus accepted");
        training_now = training_read();
        const auto focus_player = std::find_if(state_players(training_now.object()->at("state")).begin(),
            state_players(training_now.object()->at("state")).end(), [&](const Json& player) {
                return int(*player.object()->at("row").integer()) == player_row;
            });
        check(focus_player != state_players(training_now.object()->at("state")).end() &&
              int(*focus_player->object()->at("focus").integer()) == (focus_before + 1) % 8,
              "training.focus cycles the selected player's focus");

        const int current_auto = int(*training_now.object()->at("state").object()->at("automation").integer());
        const int sub_auto_value = (current_auto & 2) ? 0 : 1;
        const auto auto_changed = training_protocol.handle(request({{"cmd", "training.auto"}, {"manager", 0},
            {"revision", static_cast<std::int64_t>(training_protocol.revision())}, {"index", 1}, {"value", sub_auto_value}}));
        check(*auto_changed.object()->at("ok").boolean(), "JSON training.auto toggles a visible assistant flag");
        const auto global_off = send_text("training.auto 0 0");
        check(*global_off.object()->at("ok").boolean(), "training.auto can switch off global automation");
        const auto hidden_flag_before = training_read();
        const auto hidden_flag = send_text("training.auto 1 " + std::to_string(sub_auto_value ? 0 : 1));
        check(!*hidden_flag.object()->at("ok").boolean() && error(hidden_flag) == "training_automation_group_hidden" &&
              training_protocol.revision() == *hidden_flag_before.object()->at("revision").integer() &&
              cli::dump_json(hidden_flag_before.object()->at("state")) ==
                  cli::dump_json(training_read().object()->at("state")),
              "hidden assistant flag is rejected without changing projection");
        const auto global_on = training_protocol.handle(request({{"cmd", "training.auto"}, {"manager", 0},
            {"revision", static_cast<std::int64_t>(training_protocol.revision())}, {"index", 0}, {"value", 1}}));
        check(*global_on.object()->at("ok").boolean(), "JSON training.auto can switch automation back on");
        check(counted_rng_draws == training_draws_before, "all training dispatches are drawless");

        const auto before_stale_training = training_read();
        const auto stale_training = training_protocol.handle(request({{"cmd", "training.type"}, {"manager", 0},
            {"revision", static_cast<std::int64_t>(training_protocol.revision() - 1)}, {"index", 0}, {"value", 1}}));
        check(error(stale_training) == "stale_manager_or_revision" &&
              training_protocol.revision() == *before_stale_training.object()->at("revision").integer() &&
              cli::dump_json(before_stale_training.object()->at("state")) ==
                  cli::dump_json(training_read().object()->at("state")),
              "stale training JSON request is rejected before mutation");
        draw_hook = old_draw_hook;

        std::filesystem::remove(save_path);
        std::cout << "CLI protocol checks passed (60 same-seed kickoffs match CareerSession/season_sim path).\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "cli_protocol_tests: " << ex.what() << '\n';
        return 1;
    }
}
