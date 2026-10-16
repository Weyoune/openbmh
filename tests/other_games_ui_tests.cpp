// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/other_games.hpp"
#include "ui/flow.hpp"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void rejected(const std::function<void()>& action, const char* message) {
    try { action(); } catch (const FormatError&) { return; }
    throw std::runtime_error(message);
}

OtherGamesState state_for(OtherGamesDisplayMode mode, int participants = 24, int offset = 0,
                          int competition = 0) {
    OtherGamesState state;
    state.identity = {0, 3, 11, 100, 8, 44, 0, uint8_t(mode == OtherGamesDisplayMode::detail ? 0x80 : 0)};
    state.projection.competition = competition;
    state.projection.descriptor_competition = competition;
    state.projection.linked_page = 0;
    state.projection.entry_index = 0;
    state.projection.entry_address = 0x20000;
    state.projection.knockout = true;
    state.projection.current_round = 1;
    state.projection.round = 1;
    state.projection.rounds = 3;
    state.projection.title = "Pokal";
    state.projection.round_heading = "Unit knockout";
    state.projection.source_participant_count = participants;
    state.projection.participant_offset = mode == OtherGamesDisplayMode::detail ? offset : 0;
    state.projection.display_mode_toggle_available = participants > 16;
    state.projection.display_mode = mode;
    state.projection.round_arrows_visible = true;
    state.projection.fixtures_available = true;
    for (int slot = 0; slot < 8; ++slot) {
        const int competition = slot == 2 ? 12 : slot < 2 ? slot : 20;
        state.projection.selectors.push_back({slot, competition, competition == 20 ? "" : "Pokal", slot == 0});
        state.selector_labels[size_t(slot)] = competition == 20 ? "" : "Pokal";
    }
    state.projection.linked_pages = {{0, 0, competition, 0, 2}, {1, 0, competition, 1, 2},
                                     {2, 1, competition, 0, 1}};
    state.source_participant_offset = offset;
    const int projected_participants = mode == OtherGamesDisplayMode::detail
        ? std::min(16, std::max(0, participants - offset)) : participants;
    for (int match = 0; match < projected_participants / 2; ++match) {
        OtherGamesRow row;
        row.kind = OtherGamesRowKind::match;
        row.home_team = 3 + (mode == OtherGamesDisplayMode::detail ? offset : 0) + match * 2;
        row.away_team = 4 + (mode == OtherGamesDisplayMode::detail ? offset : 0) + match * 2;
        row.home_name = "Heim";
        row.away_name = "Gast";
        row.source_result = 0x0042;
        row.played = true;
        row.home_goals = 2;
        row.away_goals = 1;
        row.source_match = match + (mode == OtherGamesDisplayMode::detail ? offset / 2 : 0);
        state.projection.rows.push_back(row);
        OtherGamesDisplayRow display;
        display.name_colors = {0x5f, 0x5f};
        display.league_tiers = {0, 1};
        display.countries = {0xff, 0xff};
        display.details = {"10 08 01 01 01 02", "10 05 02 01 02 03"};
        display.primary_result = "2:1";
        state.rows.push_back(display);
    }
    return state;
}

void press_release(OtherGamesScreen& screen, int x, int y) {
    check(screen.pointer_down(x, y), "Source control down should be accepted");
    check(screen.release(x, y), "Source control up should resolve the armed control");
}

