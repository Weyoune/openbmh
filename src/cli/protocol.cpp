// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "cli/protocol.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

#include "app/career_session.hpp"
#include "formats/m3d.hpp"
#include "game/messages.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "ui/bank.hpp"
#include "ui/desktop.hpp"
#include "ui/league_table.hpp"
#include "ui/manager.hpp"
#include "ui/mailbox_state.hpp"
#include "ui/stadium.hpp"
#include "ui/toto.hpp"
#include "ui/training.hpp"
#include "ui/training_camp.hpp"
#include "ui/tactics.hpp"

namespace bmh::cli {
namespace {
using Object = Json::Object;
using Array = Json::Array;

const Json* find(const Json& value, std::string_view key) {
    const auto* object = value.object();
    if (!object) return nullptr;
    const auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

std::string string(const Json& value, std::string_view name) {
    const auto* v = value.string();
    if (!v) throw JsonError("field_must_be_string:" + std::string(name));
    return *v;
}

std::int64_t integer(const Json& value, std::string_view name) {
    const auto* v = value.integer();
    if (!v) throw JsonError("field_must_be_integer:" + std::string(name));
    return *v;
}

bool boolean(const Json& value, std::string_view name) {
    const auto* v = value.boolean();
    if (!v) throw JsonError("field_must_be_boolean:" + std::string(name));
    return *v;
}

const Json& required(const Json& value, std::string_view key) {
    const auto* found = find(value, key);
    if (!found) throw JsonError("missing_field:" + std::string(key));
    return *found;
}

std::int64_t optional_integer(const Json& value, std::string_view key, std::int64_t fallback) {
    const auto* found = find(value, key);
    return found ? integer(*found, key) : fallback;
}

std::string optional_string(const Json& value, std::string_view key, std::string fallback = {}) {
    const auto* found = find(value, key);
    return found ? string(*found, key) : std::move(fallback);
}

bool optional_boolean(const Json& value, std::string_view key, bool fallback) {
    const auto* found = find(value, key);
    return found ? boolean(*found, key) : fallback;
}

int bounded_int(std::int64_t value, std::string_view field, int low, int high) {
    if (value < low || value > high) throw JsonError("out_of_range:" + std::string(field));
    return static_cast<int>(value);
}

std::uint64_t bounded_u64(std::int64_t value, std::string_view field) {
    if (value < 0) throw JsonError("out_of_range:" + std::string(field));
    return static_cast<std::uint64_t>(value);
}

void check_keys(const Json& value, std::initializer_list<std::string_view> allowed) {
    const auto* object = value.object();
    if (!object) throw JsonError("request_must_be_object");
    for (const auto& [key, unused] : *object) {
        (void)unused;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            throw JsonError("unknown_field:" + key);
    }
}

void check_command_keys(const Json& value, std::string_view cmd) {
    std::vector<std::string_view> allowed;
    if (cmd == "__protocol_error") allowed = {"cmd", "detail"};
    else if (cmd == "new") allowed = {"cmd", "options", "managers"};
    else if (cmd == "load") allowed = {"cmd", "path"};
    else if (cmd == "save") allowed = {"cmd", "manager", "revision", "path", "overwrite"};
    else if (cmd == "kickoff") allowed = {"cmd", "manager", "revision"};
    else if (cmd == "table") allowed = {"cmd", "manager", "league"};
    else if (cmd == "mail") allowed = {"cmd", "manager"};
    else if (cmd == "tactics.set") allowed = {"cmd", "manager", "revision", "index", "value"};
    else if (cmd == "training.initialize") allowed = {"cmd", "manager", "revision"};
    else if (cmd == "training.type" || cmd == "training.area" || cmd == "training.auto")
        allowed = {"cmd", "manager", "revision", "index", "value"};
    else if (cmd == "training.intensity") allowed = {"cmd", "manager", "revision", "row", "value"};
    else if (cmd == "training.focus") allowed = {"cmd", "manager", "revision", "row"};
    else if (cmd == "camp.focus") allowed = {"cmd", "manager", "revision", "focus"};
    else if (cmd == "camp.book") allowed = {"cmd", "manager", "revision", "resort"};
    else if (cmd == "bank.deposit" || cmd == "bank.withdraw") allowed = {"cmd", "manager", "revision", "amount"};
    else if (cmd == "bank.shares.buy" || cmd == "bank.shares.sell" || cmd == "bank.tip")
        allowed = {"cmd", "manager", "revision", "asset", "count"};
    else if (cmd == "bank.fund.buy" || cmd == "bank.fund.sell")
        allowed = {"cmd", "manager", "revision", "asset", "count"};
    else if (cmd == "stadium.open") allowed = {"cmd", "manager", "revision"};
    else if (cmd == "stadium.offer") allowed = {"cmd", "manager", "revision", "object"};
    else if (cmd == "stadium.build" || cmd == "stadium.decline")
        allowed = {"cmd", "manager", "revision", "object", "price"};
    else if (cmd == "toto.submit") allowed = {"cmd", "manager", "revision", "stake", "tips"};
    else allowed = {"cmd"};
    const auto* object = value.object();
    for (const auto& [key, unused] : *object) {
        (void)unused;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            throw JsonError("unknown_field_for_command:" + key);
    }
}

Json raw_json(std::string value) { return parse_json(value); }

Json failed(std::string code, std::uint64_t revision, std::string detail = {}) {
    Object out{{"ok", false}, {"error", std::move(code)}, {"revision", Json(static_cast<std::int64_t>(revision))}};
    if (!detail.empty()) out.emplace("detail", std::move(detail));
    return out;
}

Json successful(std::uint64_t revision, Object extra = {}) {
    extra.emplace("ok", true);
    extra.emplace("revision", Json(static_cast<std::int64_t>(revision)));
    return extra;
}

Json state_response(const ui::DeskState& desk, std::string_view view, Json state) {
    return Object{{"view", std::string(view)}, {"state", std::move(state)}};
}

Json tactics_state(const ui::TacticsState& state) {
    Array values, depth, lane;
    for (auto value : state.values) values.emplace_back(int(value));
    for (auto value : state.depth) depth.emplace_back(int(value));
    for (auto value : state.lane) lane.emplace_back(int(value));
    return Object{{"manager_slot", state.manager_slot}, {"runtime_club", state.runtime_club},
        {"revision", static_cast<std::int64_t>(state.revision)}, {"source", state.source},
        {"club_name", ui::original_to_utf8(state.club_name)},
        {"assistant_attitude", int(state.assistant_attitude)}, {"live_scene_option", state.live_scene_option},
        {"values", std::move(values)}, {"depth", std::move(depth)}, {"lane", std::move(lane)}};
}

// The desk projection as JSON. ui::desk_json marks it "engine_attached":false (the SDL flow
// sets the flag once a career is attached); every protocol desk comes from the engine.
Json desk_state_json(const ui::DeskState& desk) {
    Json result = raw_json(ui::desk_json(desk));
    result.object()->insert_or_assign("engine_attached", true);
    return result;
}

Json training_state(const ui::TrainingState& state) {
    Json result = raw_json(ui::training_json(state));
    result.object()->insert_or_assign("engine_attached", true);
    return result;
}

Json result_state(const app::CareerSession& session) {
    const auto& results = session.completed_matchday_results();
    if (!results) return Object{{"valid", false}, {"groups", Array{}}};
    Array groups;
    for (const auto& group : results->groups) {
        Array matches;
        for (const auto& match : group.matches) {
            const char* status = "unknown";
            switch (match.status) {
            case ui::MatchResultStatus::played: status = "played"; break;
            case ui::MatchResultStatus::postponed: status = "postponed"; break;
            case ui::MatchResultStatus::not_played: status = "not_played"; break;
            case ui::MatchResultStatus::unknown: break;
            }
            Object entry{{"home", match.home}, {"away", match.away}, {"status", status},
                {"home_goals", match.home_goals ? Json(*match.home_goals) : Json(nullptr)},
                {"away_goals", match.away_goals ? Json(*match.away_goals) : Json(nullptr)}};
            // cup: "regular", "extra_time" or "penalties" (the score then includes the shoot-out)
            if (group.cup) entry["decided"] = std::string(match.decided == 2 ? "penalties" : match.decided == 1 ? "extra_time" : "regular");
            matches.emplace_back(std::move(entry));
        }
        Object g{{"competition", group.competition}, {"cup", group.cup}, {"matches", std::move(matches)}};
        if (group.league_id) g["league_id"] = *group.league_id;
        if (group.cup_id) g["cup_id"] = *group.cup_id;
        groups.emplace_back(std::move(g));
    }
    return Object{{"valid", true}, {"date", static_cast<std::int64_t>(results->date)},
        {"date_label", results->date_label}, {"calendar_entry", int(results->calendar_entry)},
        // CurrentMatchday: a random number drawn per matchday (draw_matchday_number), not the
        // matchday of a league (issue #6) - hence its own name
        {"matchday_draw", int(results->matchday)}, {"groups", std::move(groups)}};
}

Json mailbox_state_json(const ui::MailboxState& state) {
    Array messages;
    for (const auto& message : state.messages) {
        Array recipients;
        for (int manager : message.recipient_managers) recipients.emplace_back(manager);
        messages.emplace_back(Object{{"slot_snapshot_id", int(message.entry_index)},
            {"recipient_mask", static_cast<std::int64_t>(message.recipient_mask)},
            {"keep_mask", static_cast<std::int64_t>(message.kept_by_mask)},
            {"recipient_managers", std::move(recipients)},
            {"general_broadcast", message.general_broadcast},
            {"kept_for_manager", message.kept_for_manager},
            {"date_counter", int(message.date_counter)}, {"kind", "unclassified"}, {"text", message.text}});
    }
    return Object{{"manager_slot", state.manager_slot}, {"messages", std::move(messages)}};
}

Json league_table_state(const app::CareerSession& session, int manager, int league) {
    const auto table = bmh::compute_league_table(session.state(), league, 0,
        bmh::league_matchdays(league), 7, 1);
    std::array<int, 20> order{};
    for (int club = 0; club < table.size; ++club) order[club] = club;
    std::sort(order.begin(), order.begin() + table.size, [&](int lhs, int rhs) {
        return table.rank(lhs) < table.rank(rhs);
    });
    Array rows;
    const int first_team = bmh::league_first_team(league);
    for (int index = 0; index < table.size; ++index) {
        const int club = order[index];
        const int runtime_team = first_team + club;
        const int games = table.games(club);
        const int draws = games - table.wins[club] - table.defeats[club];
        rows.emplace_back(Object{{"rank", table.rank(club) + 1},
            {"club_identity_key", session.state().club_ids[runtime_team]}, {"runtime_team_id", runtime_team},
            {"name", ui::original_to_utf8(session.state().club_name(runtime_team))},
            {"games", games}, {"wins", table.wins[club]}, {"draws", draws},
            {"losses", table.defeats[club]}, {"goals_for", table.goals_for[club]},
            {"goals_against", table.goals_against[club]},
            {"goal_difference", int(table.goals_for[club]) - int(table.goals_against[club])},
            {"points", int(table.points[club])}, {"previous_rank", table.previous_rank(club) + 1}});
    }
    return Object{{"manager_slot", manager}, {"league_id", league},
        // the original league name path of the table screen (issue #5: not TEXTD Prediction_TablePrediction_TextPointer + 4n,
        // which starts one entry early with a referee text)
        {"league_name", ui::league_table_from_game(session.state(), manager, league).league_name},
        {"points_mode", 1},
        {"rows", std::move(rows)}};
}

void put_envelope(const Json& request, int& manager, std::uint64_t& revision) {
    manager = bounded_int(integer(required(request, "manager"), "manager"), "manager", 0, 3);
    revision = bounded_u64(integer(required(request, "revision"), "revision"), "revision");
}

std::string command_name(const Json& request) {
    return string(required(request, "cmd"), "cmd");
}

ui::DeskState current_desk(app::CareerSession& session, std::uint64_t revision) {
    return session.desktop_projection(revision);
}

Object completed_status(const app::CareerSession& session, std::uint64_t revision) {
    const auto& state = session.state();
    return {{"phase", "career_complete"}, {"active", false},
        {"revision", static_cast<std::int64_t>(revision)},
        {"date", static_cast<std::int64_t>(state.date)}, {"season", state.season},
        {"duration", int(state.memory.u8(::bmh::layout::CareerDuration))}};
}

Json action_state(app::CareerSession& session, std::uint64_t revision,
                  std::string_view family, int manager) {
    const auto prep = session.preparation_projection(revision);
    if (family == "bank" && prep.bank) return state_response(prep.desktop, family, raw_json(ui::bank_state_json(*prep.bank)));
    if (family == "camp" && prep.training_camp) return state_response(prep.desktop, family, raw_json(ui::training_camp_json(*prep.training_camp)));
    if (family == "stadium" && prep.stadium) return state_response(prep.desktop, family, raw_json(ui::stadium_state_json(*prep.stadium)));
    if (family == "toto" && prep.toto) return state_response(prep.desktop, family, raw_json(ui::toto_state_json(*prep.toto)));
    (void)manager;
    return state_response(prep.desktop, "desk", desk_state_json(prep.desktop));
}

ui::SetupOptions parse_options(const Json& value) {
    check_keys(value, {"duration", "difficulty", "managers", "mode", "league", "world"});
    ui::SetupOptions options;
    options.duration = bounded_int(optional_integer(value, "duration", options.duration), "duration", 0, 5);
    options.difficulty = bounded_int(optional_integer(value, "difficulty", options.difficulty), "difficulty", 0, 9);
    options.managers = bounded_int(optional_integer(value, "managers", 1), "managers", 1, 1);
    options.mode = bounded_int(optional_integer(value, "mode", options.mode), "mode", 0, 3);
    options.league = optional_boolean(value, "league", options.league);
    options.world = optional_boolean(value, "world", options.world);
    return options;
}

ui::ManagerChoice parse_manager(const Json& value) {
    check_keys(value, {"name", "master_club", "start_league", "strength", "portrait"});
    ui::ManagerChoice choice;
    choice.slot = 0;
    choice.name = ui::utf8_to_original(string(required(value, "name"), "name"));
    if (choice.name.empty() || choice.name.size() > 0x19) throw JsonError("invalid_manager_name_length");
    choice.master_club = bounded_int(integer(required(value, "master_club"), "master_club"), "master_club", 0, 4095);
    choice.start_league = bounded_int(optional_integer(value, "start_league", 0), "start_league", 0, 4);
    choice.strength = bounded_int(optional_integer(value, "strength", 1), "strength", 0, 2);
    choice.portrait = bounded_int(optional_integer(value, "portrait", 0), "portrait", 0, 11);
    return choice;
}

} // namespace

Protocol::Protocol(std::filesystem::path data, std::uint32_t seed)
    : data_(std::move(data)), seed_(seed) {}

Protocol::~Protocol() = default;

bool Protocol::active() const noexcept { return session_ && !session_->career_complete(); }

Json Protocol::help() {
    return Object{
        {"protocol", 1},
        {"commands", Array{
            Object{{"cmd", "new"}, {"params", "options:{duration,difficulty,mode,league,world,managers:1}; managers:[{name,master_club,start_league?,strength?,portrait?}]"}},
            Object{{"cmd", "load"}, {"params", "path"}},
            Object{{"cmd", "save"}, {"params", "manager,revision,path,overwrite?"}},
            Object{{"cmd", "status"}, {"params", "none"}},
            Object{{"cmd", "desk"}, {"params", "none"}},
            Object{{"cmd", "kickoff"}, {"params", "manager,revision"}},
            Object{{"cmd", "results|tactics"}, {"params", "none"}},
            Object{{"cmd", "mail"}, {"params", "manager:0..3 (required; read-only owned mailbox projection)"}},
            Object{{"cmd", "table"}, {"params", "manager,league:0..8 (read-only; current league text and game point rule)"}},
            Object{{"cmd", "tactics.set"}, {"params", "manager,revision,index:0..7,value (original table range)"}},
            Object{{"cmd", "training"}, {"params", "none (read-only current training projection)"}},
            Object{{"cmd", "training.initialize"}, {"params", "manager,revision"}},
            Object{{"cmd", "training.type"}, {"params", "manager,revision,index:0..3,value:1..3"}},
            Object{{"cmd", "training.area"}, {"params", "manager,revision,index:0..8,value:1..3"}},
            Object{{"cmd", "training.intensity"}, {"params", "manager,revision,row,value:1..3"}},
            Object{{"cmd", "training.focus"}, {"params", "manager,revision,row (cycle focus)"}},
            Object{{"cmd", "training.auto"}, {"params", "manager,revision,index:0..4,value:0|1"}},
            Object{{"cmd", "camp.focus"}, {"params", "manager,revision,focus:[4 values]"}},
            Object{{"cmd", "camp.book"}, {"params", "manager,revision,resort:0..7"}},
            Object{{"cmd", "bank.deposit|bank.withdraw"}, {"params", "manager,revision,amount"}},
            Object{{"cmd", "bank.shares.buy|bank.shares.sell|bank.tip"}, {"params", "manager,revision,asset,count?"}},
            Object{{"cmd", "bank.fund.buy|bank.fund.sell"}, {"params", "manager,revision,asset,count"}},
            Object{{"cmd", "stadium.open|stadium.offer|stadium.build"}, {"params", "manager,revision,object?,price?"}},
            Object{{"cmd", "toto.submit"}, {"params", "manager,revision,stake,tips:[11 values]"}},
            Object{{"cmd", "quit"}, {"params", "none"}},
        }},
        {"not_available", Array{"squad", "player", "calendar", "finances", "lineup.auto", "match.start", "match.step", "match.sub", "match.tactics", "match.finish"}},
        {"note", "Unavailable commands are rejected: CareerSession has no matching projection or dispatch API yet."}
    };
}

Json Protocol::handle(const Json& request, bool replay) {
    try {
        check_keys(request, {"cmd", "detail", "manager", "revision", "league", "path", "overwrite", "options", "managers", "amount", "asset", "count", "focus", "resort", "object", "price", "stake", "tips", "index", "value", "row"});
        const std::string cmd = command_name(request);
        check_command_keys(request, cmd);
        if (cmd == "__protocol_error")
            return failed("invalid_request", revision_, optional_string(request, "detail", "invalid protocol input"));
        if (cmd == "help") return successful(revision_, {{"help", help()}});
        if (cmd == "quit") return successful(revision_, {{"quit", true}});
        if (cmd == "new") {
            if (active()) return failed("career_already_active", revision_);
            const auto options_value = find(request, "options") ? *find(request, "options") : Json(Object{});
            const auto options = parse_options(options_value);
            const Json& managers_value = required(request, "managers");
            const auto* managers = managers_value.array();
            if (!managers || managers->size() != 1) return failed("stage1_requires_one_manager", revision_);
            const auto choice = parse_manager(managers->front());
            const auto database = load_database(data_ / "M3D.MAN");
            if (choice.master_club >= int(database.club_names.size()))
                return failed("invalid_master_club", revision_);
            auto candidate = std::make_unique<app::CareerSession>(data_, seed_);
            std::string error;
            if (!candidate->confirm_manager(options, choice, error)) return failed(error.empty() ? "manager_rejected" : error, revision_);
            ui::CareerInitializationResult init;
            if (!candidate->finish(init, error)) return failed(error.empty() ? "career_start_failed" : error, revision_);
            revision_ = init.desktop.revision;
            session_ = std::move(candidate);
            return successful(revision_, {{"events", Array{Json(Object{{"event", "career_started"}, {"manager", 0}})}},
                {"state", desk_state_json(init.desktop)}});
        }
        if (cmd == "load") {
            const std::filesystem::path path(string(required(request, "path"), "path"));
            auto candidate = std::make_unique<app::CareerSession>(data_, seed_, path);
            const std::uint64_t next_revision = revision_ + 1;
            auto desk = candidate->desktop_projection(next_revision);
            session_ = std::move(candidate);
            revision_ = next_revision;
            return successful(revision_, {{"events", Array{Json(Object{{"event", "career_loaded"}})}},
                {"state", desk_state_json(desk)}});
        }
        // Source's terminal career has no following desk. Keep its owned state
        // available for status; reject career commands before any projection,
        // envelope parsing, file write or dispatch can occur.
        if (session_ && session_->career_complete()) {
            if (cmd == "status")
                return successful(revision_, {{"status", completed_status(*session_, revision_)}});
            return failed("career_complete", revision_);
        }
        if (cmd == "status" || cmd == "desk") {
            if (!session_) return failed("no_active_career", revision_);
            const auto desk = current_desk(*session_, revision_);
            Object status{{"manager", desk.manager_slot}, {"revision", static_cast<std::int64_t>(revision_)},
                {"day", desk.date.day}, {"month", desk.date.month}, {"year", desk.date.year},
                {"club", ui::original_to_utf8(desk.club_name)}, {"balance", desk.balance}, {"runtime_club", desk.runtime_club},
                {"source", desk.source}};
            return successful(revision_, {{"state", desk_state_json(desk)}, {"status", Json(std::move(status))}});
        }
        if (cmd == "results" || cmd == "tactics") {
            if (!session_) return failed("no_active_career", revision_);
            return successful(revision_, {{"state", cmd == "results" ? result_state(*session_) :
                tactics_state(session_->preparation_projection(revision_).tactics)}});
        }
        if (cmd == "mail") {
            if (!session_) return failed("no_active_career", revision_);
            const int manager = bounded_int(integer(required(request, "manager"), "manager"), "manager", 0, 3);
            if (size_t(manager) >= session_->state().manager_teams.size())
                return failed("invalid_manager_slot", revision_);
            const auto state = ui::mailbox_state_from_game(session_->state(), manager);
            return successful(revision_, {{"state", mailbox_state_json(state)}});
        }
        if (cmd == "table") {
            if (!session_) return failed("no_active_career", revision_);
            const int manager = bounded_int(integer(required(request, "manager"), "manager"), "manager", 0, 3);
            const auto raw_league = integer(required(request, "league"), "league");
            if (raw_league < 0 || raw_league >= bmh::kLeagueCount)
                return failed("invalid_league", revision_);
            const auto desk = current_desk(*session_, revision_);
            if (manager != desk.manager_slot) return failed("stale_manager_or_revision", revision_);
            return successful(revision_, {{"state", league_table_state(*session_, manager, int(raw_league))}});
        }
        if (cmd == "training") {
            if (!session_) return failed("no_active_career", revision_);
            const auto prep = session_->preparation_projection(revision_);
            if (!prep.training) return failed("training_projection_unavailable", revision_);
            return successful(revision_, {{"state", training_state(*prep.training)}});
        }
        if (cmd == "training.initialize" || cmd == "training.type" || cmd == "training.area" ||
            cmd == "training.intensity" || cmd == "training.focus" || cmd == "training.auto") {
            if (!session_) return failed("no_active_career", revision_);
            int manager = -1;
            std::uint64_t request_revision = 0;
            put_envelope(request, manager, request_revision);
            const auto prep = session_->preparation_projection(revision_);
            if (!prep.training) return failed("training_projection_unavailable", revision_);
            const auto& current = *prep.training;
            if (request_revision != revision_ || manager != current.manager_slot)
                return failed("stale_manager_or_revision", revision_);

            ui::TrainingRequest action;
            action.manager_slot = current.manager_slot;
            action.runtime_club = current.runtime_club;
            action.revision = request_revision;
            action.desired_types = current.types;
            action.desired_areas = current.areas;
            action.desired_automation = current.automation;
            if (cmd == "training.initialize") {
                action.kind = ui::TrainingRequestKind::initialize;
                action.desired_automation = 0x9f;
            } else if (cmd == "training.type") {
                action.kind = ui::TrainingRequestKind::set_type;
                action.index = bounded_int(integer(required(request, "index"), "index"), "index", 0, 3);
                action.value = bounded_int(integer(required(request, "value"), "value"), "value", 1, 3);
                action.desired_types[size_t(action.index)] = uint8_t(action.value);
                action.desired_automation &= uint8_t(~2);
            } else if (cmd == "training.area") {
                action.kind = ui::TrainingRequestKind::set_area;
                action.index = bounded_int(integer(required(request, "index"), "index"), "index", 0, 8);
                action.value = bounded_int(integer(required(request, "value"), "value"), "value", 1, 3);
                action.desired_areas[size_t(action.index)] = uint8_t(action.value);
                action.desired_automation &= uint8_t(~4);
            } else if (cmd == "training.intensity" || cmd == "training.focus") {
                action.kind = cmd == "training.intensity" ? ui::TrainingRequestKind::set_player_intensity :
                    ui::TrainingRequestKind::set_player_focus;
                action.index = bounded_int(integer(required(request, "row"), "row"), "row", 0, current.squad_capacity - 1);
                const auto player = std::find_if(current.players.begin(), current.players.end(), [&](const auto& p) {
                    return p.row == action.index;
                });
                if (player == current.players.end()) return failed("training_row_unavailable", revision_);
                action.player_id = player->player_id;
                if (cmd == "training.intensity") {
                    action.value = bounded_int(integer(required(request, "value"), "value"), "value", 1, 3);
                    action.desired_automation &= uint8_t(~8);
                } else action.value = (int(player->focus) + 1) % 8;
            } else {
                action.kind = ui::TrainingRequestKind::set_automation;
                action.index = bounded_int(integer(required(request, "index"), "index"), "index", 0, 4);
                action.value = bounded_int(integer(required(request, "value"), "value"), "value", 0, 1);
                const uint8_t mask = uint8_t(1u << action.index);
                action.desired_automation = uint8_t((current.automation & ~mask) |
                    (action.value ? mask : 0));
            }
            std::string error;
            if (!session_->dispatch_training(action, revision_, error))
                return failed(error.empty() ? "action_rejected" : error, revision_, error);
            const auto updated = session_->preparation_projection(revision_).training;
            if (!updated) return failed("training_projection_unavailable", revision_);
            return successful(revision_, {{"state", training_state(*updated)},
                {"events", Array{Json(Object{{"event", "action_applied"}, {"command", cmd}})}}});
        }
        if (cmd == "tactics.set") {
            if (!session_) return failed("no_active_career", revision_);
            ui::TacticsRequest action;
            put_envelope(request, action.manager_slot, action.revision);
            action.runtime_club = current_desk(*session_, revision_).runtime_club;
            action.kind = ui::TacticsRequestKind::set_tactic;
            action.index = bounded_int(integer(required(request, "index"), "index"), "index", 0, 7);
            action.desired_value = bounded_int(integer(required(request, "value"), "value"), "value", 0, 11);
            if (action.index == 0) action.desired_assistant_attitude = 0;
            std::string error;
            if (!session_->dispatch_tactics(action, revision_, error))
                return failed("action_rejected", revision_, error);
            return successful(revision_, {{"state", tactics_state(session_->preparation_projection(revision_).tactics)},
                {"events", Array{Json(Object{{"event", "action_applied"}, {"command", cmd}})}}});
        }
        if (cmd == "save") {
            if (!session_) return failed("no_active_career", revision_);
            int manager; std::uint64_t request_revision; put_envelope(request, manager, request_revision);
            const auto desk = current_desk(*session_, revision_);
            if (request_revision != revision_ || manager != desk.manager_slot)
                return failed("stale_manager_or_revision", revision_);
            const bool overwrite = replay || optional_boolean(request, "overwrite", false);
            std::string error;
            if (!session_->save_game(string(required(request, "path"), "path"), manager, overwrite, error))
                return failed(error.empty() ? "save_rejected" : error, revision_);
            return successful(revision_, {{"events", Array{Json(Object{{"event", "career_saved"}})}}});
        }
        if (cmd == "kickoff") {
            if (!session_) return failed("no_active_career", revision_);
            int manager; std::uint64_t request_revision; put_envelope(request, manager, request_revision);
            ui::DeskRequest req; req.kind = ui::DeskRequestKind::advance_turn; req.manager_slot = manager; req.revision = request_revision;
            std::string error;
            const auto advanced = session_->dispatch_kickoff(req, revision_, error);
            if (advanced.kind == TurnDeskKind::Failed || advanced.kind == TurnDeskKind::WaitingAtDesk)
                return failed(error.empty() ? advanced.reason : error, revision_);
            if (advanced.kind == TurnDeskKind::CareerComplete) {
                return successful(revision_, {{"events", Array{Json(Object{
                    {"event", "career_complete"}, {"date", static_cast<std::int64_t>(advanced.date)},
                    {"season", session_->state().season}, {"duration", int(session_->state().memory.u8(::bmh::layout::CareerDuration))}})}},
                    {"status", completed_status(*session_, revision_)}});
            }
            Array events;
            for (const auto& event : advanced.audio_events) {
                std::string kind = event.kind == TurnAudioKind::kickoff_whistle ? "kickoff_whistle" :
                    event.kind == TurnAudioKind::goal ? "goal" : "full_time_whistle";
                Object item{{"event", "audio"}, {"session_id", static_cast<std::int64_t>(event.session_id)},
                    {"event_id", static_cast<std::int64_t>(event.event_id)}, {"kind", std::move(kind)}};
                if (event.original_id) item.emplace("original_id", int(*event.original_id));
                events.emplace_back(std::move(item));
            }
            events.emplace_back(Object{{"event", "desk"}, {"manager", advanced.manager_slot},
                {"date", static_cast<std::int64_t>(advanced.date)},
                {"kind", advanced.kind == TurnDeskKind::SeasonEndDesk ? "season_end" : "next_desk"}});
            const auto desk = current_desk(*session_, revision_);
            return successful(revision_, {{"events", Json(std::move(events))}, {"state", desk_state_json(desk)},
                {"results", result_state(*session_)}});
        }

        if (cmd.rfind("camp.", 0) == 0 || cmd.rfind("bank.", 0) == 0 ||
            cmd.rfind("stadium.", 0) == 0 || cmd == "toto.submit") {
            if (!session_) return failed("no_active_career", revision_);
            int manager; std::uint64_t request_revision; put_envelope(request, manager, request_revision);
            const auto desk = current_desk(*session_, revision_);
            const int runtime_club = desk.runtime_club;
            std::string error;
            bool accepted = false;
            std::string family;
            if (cmd == "camp.focus" || cmd == "camp.book") {
                family = "camp";
                ui::TrainingCampRequest req; req.manager_slot = manager; req.runtime_club = runtime_club; req.revision = request_revision;
                const auto prep = session_->preparation_projection(revision_);
                if (!prep.training_camp) return failed("training_camp_projection_unavailable", revision_);
                req.focus = prep.training_camp->focus;
                if (cmd == "camp.focus") {
                    req.kind = ui::TrainingCampRequestKind::set_focus;
                    const Json& value = required(request, "focus"); const auto* values = value.array();
                    if (!values || values->size() != 4) return failed("invalid_focus", revision_);
                    for (size_t i = 0; i < 4; ++i) req.focus[i] = static_cast<uint8_t>(bounded_int(integer(values->at(i), "focus"), "focus", 0, 255));
                } else {
                    req.kind = ui::TrainingCampRequestKind::book;
                    req.resort = bounded_int(integer(required(request, "resort"), "resort"), "resort", 0, 7);
                }
                accepted = session_->dispatch_training_camp(req, revision_, error);
            } else if (cmd.rfind("bank.", 0) == 0) {
                family = "bank";
                ui::BankRequest req; req.manager_slot = manager; req.runtime_club = runtime_club; req.revision = request_revision;
                if (cmd == "bank.deposit" || cmd == "bank.withdraw") {
                    req.kind = ui::BankRequestKind::move_to_deposit;
                    const auto amount = integer(required(request, "amount"), "amount");
                    if (amount <= 0 || amount > std::numeric_limits<std::int32_t>::max()) return failed("invalid_amount", revision_);
                    req.amount = static_cast<std::int32_t>(cmd == "bank.deposit" ? amount : -amount);
                } else if (cmd == "bank.shares.buy" || cmd == "bank.shares.sell" || cmd == "bank.tip") {
                    req.kind = cmd == "bank.shares.buy" ? ui::BankRequestKind::buy_shares :
                        cmd == "bank.shares.sell" ? ui::BankRequestKind::sell_shares : ui::BankRequestKind::stock_tip;
                    req.asset_index = bounded_int(integer(required(request, "asset"), "asset"), "asset", 0, 11);
                    req.count = bounded_int(optional_integer(request, "count", 0), "count", 0, 9999);
                } else if (cmd == "bank.fund.buy" || cmd == "bank.fund.sell") {
                    req.kind = cmd == "bank.fund.buy" ? ui::BankRequestKind::buy_fund : ui::BankRequestKind::sell_fund;
                    req.asset_index = bounded_int(integer(required(request, "asset"), "asset"), "asset", 0, 5);
                    req.count = bounded_int(integer(required(request, "count"), "count"), "count", 1, 10);
                } else return failed("unsupported_command", revision_);
                accepted = session_->dispatch_bank(req, revision_, error);
            } else if (cmd.rfind("stadium.", 0) == 0) {
                family = "stadium";
                ui::StadiumRequest req; req.manager_slot = manager; req.runtime_club = runtime_club; req.revision = request_revision;
                if (cmd == "stadium.open") req.kind = ui::StadiumRequestKind::open;
                else if (cmd == "stadium.offer") { req.kind = ui::StadiumRequestKind::quote_periphery; req.object = bounded_int(integer(required(request, "object"), "object"), "object", 0, 13); }
                else if (cmd == "stadium.build") { req.kind = ui::StadiumRequestKind::build_periphery; req.object = bounded_int(integer(required(request, "object"), "object"), "object", 0, 13); req.price = bounded_int(integer(required(request, "price"), "price"), "price", 1, std::numeric_limits<int>::max()); }
                else if (cmd == "stadium.decline") { req.kind = ui::StadiumRequestKind::decline_periphery; req.object = bounded_int(integer(required(request, "object"), "object"), "object", 0, 13); req.price = bounded_int(integer(required(request, "price"), "price"), "price", 1, std::numeric_limits<int>::max()); }
                else return failed("unsupported_command", revision_);
                accepted = session_->dispatch_stadium(req, revision_, error);
            } else if (cmd == "toto.submit") {
                family = "toto";
                ui::TotoRequest req; req.manager_slot = manager; req.runtime_club = runtime_club; req.revision = request_revision;
                req.stake = bounded_int(integer(required(request, "stake"), "stake"), "stake", 0, std::numeric_limits<int>::max());
                const Json& value = required(request, "tips"); const auto* tips = value.array();
                if (!tips || tips->size() != req.tips.size()) return failed("invalid_tips", revision_);
                for (size_t i = 0; i < req.tips.size(); ++i) req.tips[i] = static_cast<uint8_t>(bounded_int(integer(tips->at(i), "tips"), "tips", 0, 2));
                req.kind = ui::TotoRequestKind::submit;
                accepted = session_->dispatch_toto(req, revision_, error);
            }
            if (!accepted) return failed(error.empty() ? "action_rejected" : error, revision_);
            return successful(revision_, {{"events", Array{Json(Object{{"event", "action_applied"}, {"command", cmd}})}},
                {"state", action_state(*session_, revision_, family, manager).object()->at("state")}});
        }
        if (cmd == "squad" || cmd == "player" || cmd == "calendar" || cmd == "finances" ||
            cmd == "lineup.auto" || cmd.rfind("match.", 0) == 0)
            return failed("career_session_api_unavailable", revision_, "This command has no CareerSession projection/dispatch adapter in E10 stage 1.");
        return failed("unsupported_command", revision_);
    } catch (const JsonError& e) {
        return failed("invalid_request", revision_, e.what());
    } catch (const std::exception& e) {
        return failed("operation_failed", revision_, e.what());
    }
}

Json text_request(std::string_view line, const Protocol* protocol) {
    std::istringstream input{std::string(line)};
    std::string cmd;
    input >> cmd;
    if (cmd.empty()) throw JsonError("empty_command");
    Object req{{"cmd", cmd}};
    std::string path, name;
    auto envelope = [&] {
        if (!protocol || !protocol->active()) throw JsonError("no_active_career");
        req.emplace("manager", 0);
        req.emplace("revision", static_cast<std::int64_t>(protocol->revision()));
    };
    if (cmd == "new") {
        int master = -1, strength = 1;
        if (!(input >> master >> std::quoted(name))) throw JsonError("usage:new <master_club> <manager_name> [strength]");
        if (input >> strength) { /* optional */ }
        req.emplace("options", Json(Object{{"managers", 1}}));
        req.emplace("managers", Json(Array{Json(Object{{"master_club", master}, {"name", name}, {"strength", strength}})}));
    } else if (cmd == "load" || cmd == "save") {
        if (!(input >> std::quoted(path))) throw JsonError("usage:" + cmd + " <path>");
        req.emplace("path", path);
        if (cmd == "save") envelope();
    } else if (cmd == "kickoff") envelope();
    else if (cmd == "tactics.set") {
        int index = -1, value = -1;
        if (!(input >> index >> value)) throw JsonError("usage:tactics.set <index> <value>");
        req.emplace("index", index); req.emplace("value", value); envelope();
    }
    else if (cmd == "training") {
    } else if (cmd == "training.initialize") envelope();
    else if (cmd == "training.type" || cmd == "training.area" || cmd == "training.auto") {
        int index = -1, value = -1;
        if (!(input >> index >> value)) throw JsonError("usage:" + cmd + " <index> <value>");
        req.emplace("index", index); req.emplace("value", value); envelope();
    } else if (cmd == "training.intensity") {
        int row = -1, value = -1;
        if (!(input >> row >> value)) throw JsonError("usage:training.intensity <row> <value>");
        req.emplace("row", row); req.emplace("value", value); envelope();
    } else if (cmd == "training.focus") {
        int row = -1;
        if (!(input >> row)) throw JsonError("usage:training.focus <row>");
        req.emplace("row", row); envelope();
    } else if (cmd == "table") {
        int manager = -1, league = -1;
        if (!(input >> manager >> league)) throw JsonError("usage:table <manager> <league>");
        if (!protocol || !protocol->active()) throw JsonError("no_active_career");
        req.emplace("manager", manager);
        req.emplace("league", league);
    } else if (cmd == "mail") {
        int manager = -1;
        if (!(input >> manager)) throw JsonError("usage:mail <manager:0..3>");
        if (!protocol || !protocol->active()) throw JsonError("no_active_career");
        req.emplace("manager", manager);
    }
    else if (cmd == "bank.deposit" || cmd == "bank.withdraw") {
        std::int64_t amount;
        if (!(input >> amount)) throw JsonError("usage:" + cmd + " <amount>");
        req.emplace("amount", amount); envelope();
    } else if (cmd == "bank.shares.buy" || cmd == "bank.shares.sell" || cmd == "bank.tip") {
        int asset = -1, count = 0;
        if (!(input >> asset)) throw JsonError("usage:" + cmd + " <asset> [count]");
        if (cmd != "bank.tip" && !(input >> count)) throw JsonError("usage:" + cmd + " <asset> <count>");
        req.emplace("asset", asset); req.emplace("count", count); envelope();
    } else if (cmd == "bank.fund.buy" || cmd == "bank.fund.sell") {
        int asset = -1, count = 0;
        if (!(input >> asset >> count)) throw JsonError("usage:" + cmd + " <asset> <count>");
        req.emplace("asset", asset); req.emplace("count", count); envelope();
    } else if (cmd == "camp.book") {
        int resort = -1; if (!(input >> resort)) throw JsonError("usage:camp.book <resort>");
        req.emplace("resort", resort); envelope();
    } else if (cmd == "help" || cmd == "status" || cmd == "desk" || cmd == "quit" || cmd == "tactics" || cmd == "results") {
    } else throw JsonError("unsupported_text_command:" + cmd);
    std::string extra;
    if (input >> extra) throw JsonError("too_many_arguments");
    return req;
}

std::string text_response(const Json& response) {
    const auto* ok = find(response, "ok");
    const auto* ok_value = ok ? ok->boolean() : nullptr;
    if (ok_value && !*ok_value) {
        std::string out = "ERROR " + optional_string(response, "error", "unknown_error");
        if (const auto* detail = find(response, "detail"); detail && detail->string()) out += ": " + *detail->string();
        return out;
    }
    std::string out = "OK revision " + std::to_string(optional_integer(response, "revision", 0));
    if (const auto* status = find(response, "status"); status && status->object()) {
        if (optional_string(*status, "phase", "") == "career_complete")
            return out + " | career complete | date " + std::to_string(optional_integer(*status, "date", 0));
        out += " | " + optional_string(*status, "club", "club ?");
        out += " | date " + std::to_string(optional_integer(*status, "day", 0)) + "." +
            std::to_string(optional_integer(*status, "month", 0)) + "." + std::to_string(optional_integer(*status, "year", 0));
        out += " | balance " + std::to_string(optional_integer(*status, "balance", 0));
    } else if (const auto* state = find(response, "state")) out += " | " + dump_json(*state);
    return out;
}

} // namespace bmh::cli
