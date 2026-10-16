// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/club_info.hpp"
#include "ui/setup.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char* what) { if (!value) throw std::runtime_error(what); }

bmh::ui::ClubInfoProjection projection(bool squad) {
    bmh::ui::ClubInfoProjection p;
    p.runtime_team = 17; p.original_club_name = "FC TESTVEREIN";
    p.has_squad = squad; p.category = 1;
    if (squad) for (int row = 0; row < 16; ++row) {
        bmh::ui::ClubInfoPlayer player;
        player.row = row; player.rendered_row = row; player.physical_row = row;
        player.player_id = row; player.displayed_slot = uint16_t(0x8000 | row);
        player.original_name = row == 0 ? "NEUER" : "TESTSPIELER";
        player.attributes.w35c = uint32_t(45 + row) | (uint32_t(60 + row) << 7) | (uint32_t(70 + row) << 14);
        player.attributes.w364 = uint32_t(row % 4) | (uint32_t(5 + row % 3) << 21);
        player.attributes.w368 = uint32_t(row % 4) | (uint32_t((row + 1) % 4) << 2);
        player.cards = uint16_t(row + 1); player.bonus = uint8_t(row);
        if (row == 0) { std::array<uint8_t, 0x58> board{}; board[7] = 12; board[9] = 4; board[0xf] = 58; player.manager_board = board; }
        p.players.push_back(std::move(player));
    }
    return p;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: club_info_tests <Original BMH data>");
        const std::filesystem::path data(argv[1]);
        if (!std::filesystem::exists(data) || !std::filesystem::exists(data / "BMH.exe")) {
            std::cout << "SKIP: Original BMH data is unavailable\n";
            return 0;
        }
        bmh::ui::ClubInfoScreen screen(data, projection(true));
        const auto image_a = screen.render();
        check(image_a.width == 320 && image_a.height == 240, "Source framebuffer dimensions");
        check(screen.pointer_down(20, 30).kind == bmh::ui::ClubInfoRequestKind::open_player_detail,
              "name-column click routes to player detail");
        const auto row_request = screen.pointer_down(20, 30);
        check(row_request.player_id == 0 && row_request.rendered_row == 0 && row_request.physical_row == 0 && row_request.runtime_team == 17,
              "player request owns runtime team and row identity");
        check(screen.pointer_down(200, 30).kind == bmh::ui::ClubInfoRequestKind::none,
              "click outside name column is inert");
        check(screen.pointer_down(200, 70, true).kind == bmh::ui::ClubInfoRequestKind::close,
              "right button down closes the original modal");
        check(screen.button_at(52, 220) == bmh::ui::ClubInfoButton::view,
              "view button Source inclusive edge");
        screen.press(10, 220);
        check(screen.pressed_button() == bmh::ui::ClubInfoButton::view, "view press remembered");
        check(screen.render().pixels != image_a.pixels, "pressed button uses its Original active frame");
        screen.move(80, 220);
        check(!screen.pressed_button(), "leaving button cancels press");
        check(screen.release(10, 220).kind == bmh::ui::ClubInfoRequestKind::none,
              "cancelled button cannot activate on release");
        screen.press(10, 220);
        check(screen.release(70, 220).kind == bmh::ui::ClubInfoRequestKind::none,
              "release on a different button cannot dispatch");
        screen.press(10, 220);
        const auto toggle = screen.release(10, 220);
        check(toggle.kind == bmh::ui::ClubInfoRequestKind::toggle_view, "same-button press/release toggles view");
        auto view_b = projection(true); view_b.view = bmh::ui::ClubInfoView::data_b;
        bmh::ui::ClubInfoScreen screen_b(data, std::move(view_b));
        check(screen_b.render().pixels != image_a.pixels, "data A and B render different Source columns");
        screen.press(130, 220);
        const auto category = screen.release(130, 220);
        check(category.kind == bmh::ui::ClubInfoRequestKind::change_category && category.requested_category == 2,
              "category button returns an owned mutation request");
        check(screen.move(20, 30) && screen.hovered_row() == 0, "name hover selects actual squad row");
        check(screen.render().pixels != image_a.pixels, "hover redraw changes selected name color");

        bmh::ui::ClubInfoScreen simulated(data, projection(false));
        const auto simulation = simulated.render();
        check(simulation.width == 320 && simulation.height == 240, "simulated-club frame dimensions");
        check(!simulated.button_at(61, 220) && !simulated.button_at(130, 220) &&
              simulated.button_at(1, 220) == bmh::ui::ClubInfoButton::category,
              "simulated team exposes only its icon control");
        simulated.press(1, 220);
        check(simulated.release(1, 220).kind == bmh::ui::ClubInfoRequestKind::change_category,
              "simulated-team category remains a controller request");

        const auto exe = bmh::load_le(bmh::ui::asset_path(data, "BMH.exe"));
        bmh::GameState game;
        game.memory.add_block(::bmh::layout::TextGradientDisabled, 1); game.memory.set8(::bmh::layout::TextGradientDisabled, 0);
        game.memory.add_block(::bmh::layout::PlainBackground, 1); game.memory.set8(::bmh::layout::PlainBackground, 0);
        game.team_morale.assign(138, 0); game.team_morale[0] = 0x40;
        game.squad_slots.assign(24, bmh::kEmptySlot); game.squad_slots[0] = 0x8000;
        game.club_ids = {0}; game.club_names = {'F','C',' ','T','E','S','T',0};
        game.player_name_offsets = {0}; game.player_names = {'N','E','U','E','R',0};
        game.players.resize(1); game.players[0].w364 = 0x12345678;
        game.player_cards = {7}; game.player_bonus = {3};
        bmh::ClubInfoEntry snapshot; snapshot.displayed_slots.assign(24, bmh::kEmptySlot); snapshot.displayed_slots[0] = 0x8000;
        const auto owned = bmh::ui::club_info_from_game(game, 0, snapshot, exe, {});
        check(owned.runtime_team == 0 && owned.original_club_name == "FC TEST" && owned.category == 1,
              "builder uses requested runtime club identity and current category");
        check(owned.players.size() == 24 && owned.players[0].player_id == 0 &&
              owned.players[0].displayed_slot == snapshot.displayed_slots[0] &&
              owned.players[0].original_name == "NEUER" && owned.players[0].cards == 7,
              "builder consumes the pre-cleanup slot snapshot and owns player data");
        game.players[0].w364 = 0; game.player_names[0] = 'X'; game.club_names[0] = 'X';
        check(owned.players[0].attributes.w364 == 0x12345678 && owned.players[0].original_name == "NEUER" &&
              owned.original_club_name == "FC TEST", "projection remains owned after live state changes");
        game.players[0].w364 = 0x12345678; game.player_names[0] = 'N'; game.club_names[0] = 'F';
        game.manager_teams = {0}; game.tactics.assign(0x840, 0);
        game.memory.add_block(::bmh::layout::Career_CareerManagersEnd_RuntimeField, 0x18); game.memory.add_block(::bmh::layout::ClubInfo_FormatError_RuntimeField2, 0x0c);
        game.memory.add_block(::bmh::layout::ClubInfo_FormatError_RuntimeField, 0x420); game.memory.add_block(::bmh::layout::Career_RemoveJoinLoans_RuntimeField, 800);
        game.memory.add_block(::bmh::layout::OfferSlots, 16);
        for (int i = 0; i < 12; ++i) game.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField + uint32_t(i) * 2, 0x0fff);
        game.memory.set16(::bmh::layout::Career_CareerManagersEnd_RuntimeField, 0); game.memory.set8(::bmh::layout::ClubInfo_FormatError_RuntimeField2, 0);
        game.memory.set32(::bmh::layout::OfferSlots, 0xffffffff);
        const auto with_loan = bmh::ui::club_info_from_game(game, 0, snapshot, exe, {});
        const auto& borrowed = with_loan.players.back();
        check(with_loan.players.size() == 24 && borrowed.borrowed && borrowed.player_id == 0 &&
              borrowed.row == 23 && borrowed.rendered_row == 1 && borrowed.physical_row == -1 && borrowed.human_loan_index == 0 &&
              borrowed.manager_board.has_value(), "borrowed row owns displayed, physical, loan and board identities");
        std::cout << "ClubInfo native asset and routing tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