void action_and_cancel_tests(const std::filesystem::path& data) {
    OtherGamesScreen screen(data, state_for(OtherGamesDisplayMode::detail));
    check(screen.render().width == 320 && screen.render().height == 240,
          "renderer uses the Original 320 by 240 coordinate plane");

    // The selector row uses the Source two-column/four-row 86x10 controls.
    press_release(screen, 44, 189);
    check(screen.pending() && screen.pending()->action == OtherGamesAction::select_competition &&
          screen.pending()->value == 1, "selector button carries its exact slot, not competition ID");
    const auto frozen = screen.state_json();
    screen.reject_action("test rejection");
    check(!screen.pending() && screen.state_json().find("test rejection") != std::string::npos &&
          screen.state().projection.competition == 0 && screen.state_json().find("\"scope\":44") != std::string::npos,
          "rejected selection preserves the owned page and records a visible rejection");
    check(frozen.find("\"competition\":0") != std::string::npos,
          "before-rejection projection remains the accepted current competition");

    // Source linked selector cells are 8x8 inclusive and carry a flattened ordinal.
    check(screen.pointer_down(237, 178), "linked stage group cell is actionable");
    check(screen.pending() && screen.pending()->action == OtherGamesAction::select_linked_page &&
          screen.pending()->value == 1, "linked cell sends its flattened page ordinal");
    screen.reject_action("test rejection");
    check(screen.pointer_down(233, 187), "a new linked stage advances the Source group row by nine pixels");
    check(screen.pending() && screen.pending()->value == 2,
          "linked-stage row maps to the correct flattened ordinal");
    screen.reject_action("test rejection");

    // Held/released arrows use Source bounds; a drag out cancels without an action.
    check(screen.pointer_down(263, 177), "arrow down arms the Original arrow");
    check(screen.move(320, 219), "moving outside the arrow clears the pressed state");
    check(!screen.release(320, 219) && !screen.pending(), "outside release cancels the arrow action");
    press_release(screen, 263, 177);
    check(screen.pending() && screen.pending()->action == OtherGamesAction::previous_round &&
          screen.pending()->value == -1, "left round arrow emits a typed request without a guessed target round");
    screen.reject_action("test rejection");
    check(!screen.pointer_down(262, 176), "pixels outside the Source arrow rectangle do not activate it");

    press_release(screen, 316, 215);
    check(screen.pending() && screen.pending()->action == OtherGamesAction::toggle_display_mode,
          "display toggle accepts the inclusive right edge");
    screen.reject_action("test rejection");

    check(screen.pointer_down(4, 43), "Source strict team hit rectangle identifies a visible home team");
    check(screen.pending() && screen.pending()->action == OtherGamesAction::team_info &&
          screen.pending()->value == 3, "team-info request carries the visible runtime team ID");
    screen.reject_action("Team information is not available here");
    check(screen.render().width == 320, "unsupported team information leaves the current modal usable");

    check(screen.pointer_down(4, 43), "team hit is re-enabled after a rejected route");
    screen.reject_action("Team information is not available here");
    check(screen.pointer_down(160, 160, true), "Original right-pointer event requests close");
    check(screen.pending() && screen.pending()->action == OtherGamesAction::close &&
          screen.pending()->value == -1, "close carries no forged selector value");
}

