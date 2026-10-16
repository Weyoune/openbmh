// SPDX-License-Identifier: GPL-3.0-only
// The club windows of docs/CLI_CONCEPT/06-verein.md: Tactics (menu 18), Training (menu 16)
// and TrainingCamp (menu 15). The views come from CareerSession::preparation_projection, the
// changes go through dispatch_tactics / dispatch_training / dispatch_training_camp with the
// same requests the SDL screens build (src/ui/tactics.cpp, training.cpp, training_camp.cpp).

#include <algorithm>

#include "cli/console/console.hpp"
#include "cli/console/detail.hpp"

namespace bmh::cli::console {

using namespace detail;

namespace {

// 06-verein 2: the value range of the eight tactics settings (H2 p. 71)
constexpr int kTacticMax[8] = {3, 3, 3, 3, 1, 1, 1, 11};

const std::vector<std::string> kTypes = {"TOR", "ABW", "MIT", "STU"};
const std::vector<std::string> kAreas = {"Fitness", "Skill", "Corner", "FreeKick", "Tackling", "Pass", "Shot", "Speed", "Game"};
constexpr int kPositionNames = 629;  // positionGoalkeeperCaps "TOR", ABW, MIT, STU (TrainingPlayer::position 0-3)

// TEXTD indices (docs/formats/textd_inventory.csv)
constexpr int kCampQualityTitles = 362;   // campPitchQuality .. campAccommodation (362-365)
constexpr int kCampResorts = 371;         // campResortLodz .. (371-378), names differ per edition
constexpr int kCampPlayersReturnHappy = 1259;
constexpr int kCampRecentlyVisited = 1265;
constexpr int kCampBoardDeniesFurtherVisits = 1266;

std::string balls(int n) { return std::string(size_t(std::max(0, n)), 'o'); }

}  // namespace

void Console::register_club_commands() {
    // --- Tactics (06-verein 2) ------------------------------------------------------------
    auto show_tactics = [this] {
        const ui::TacticsState t = prep().tactics;
        std::vector<std::vector<std::string>> rows;
        for (size_t i = 0; i < 8; ++i)
            rows.push_back({std::to_string(i), detail::kTacticNames[i], std::to_string(t.values[i]), std::to_string(kTacticMax[i])});
        emit_table({"Item", "Setting", "Value", "Max"}, rows);
        emit('i', std::string("assistant attitude control: ") + on_off(t.assistant_attitude != 0));
    };
    openers_[18] = [this, show_tactics] {
        open_window("Tactics");
        show_tactics();
    };
    add({"Tactics", "ShowTactics", {}, "the eight settings with value and maximum", false}, [show_tactics](const Values&) { show_tactics(); });
    auto set_tactic = [this, show_tactics](int item, int value) {
        const ui::TacticsState t = prep().tactics;
        if (value < 0 || value > kTacticMax[item])
            fail("E_BAD_ARGUMENT", "item " + std::to_string(item) + " takes 0.." + std::to_string(kTacticMax[item]));
        if (t.values[size_t(item)] == value) {
            emit('+', "item " + std::to_string(item) + " already " + std::to_string(value));
            return;
        }
        ui::TacticsRequest req;
        req.kind = ui::TacticsRequestKind::set_tactic;
        req.manager_slot = t.manager_slot;
        req.runtime_club = t.runtime_club;
        req.revision = revision_;
        req.index = item;
        req.desired_value = value;
        // the offensive/defensive arrows switch the assistant's attitude control off
        if (item == 0) req.desired_assistant_attitude = 0;
        std::string error;
        require_engine(session_->dispatch_tactics(req, revision_, error), "tactics", error);
        emit('+', "item " + std::to_string(item) + " = " + std::to_string(value));
    };
    add({"Tactics", "SetTactic", {integer("item", 0, 7), integer("value", 0, 11)}, "set a value directly (item 0 ends the assistant's D/O control)", true},
        [set_tactic](const Values& v) { set_tactic(int(v.integer("item")), int(v.integer("value"))); });
    add({"Tactics", "CycleTactic", {integer("item", 0, 6)}, "a click: value + 1, after the maximum 0", true}, [this, set_tactic](const Values& v) {
        const int item = int(v.integer("item"));
        const int value = prep().tactics.values[size_t(item)];
        set_tactic(item, value >= kTacticMax[item] ? 0 : value + 1);
    });

    // --- Training (06-verein 3) -----------------------------------------------------------
    auto training = [this] {
        const auto p = prep();
        if (!p.training) fail("E_MODE_LOCKED", "there is no training in mode I");
        return *p.training;
    };
    auto training_request = [this](const ui::TrainingState& t, ui::TrainingRequestKind kind) {
        ui::TrainingRequest r;
        r.kind = kind;
        r.manager_slot = t.manager_slot;
        r.runtime_club = t.runtime_club;
        r.revision = revision_;
        r.desired_types = t.types;
        r.desired_areas = t.areas;
        r.desired_automation = t.automation;
        return r;
    };
    auto dispatch_training = [this](const ui::TrainingRequest& r) {
        std::string error;
        require_engine(session_->dispatch_training(r, revision_, error), "training", error);
    };
    auto players_of = [](const ui::TrainingState& t) {
        std::vector<PlayerRef> refs;
        for (const auto& p : t.players) refs.push_back({p.row, p.player_id, utf8(p.name)});
        return refs;
    };
    auto show_training = [this, training] {
        const ui::TrainingState t = training();
        int type_sum = 0, fitness = 0, other = 0;
        std::vector<std::vector<std::string>> types, areas;
        for (size_t i = 0; i < 4; ++i) {
            type_sum += t.types[i];
            types.push_back({kTypes[i], std::to_string(t.types[i]), balls(t.types[i])});
        }
        for (size_t i = 0; i < 9; ++i) {
            (i < 2 ? fitness : other) += t.areas[i];
            areas.push_back({kAreas[i], std::to_string(t.areas[i]), balls(t.areas[i])});
        }
        emit_table({"Type", "Balls", ""}, types);
        emit_table({"Area", "Balls", ""}, areas);
        emit('i', "balls: type=" + std::to_string(type_sum) + "/8 areas=" + std::to_string(other) + "/18 (Fitness+Skill=" +
                      std::to_string(fitness) + "/4)" + (other > 14 ? " [specialTraining]" : ""));
        std::vector<std::vector<std::string>> players;
        for (const auto& p : t.players)
            players.push_back({"row:" + std::to_string(p.row), "id:" + std::to_string(p.player_id), utf8(p.name),
                               p.position >= 0 && p.position < 4 ? text(kPositionNames + p.position) : "?", std::to_string(p.intensity),
                               std::to_string(p.focus)});
        emit_table({"Row", "Id", "Player", "Pos", "Intensity", "Focus"}, players);
        emit('i', "automation bits: " + std::to_string(t.automation & 0x1f) + (t.needs_initialization() ? " (not initialized)" : ""));
    };
    openers_[16] = [this, training, training_request, dispatch_training, show_training] {
        const ui::TrainingState t = training();
        open_window("Training");
        // Initialize: the first opening initializes the plan (06-verein 3)
        if (t.needs_initialization()) {
            auto r = training_request(t, ui::TrainingRequestKind::initialize);
            r.desired_automation = 0x9f;
            dispatch_training(r);
            emit('+', "Training initialized");
        }
        show_training();
    };
    add({"Training", "ShowTraining", {}, "types, areas, sums, players, automation", false}, [show_training](const Values&) { show_training(); });
    // The engine accepts 1-3 balls (CareerSession::dispatch_training, valid_plan); the
    // concept's 0..3 would leave a field empty, which the original screen does not allow.
    add({"Training", "SetTrainingType", {choice("type", kTypes), integer("balls", 1, 3)}, "balls of a player type (sum <= 8)", true},
        [this, training, training_request, dispatch_training](const Values& v) {
            const ui::TrainingState t = training();
            const int type = v.choice("type"), value = int(v.integer("balls"));
            if (t.types[size_t(type)] == value) {
                emit('+', kTypes[size_t(type)] + " already " + std::to_string(value));
                return;
            }
            auto r = training_request(t, ui::TrainingRequestKind::set_type);
            r.index = type;
            r.value = value;
            r.desired_types[size_t(type)] = uint8_t(value);
            r.desired_automation &= uint8_t(~2);
            int sum = 0;
            for (auto b : r.desired_types) sum += b;
            if (sum > 8) fail("E_RULE", "the four player types may have 8 balls together (would be " + std::to_string(sum) + ")");
            dispatch_training(r);
            emit('+', kTypes[size_t(type)] + " = " + std::to_string(value));
        });
    add({"Training", "SetTrainingArea", {choice("area", kAreas), integer("balls", 1, 3)}, "balls of a training area (F+S <= 4, rest <= 18)", true},
        [this, training, training_request, dispatch_training](const Values& v) {
            const ui::TrainingState t = training();
            const int area = v.choice("area"), value = int(v.integer("balls"));
            if (t.areas[size_t(area)] == value) {
                emit('+', kAreas[size_t(area)] + " already " + std::to_string(value));
                return;
            }
            auto r = training_request(t, ui::TrainingRequestKind::set_area);
            r.index = area;
            r.value = value;
            r.desired_areas[size_t(area)] = uint8_t(value);
            r.desired_automation &= uint8_t(~4);
            int fitness = 0, other = 0;
            for (size_t i = 0; i < 9; ++i) (i < 2 ? fitness : other) += r.desired_areas[i];
            if (fitness > 4) fail("E_RULE", "Fitness and Skill may have 4 balls together (would be " + std::to_string(fitness) + ")");
            if (other > 18) fail("E_RULE", "the seven other areas may have 18 balls together (would be " + std::to_string(other) + ")");
            dispatch_training(r);
            emit('+', kAreas[size_t(area)] + " = " + std::to_string(value));
        });
    Param player = detail::text("player");
    player.display = "player:Player";
    add({"Training", "SetPlayerIntensity", {player, integer("intensity", 1, 3)}, "training intensity of a player", true},
        [this, training, training_request, dispatch_training, players_of](const Values& v) {
            const ui::TrainingState t = training();
            const auto& p = t.players[resolve_player(v.text("player"), players_of(t))];
            const int value = int(v.integer("intensity"));
            if (p.intensity == value) {
                emit('+', utf8(p.name) + " already " + std::to_string(value));
                return;
            }
            auto r = training_request(t, ui::TrainingRequestKind::set_player_intensity);
            r.index = p.row;
            r.player_id = p.player_id;
            r.value = value;
            r.desired_automation &= uint8_t(~8);
            dispatch_training(r);
            emit('+', utf8(p.name) + ": intensity " + std::to_string(value));
        });
    add({"Training", "CyclePlayerFocus", {player}, "the player's focus symbol: next focus", true},
        [this, training, training_request, dispatch_training, players_of](const Values& v) {
            const ui::TrainingState t = training();
            const auto& p = t.players[resolve_player(v.text("player"), players_of(t))];
            auto r = training_request(t, ui::TrainingRequestKind::set_player_focus);
            r.index = p.row;
            r.player_id = p.player_id;
            r.value = (int(p.focus) + 1) % 8;
            dispatch_training(r);
            emit('+', utf8(p.name) + ": focus " + std::to_string(r.value));
        });
    add({"Training", "SetTrainingAutomation", {integer("index", 0, 4), boolean("on")}, "one automation bit (D p. 109)", true},
        [this, training, training_request, dispatch_training](const Values& v) {
            const ui::TrainingState t = training();
            const int index = int(v.integer("index"));
            const bool on = v.boolean("on");
            const uint8_t mask = uint8_t(1u << index);
            if (((t.automation & mask) != 0) == on) {
                emit('+', "automation bit " + std::to_string(index) + " already " + on_off(on));
                return;
            }
            auto r = training_request(t, ui::TrainingRequestKind::set_automation);
            r.index = index;
            r.value = on ? 1 : 0;
            r.desired_automation = uint8_t((t.automation & ~mask) | (on ? mask : 0));
            dispatch_training(r);
            emit('+', "automation bit " + std::to_string(index) + " " + on_off(on));
        });
    add({"Training", "InitializeTraining", {}, "the original initialization (only when needed)", true},
        [this, training, training_request, dispatch_training](const Values&) {
            const ui::TrainingState t = training();
            if (!t.needs_initialization()) {
                emit('+', "training is already initialized");
                return;
            }
            auto r = training_request(t, ui::TrainingRequestKind::initialize);
            r.desired_automation = 0x9f;
            dispatch_training(r);
            emit('+', "Training initialized");
        });

    // --- TrainingCamp (06-verein 4) -------------------------------------------------------
    auto camp = [this] {
        const auto p = prep();
        if (!p.training_camp) fail("E_MODE_LOCKED", "no training camp available");
        return *p.training_camp;
    };
    auto camp_request = [this](const ui::TrainingCampState& c, ui::TrainingCampRequestKind kind) {
        ui::TrainingCampRequest r;
        r.kind = kind;
        r.manager_slot = c.manager_slot;
        r.runtime_club = c.runtime_club;
        r.revision = revision_;
        r.focus = c.focus;
        return r;
    };
    auto visits_text = [this](const ui::TrainingCampState& c) {
        if (c.booked_this_week()) return "booked this week";
        return c.visit_count() < 3 ? "" : "no visit possible";
    };
    auto show_camp = [this, camp, visits_text] {
        const ui::TrainingCampState c = camp();
        emit_table({"Slider", "Value"},
                   {{text(367), std::to_string(c.focus[0])}, {text(368), std::to_string(c.focus[1])},
                    {text(369), std::to_string(c.focus[2])}, {text(370), std::to_string(c.focus[3])}});
        std::string state = visits_text(c);
        emit('i', "visits this season: " + std::to_string(c.visit_count()) + "/3" + (state.empty() ? "" : " (" + state + ")") +
                      ", balance " + money(c.balance));
    };
    openers_[15] = [this, camp, camp_request, show_camp] {
        const ui::TrainingCampState c = camp();
        open_window("TrainingCamp");
        // SetTrainingCampFocus derives the fourth slider while drawing; the screen writes it back
        // once when it differs (TrainingCampScreen::initialize_focus)
        if (c.focus_normalization_required) {
            std::string error;
            require_engine(session_->dispatch_training_camp(camp_request(c, ui::TrainingCampRequestKind::set_focus), revision_, error),
                           "training camp", error);
        }
        show_camp();
    };
    add({"TrainingCamp", "ListResorts", {}, "the eight sports schools with stars and price", false}, [this, camp, visits_text](const Values&) {
        const ui::TrainingCampState c = camp();
        std::vector<std::vector<std::string>> rows;
        for (size_t r = 0; r < 8; ++r)
            rows.push_back({std::to_string(r), text(kCampResorts + int(r)), std::to_string(c.quality[r][0]), std::to_string(c.quality[r][1]),
                            std::to_string(c.quality[r][2]), std::to_string(c.quality[r][3]), money(c.prices[r])});
        emit_table({"Resort", "Name", text(kCampQualityTitles), text(kCampQualityTitles + 1), text(kCampQualityTitles + 2),
                    text(kCampQualityTitles + 3), "Price"},
                   rows);
        std::string state = visits_text(c);
        emit('i', "visits this season: " + std::to_string(c.visit_count()) + "/3" + (state.empty() ? "" : " (" + state + ")"));
    });
    add({"TrainingCamp", "ShowCamp", {}, "sliders, recuperation, visits", false}, [show_camp](const Values&) { show_camp(); });
    add({"TrainingCamp", "SetCampFocus", {integer("fitness", 0, 15), integer("skill", 0, 15), integer("game", 0, 15)},
         "the three sliders; recuperation follows", true},
        [this, camp, camp_request, show_camp](const Values& v) {
            const ui::TrainingCampState c = camp();
            auto r = camp_request(c, ui::TrainingCampRequestKind::set_focus);
            r.focus[0] = uint8_t(v.integer("fitness"));
            r.focus[1] = uint8_t(v.integer("skill"));
            r.focus[2] = uint8_t(v.integer("game"));
            r.focus[3] = uint8_t(15 - (r.focus[0] + r.focus[1] + r.focus[2]) / 3);  // SetTrainingCampFocus
            if (r.focus == c.focus) {
                emit('+', "sliders unchanged");
                return;
            }
            std::string error;
            require_engine(session_->dispatch_training_camp(r, revision_, error), "training camp", error);
            show_camp();
        });
    add({"TrainingCamp", "BookCamp", {integer("resort", 0, 7)}, "MACH ICH: book the sports school", true},
        [this, camp, camp_request](const Values& v) {
            const ui::TrainingCampState c = camp();
            if (c.booked_this_week()) fail("E_RULE", text(kCampRecentlyVisited), "campRecentlyVisited");
            if (c.visit_count() >= 3) fail("E_RULE", text(kCampBoardDeniesFurtherVisits), "campBoardDeniesFurtherVisits");
            auto r = camp_request(c, ui::TrainingCampRequestKind::book);
            r.resort = int(v.integer("resort"));
            std::string error;
            require_engine(session_->dispatch_training_camp(r, revision_, error), "training camp", error);
            const ui::TrainingCampState after = camp();
            switch (after.feedback) {
            case ui::TrainingCampFeedback::insufficient_funds: fail("E_INSUFFICIENT_FUNDS", "the camp costs " + money(c.prices[size_t(r.resort)]));
            case ui::TrainingCampFeedback::booked_this_week: fail("E_RULE", text(kCampRecentlyVisited), "campRecentlyVisited");
            case ui::TrainingCampFeedback::visit_limit: fail("E_RULE", text(kCampBoardDeniesFurtherVisits), "campBoardDeniesFurtherVisits");
            default: break;
            }
            emit('+', text(kCampResorts + r.resort) + " booked for " + money(c.prices[size_t(r.resort)]) + ". " + text(kCampPlayersReturnHappy));
        });
}

}  // namespace bmh::cli::console