void geometry_tests(const std::filesystem::path& data) {
    const auto arrows = load_vga(asset_path(data / "KB", "1.VGA"));
    check(arrows.width == 253 && arrows.height == 43,
          "Original KB1 contains the Source arrow strips and badge row in a 253 by 43 atlas");
    auto empty_state = state_for(OtherGamesDisplayMode::detail, 0);
    OtherGamesScreen empty(data, std::move(empty_state));
    check(empty.render().width == 320 && !empty.pointer_down(5, 42),
          "zero-participant cursor view is valid and exposes no fabricated fixture action");
    auto unsupported_empty_stage = state_for(OtherGamesDisplayMode::detail);
    unsupported_empty_stage.projection.linked_pages[0].group_count = 0;
    rejected([&] { OtherGamesScreen invalid(data, std::move(unsupported_empty_stage)); },
             "linked graph without represented groups must fail closed");

    auto compact_state = state_for(OtherGamesDisplayMode::compact, 24);
    compact_state.identity.expected_view_byte = 0;
    compact_state.source_participant_offset = 16; // retained when compact hides the row offset
    compact_state.projection.participant_offset = 0;
    OtherGamesScreen compact(data, std::move(compact_state));
    // Compact mode renders all rows from the compact layout; row coordinates do not inherit
    // the saved detailed cursor.
    check(compact.pointer_down(1, 83), "compact row's first strict hit pixel is actionable");
    check(compact.pending() && compact.pending()->action == OtherGamesAction::team_info &&
          compact.pending()->value == 3, "compact projection uses all rows despite retained cursor");
    compact.reject_action("unsupported");
    check(!compact.pointer_down(102, 83), "compact home/away gap remains non-actionable");
    press_release(compact, 202, 215);
    check(compact.pending() && compact.pending()->action == OtherGamesAction::toggle_display_mode,
          "compact mode keeps the Source toggle available to return to detail");
    compact.reject_action("toggle rejected for fixture");

    auto second_page_state = state_for(OtherGamesDisplayMode::detail, 32, 16);
    check(second_page_state.projection.rows.size() == 8,
          "detail fixture contains only the already-windowed second page rows");
    OtherGamesScreen second_page(data, std::move(second_page_state));
    check(second_page.pointer_down(4, 43), "cursor16 uses the first already-windowed fixture row");
    check(second_page.pending() && second_page.pending()->value == 19,
          "cursor16 render/hit consumes the projected row from index zero");
    second_page.reject_action("unsupported team detail");

    auto overlap_state = state_for(OtherGamesDisplayMode::detail, 24);
    OtherGamesScreen overlap(data, std::move(overlap_state));
    check(overlap.pointer_down(300, 215), "mode toggle wins where it overlaps the right round arrow");
    check(overlap.release(300, 215) && overlap.pending() &&
          overlap.pending()->action == OtherGamesAction::toggle_display_mode,
          "Source toggle hit priority is resolved before the overlapping arrow");

    auto linked_first = state_for(OtherGamesDisplayMode::detail);
    auto linked_auto_advanced = linked_first;
    linked_auto_advanced.projection.linked_page = 2; // Source retains page ordinal independently.
    OtherGamesScreen linked_a(data, std::move(linked_first));
    OtherGamesScreen linked_b(data, std::move(linked_auto_advanced));
    check(linked_a.render().pixels == linked_b.render().pixels,
          "linked selector highlight follows the resolved descriptor entry, not stale ordinal");

    auto group_state = state_for(OtherGamesDisplayMode::detail, 8);
    group_state.projection.knockout = false;
    group_state.projection.group_count = 2;
    group_state.projection.group_index = 0;
    group_state.projection.round = 0;
    group_state.projection.rounds = 2;
    group_state.projection.source_participant_count = 8;
    group_state.projection.group_tables.resize(1);
    group_state.projection.group_tables.front().size = 8;
    group_state.projection.display_mode_toggle_available = false;
    group_state.projection.round_arrows_visible = false;
    group_state.projection.rows.resize(4);
    group_state.rows.resize(4);
    for (int rank = 0; rank < 8; ++rank) {
        OtherGamesTableRow table;
        table.runtime_team = 3 + rank;
        table.rank = rank + 1;
        table.previous_rank = rank + 1;
        table.name = "Club";
        table.games = 2;
        table.wins = 1;
        table.points = 3;
        group_state.table_rows.push_back(table);
    }
    OtherGamesScreen group(data, std::move(group_state));
    check(group.pointer_down(1, 34), "group fixture hit starts at Source baseline 39");
    check(group.pending() && group.pending()->value == 3, "group fixture returns its home team");
    group.reject_action("unsupported");
    check(group.pointer_down(126, 34), "group overlap pixel follows Source home-first hit order");
    check(group.pending() && group.pending()->value == 3, "overlapping group boundary resolves home first");
    group.reject_action("unsupported");

    auto two_team_group = state_for(OtherGamesDisplayMode::detail, 2);
    two_team_group.projection.knockout = false;
    two_team_group.projection.group_count = 1;
    two_team_group.projection.group_index = 0;
    two_team_group.projection.source_participant_count = 2;
    OtherGamesScreen two_team_screen(data, two_team_group);
    auto two_team_frame = two_team_screen.render();
    check(two_team_frame.at(0, 96) == 0x46 && two_team_frame.at(0, 114) == 0x40,
          "two-team group fixture frame uses first baseline 104 and Source edges 96/114");
    check(two_team_screen.pointer_down(1, 99) && two_team_screen.pending()->value == 3,
          "two-team group fixture hit begins below the Source first baseline");
}

void badge_and_selected_button_tests(const std::filesystem::path& data) {
    const auto exe = load_le(asset_path(data, "BMH.exe"));
    const auto badges = load_vga(asset_path(data / "KB", "1.VGA"));
    // legacy routine's 16x5 badge includes zero-filled padding. legacy routine skips
    // format field==0, including the holes between the crown's points.
    for (const auto mode : {OtherGamesDisplayMode::detail, OtherGamesDisplayMode::compact}) {
        for (int tier = 0; tier < 5; ++tier) {
            auto state = state_for(mode);
            state.rows.front().league_tiers = {uint8_t(tier), uint8_t(tier)};
            const auto rendered = OtherGamesScreen(data, state).render();
            state.rows.front().league_tiers = {0xff, 0xff};
            const auto underlay = OtherGamesScreen(data, state).render();
            const int top = mode == OtherGamesDisplayMode::detail ? 43 : 81;
            const int sx = exe.u8(::bmh::layout::CupDraw_GroupMarker_RuntimeField + uint32_t(tier));
            int preserved = 0, drawn = 0;
            for (const int x : mode == OtherGamesDisplayMode::detail
                                    ? std::array<int, 2>{115, 242}
                                    : std::array<int, 2>{90, 200}) {
                for (int yy = 0; yy < 5; ++yy) {
                    for (int xx = 0; xx < 16; ++xx) {
                        const auto color = badges.pixels[size_t(38 + yy) * badges.width + sx + xx];
                        const auto expected = color == 0 ? underlay.pixels[size_t(top + yy) * 320 + x + xx]
                                                        : color;
                        check(rendered.pixels[size_t(top + yy) * 320 + x + xx] == expected,
                              "Source badge preserves zero padding and draws the actual atlas glyph");
                        color == 0 ? ++preserved : ++drawn;
                    }
                }
            }
            check(preserved > 0 && drawn > 0, "badge oracle exercises both mask holes and visible pixels");
        }
    }

    // legacy routine deliberately uses (+2,+2) for selection as well as mouse-down.
    // Check the raw top strip, without text, independently of draw_button.
    auto state = state_for(OtherGamesDisplayMode::detail, 0, 0, 99);
    state.selector_labels.fill("");
    const auto widgets = load_vga(asset_path(data / "KB", "0.VGA"));
    auto held = OtherGamesScreen(data, state);
    const auto released = held.render();
    check(held.pointer_down(20, 180), "selector pointer-down arms its pressed strip");
    const auto pressed = held.render();
    state.projection.competition = 0;
    const auto selected = OtherGamesScreen(data, state).render();
    for (int yy = 0; yy < 3; ++yy) {
        for (int xx = 0; xx < 31; ++xx) {
            const auto color = widgets.pixels[size_t(44 + yy) * widgets.width + 24 + xx];
            if (color == 0xc0) continue;
            const size_t at = size_t(178 + yy) * 320 + 3 + xx;
            check(selected.pixels[at] == color && pressed.pixels[at] == color,
                  "selected and physically held buttons share the Original +2,+2 atlas origin");
        }
    }
    check(held.move(319, 150) && held.render().pixels == released.pixels,
          "cancelled pointer-down restores the unpressed button without residual edges");
}

void style_header_and_table_tests(const std::filesystem::path& data) {
    auto normal = state_for(OtherGamesDisplayMode::detail, 24);
    normal.decorated_background = false;
    normal.gradient_text = false;
    normal.original_panel = true;
    auto normal_with_panel = OtherGamesScreen(data, normal).render();
    normal.original_panel = false;
    auto normal_without_panel = OtherGamesScreen(data, normal).render();
    check(normal_with_panel.at(0, 40) == 0x46 && normal_without_panel.at(0, 40) != 0x46,
          "Source NULL normal-panel branch skips the ordinary knockout frame");

    auto group_normal = state_for(OtherGamesDisplayMode::detail, 8);
    group_normal.projection.knockout = false;
    group_normal.projection.source_participant_count = 8;
    group_normal.projection.group_tables.resize(1);
    group_normal.projection.group_tables.front().size = 8;
    OtherGamesTableRow group_table_row;
    group_table_row.runtime_team = 3;
    group_table_row.rank = 1;
    group_table_row.name = "Club";
    group_normal.table_rows = {group_table_row};
    group_normal.decorated_background = false;
    group_normal.gradient_text = false;
    auto group_with_panels = OtherGamesScreen(data, group_normal).render();
    group_normal.original_panel = false;
    auto group_without_panels = OtherGamesScreen(data, group_normal).render();
    check(group_with_panels.at(0, 31) == 0x46 && group_with_panels.at(0, 125) == 0x46 &&
          group_without_panels.at(0, 31) != 0x46 && group_without_panels.at(0, 125) != 0x46,
          "Source NULL group panel branch skips both ordinary fixture and standings frames");

    auto blocked = state_for(OtherGamesDisplayMode::detail, 24);
    blocked.projection.unfinished_predecessor = true;
    blocked.decorated_background = false;
    blocked.gradient_text = false;
    blocked.original_panel = false;
    auto blocked_frame = OtherGamesScreen(data, blocked).render();
    check(blocked_frame.at(0, 112) == 0x46 && blocked_frame.at(10, 113) == 0x42,
          "nullable blocked warning keeps legacy routine's flat frame when Original panel is disabled");

    auto selected_twelve = state_for(OtherGamesDisplayMode::detail, 24, 0, 12);
    selected_twelve.projection.descriptor_competition = 11;
    selected_twelve.date_heading = "DATUM";
    auto with_descriptor_date = OtherGamesScreen(data, selected_twelve).render();
    selected_twelve.date_heading.clear();
    auto without_descriptor_date = OtherGamesScreen(data, selected_twelve).render();
    check(with_descriptor_date.pixels != without_descriptor_date.pixels,
          "date header is visible when selected ID is 12 but linked descriptor ID is not");

    auto descriptor_twelve = state_for(OtherGamesDisplayMode::detail, 24, 0, 0);
    descriptor_twelve.projection.descriptor_competition = 12;
    descriptor_twelve.date_heading = "DATUM";
    auto suppressed_descriptor_date = OtherGamesScreen(data, descriptor_twelve).render();
    descriptor_twelve.date_heading.clear();
    auto no_date = OtherGamesScreen(data, descriptor_twelve).render();
    check(suppressed_descriptor_date.pixels == no_date.pixels,
          "date header stays suppressed when the selected descriptor ID is 12");

    auto table_state = state_for(OtherGamesDisplayMode::detail, 4);
    table_state.projection.knockout = false;
    table_state.projection.source_participant_count = 4;
    table_state.projection.group_tables.resize(1);
    table_state.projection.group_tables.front().size = 4;
    table_state.table_rows.clear();
    for (int rank = 0; rank < 4; ++rank) {
        OtherGamesTableRow table;
        table.runtime_team = 3 + rank;
        table.rank = rank + 1;
        table.previous_rank = rank == 0 ? 3 : rank == 1 ? 2 : rank == 2 ? 1 : 4;
        table.name = "Club";
        table.games = 1;
        table.wins = 1;
        table.points = 3;
        table_state.table_rows.push_back(table);
    }
    table_state.previous_rank_visible = true;
    const auto prior_rank_visible = OtherGamesScreen(data, table_state).render();
    table_state.previous_rank_visible = false;
    const auto prior_rank_hidden = OtherGamesScreen(data, table_state).render();
    check(prior_rank_visible.pixels != prior_rank_hidden.pixels,
          "stage-level previous-rank visibility is honored even when this row has one game");
}

void blocked_view_tests(const std::filesystem::path& data) {
    auto blocked = state_for(OtherGamesDisplayMode::detail);
    blocked.projection.unfinished_predecessor = true;
    OtherGamesScreen screen(data, std::move(blocked));
    check(!screen.pointer_down(4, 43), "unfinished predecessor has no fixture hit targets");
    check(!screen.pointer_down(202, 215), "future-round panel hides the mode toggle");
    check(screen.pointer_down(263, 177), "source round arrow remains available in detail view");
    check(screen.release(263, 177) && screen.pending() &&
          screen.pending()->action == OtherGamesAction::previous_round,
          "blocked rows do not invent team hits or consume the Source round arrow");
}

void revision(PreparationState& state, uint64_t value) {
    state.desktop.revision = state.lineup.revision = state.tactics.revision = value;
    if (state.training) state.training->revision = value;
    if (state.pitch) state.pitch->revision = value;
    for (auto& card : state.player_cards) card.lineup.revision = value;
}

PreparationState flow_fixture(const LeImage& exe) {
    PreparationState state;
    auto& d = state.desktop;
    d.source = "engine_projection"; d.revision = 7;
    d.manager_slot = 0; d.runtime_club = 12; d.club_name = "BAYERN"; d.manager_name = "Nobody";
    d.manager_teams = {12}; d.managers = {{12,12,0,0,0,"Nobody"}};
    d.league = 0; d.portrait = 0; d.date = desk_date(0, exe); d.strength = 80;
    d.options.managers = 1; d.options.world = false; d.options.league = true;
    d.automation[4] = 0x9f;
    for (int kind : {3, 16, 18, 12}) {
        DeskItem item; item.id = kind + 1; item.kind = kind; item.x = 20 + int(d.items.size()) * 45;
        item.y = 150; item.width = 32; item.height = 25; d.items.push_back(item);
    }
    DeskItem cup; cup.id = 50; cup.kind = 5; cup.x = 200; cup.y = 150; cup.width = 32; cup.height = 25;
    d.items.push_back(cup);
    state.lineup.manager_slot = state.tactics.manager_slot = 0;
    state.lineup.runtime_club = state.tactics.runtime_club = 12;
    state.lineup.revision = state.tactics.revision = d.revision;
    state.lineup.source = state.tactics.source = d.source;
    state.lineup.club_name = state.tactics.club_name = d.club_name;
    state.tactics.manager_name = d.manager_name;
    for (int row = 0; row < 24; ++row) {
        LineupPlayer player; player.row = row;
        if (row < 2) { player.slot = uint16_t(0x8000 | row); player.player_id = row;
            player.name = row ? "PLAYER" : "KEEPER"; player.board[0x0a] = 2;
            player.board[0x41] = row ? 3 : 0; player.board[0x42] = 4; }
        state.lineup.players.push_back(player);
    }
    TrainingState training; training.manager_slot = 0; training.runtime_club = 12;
    training.revision = d.revision; training.source = d.source; training.club_name = d.club_name;
    training.mode = d.options.mode; training.squad_capacity = 24; training.types.fill(2);
    training.areas.fill(2); training.automation = 0x9f;
    training.players = {{0,0,0,0,"KEEPER",2,0,0},{1,1,0,0,"PLAYER",2,0,0}};
    state.training = training;
    PitchExtras pitch; pitch.manager_slot = 0; pitch.runtime_club = 12; pitch.revision = d.revision;
    pitch.source = d.source; pitch.indoor = false; pitch.mode = d.options.mode; pitch.badge = d.badge;
    state.pitch = pitch;
    PlayerCardState card; card.lineup = state.lineup; card.row = 0; card.badge = d.badge;
    card.nationality_name = "DEUTSCHLAND"; card.club_short_name = "BAYERN";
    card.statistics = PlayerStatisticsContext{0,0x40};
    state.player_cards.push_back(card);
    return state;
}

void flow_acknowledgement_tests(const std::filesystem::path& data) {
    auto exe = load_le(asset_path(data, "BMH.exe"));
    StartupFlow flow(data);
    flow.click(150,215); flow.click(5,225,true); flow.click(5,225,true);
    check(flow.click(90,169).request == FlowRequest::new_career, "flow fixture opens a new career request");
    auto preparation = flow_fixture(exe);
    preparation.desktop.options = flow.pending_career()->options;
    preparation.training->mode = preparation.desktop.options.mode;
    preparation.pitch->mode = preparation.desktop.options.mode;
    validate_preparation_context(preparation);
    flow.attach_initialized_career({preparation.desktop, {{0,0,12}}});
    flow.attach_preparation_context(preparation);

    const auto opened = flow.click(216,162);
    check(opened.request == FlowRequest::desk_action && opened.desk_action.menu_kind == 5 &&
          flow.desktop()->pending(), "menu 5 produces the owned desk request");
    const DeskRequest owner = *flow.desktop()->pending();
    auto initial = state_for(OtherGamesDisplayMode::detail, 24, 0, 12);
    initial.identity = {0,12,12,0,owner.revision + 1,777,0,0x80};
    revision(preparation, owner.revision + 1);
    flow.show_other_games(initial, preparation, owner);
    check(flow.in_other_games() && flow.screen_name() == "other_games",
          "valid OPEN acknowledgement transfers the view owner");

    check(flow.pointer_down(44,202).changed, "selector down arms the Flow-owned control");
    const auto select = flow.click(44,202);
    check(select.request == FlowRequest::other_games_action &&
          flow.pending_other_games_action()->action == OtherGamesAction::select_competition,
          "same selected competition remains an explicit Source action");
    auto second_page = state_for(OtherGamesDisplayMode::detail, 24, 16, 12);
    second_page.identity = flow.pending_other_games_action()->identity;
    ++second_page.identity.revision; ++second_page.identity.sequence;
    second_page.identity.expected_view_byte = 0x90;
    auto ack_preparation = preparation; revision(ack_preparation, second_page.identity.revision);
    auto forged = second_page; ++forged.identity.sequence;
    const auto before_bad_ack = flow.state_json();
    const auto before_bad_frame = flow.render().pixels;
    bool rejected = false;
    try { flow.acknowledge_other_games(forged, ack_preparation); } catch (const FormatError&) { rejected = true; }
    check(rejected && flow.state_json() == before_bad_ack && flow.render().pixels == before_bad_frame &&
          flow.in_other_games() && flow.pending_other_games_action() &&
          flow.pending_other_games_action()->action == OtherGamesAction::select_competition,
          "one-field forged ACK leaves the page owner and pending action intact");
    flow.acknowledge_other_games(second_page, ack_preparation);
    check(!flow.pending_other_games_action() && flow.other_games()->state().source_participant_offset == 16 &&
          flow.other_games()->state().identity.expected_view_byte == 0x90,
          "same selected-ID paging advances both local and saved cursor");

    check(flow.pointer_down(263,177).changed, "round arrow down arms the Flow-owned control");
    const auto arrow = flow.click(263,177);
    check(arrow.request == FlowRequest::other_games_action &&
          flow.pending_other_games_action()->action == OtherGamesAction::previous_round,
          "round arrow request is separately owned");
    const auto arrow_request = *flow.pending_other_games_action();
    auto round_page = state_for(OtherGamesDisplayMode::detail, 24, 0, 12);
    round_page.identity = arrow_request.identity; ++round_page.identity.revision; ++round_page.identity.sequence;
    round_page.identity.expected_view_byte = 0x90; round_page.projection.round = 0;
    auto round_preparation = ack_preparation; revision(round_preparation, round_page.identity.revision);
    flow.acknowledge_other_games(round_page, round_preparation);
    check(flow.other_games()->state().source_participant_offset == 0 &&
          flow.other_games()->state().identity.expected_view_byte == 0x90,
          "round navigation resets only the local cursor and preserves saved bb13c low7");

    check(flow.pointer_down(229,187).changed, "linked cell down arms the Flow-owned control");
    const auto linked = flow.click(229,187);
    check(linked.request == FlowRequest::other_games_action &&
          flow.pending_other_games_action()->action == OtherGamesAction::select_linked_page,
          "linked-page click carries an owned ordinal");
    const auto linked_request = *flow.pending_other_games_action();
    auto linked_page = state_for(OtherGamesDisplayMode::detail, 24, 16, 12);
    linked_page.identity = linked_request.identity; ++linked_page.identity.revision; ++linked_page.identity.sequence;
    linked_page.identity.expected_view_byte = 0x90;
    linked_page.projection.linked_page = 2; linked_page.projection.entry_index = 1;
    auto linked_preparation = round_preparation; revision(linked_preparation, linked_page.identity.revision);
    flow.acknowledge_other_games(linked_page, linked_preparation);
    check(flow.other_games()->state().source_participant_offset == 16,
          "linked-page reload restores the saved low7 cursor after local arrow reset");
}

void selector_saved_cursor_fallback_test(const std::filesystem::path& data) {
    auto exe = load_le(asset_path(data, "BMH.exe"));
    StartupFlow flow(data);
    flow.click(150,215); flow.click(5,225,true); flow.click(5,225,true);
    check(flow.click(90,169).request == FlowRequest::new_career,
          "fallback fixture opens a new career request");
    auto preparation = flow_fixture(exe);
    preparation.desktop.options = flow.pending_career()->options;
    preparation.training->mode = preparation.desktop.options.mode;
    preparation.pitch->mode = preparation.desktop.options.mode;
    validate_preparation_context(preparation);
    flow.attach_initialized_career({preparation.desktop, {{0,0,12}}});
    flow.attach_preparation_context(preparation);
    const auto opened = flow.click(216,162);
    check(opened.request == FlowRequest::desk_action && opened.desk_action.menu_kind == 5 &&
          flow.desktop()->pending(), "fallback fixture opens the owned menu-5 action");
    const DeskRequest owner = *flow.desktop()->pending();
    auto initial = state_for(OtherGamesDisplayMode::detail, 32, 0, 0);
    initial.identity = {0,12,12,0,owner.revision+1,778,0,0x80};
    revision(preparation, owner.revision+1);
    flow.show_other_games(initial, preparation, owner);

    check(flow.pointer_down(44,176).changed, "same-ID paging control arms for saved-cursor fixture");
    auto request = flow.click(44,176);
    check(request.request == FlowRequest::other_games_action, "same-ID paging dispatches through Flow");
    auto page16 = state_for(OtherGamesDisplayMode::detail, 32, 16, 0);
    page16.identity = flow.pending_other_games_action()->identity;
    ++page16.identity.revision; ++page16.identity.sequence;
    page16.identity.expected_view_byte = 0x90;
    auto page16_preparation = preparation; revision(page16_preparation,page16.identity.revision);
    flow.acknowledge_other_games(page16,page16_preparation);

    check(flow.pointer_down(302,177).changed, "right round arrow arms after paging");
    request = flow.click(302,177);
    check(request.request == FlowRequest::other_games_action, "round arrow dispatches through Flow");
    auto next_round = state_for(OtherGamesDisplayMode::detail, 16, 0, 0);
    next_round.identity = flow.pending_other_games_action()->identity;
    ++next_round.identity.revision; ++next_round.identity.sequence;
    next_round.identity.expected_view_byte = 0x90;
    next_round.projection.round = 2;
    auto next_round_preparation = page16_preparation;
    revision(next_round_preparation,next_round.identity.revision);
    flow.acknowledge_other_games(next_round,next_round_preparation);
    check(flow.other_games()->state().projection.round == 2 &&
          flow.other_games()->state().projection.source_participant_count == 16 &&
          flow.other_games()->state().source_participant_offset == 0 &&
          flow.other_games()->state().identity.expected_view_byte == 0x90,
          "right arrow advances round1/count32 to round2/count16, resets local cursor, and retains saved low7 offset16");

    check(flow.pointer_down(44,176).changed, "nonpaging selector arms after local arrow reset");
    request = flow.click(44,176);
    check(request.request == FlowRequest::other_games_action &&
          flow.pending_other_games_action()->value == 0,
          "same ID/slot selector follows Source's nonpaging ACK path");
    auto restored = state_for(OtherGamesDisplayMode::detail, 32, 16, 0);
    restored.identity = flow.pending_other_games_action()->identity;
    ++restored.identity.revision; ++restored.identity.sequence;
    restored.identity.expected_view_byte = 0x90;
    auto restored_preparation = next_round_preparation;
    revision(restored_preparation,restored.identity.revision);
    auto forged = restored;
    forged.source_participant_offset = 0;
    const auto before_bad = flow.state_json();
    const auto before_bad_frame = flow.render().pixels;
    rejected([&] { flow.acknowledge_other_games(forged,restored_preparation); },
             "nonpaging selector rejects a one-field wrong-cursor ACK");
    check(flow.state_json() == before_bad && flow.render().pixels == before_bad_frame &&
          flow.pending_other_games_action() &&
          flow.pending_other_games_action()->action == OtherGamesAction::select_competition,
          "bad saved-cursor ACK preserves page owner and pending selector action");
    flow.acknowledge_other_games(restored,restored_preparation);
    check(flow.other_games()->state().source_participant_offset == 16 &&
          flow.other_games()->state().identity.expected_view_byte == 0x90 &&
          flow.other_games()->state().projection.round == 1 &&
          flow.other_games()->state().projection.source_participant_count == 32 &&
          flow.other_games()->state().projection.rows.size() == 8,
          "same ID/slot fallback re-enters round1/count32 at cursor16 with eight fixtures and saved view byte");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: other_games_ui_tests <BMH data>");
        const std::filesystem::path data(argv[1]);
        action_and_cancel_tests(data);
        geometry_tests(data);
        badge_and_selected_button_tests(data);
        style_header_and_table_tests(data);
        blocked_view_tests(data);
        flow_acknowledgement_tests(data);
        selector_saved_cursor_fallback_test(data);
        std::cout << "OtherGames Source geometry, action ownership, and rejection behavior passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
