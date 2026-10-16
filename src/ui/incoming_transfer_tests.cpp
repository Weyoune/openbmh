// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/incoming_transfer.hpp"
#include "ui/source_notice_test_helpers.hpp"

#include <cstdio>
#include <vector>

#include "game/state.hpp"

using namespace bmh::ui;

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

IncomingTransferDecisionState fixture() {
    IncomingTransferDecisionState s;
    s.list_entry = 8;
    s.player = 4;
    s.seller = 10;
    s.viewer_manager = 0;
    s.player_name = "Muller";
    s.seller_name = "Bayern";
    s.negotiator_name = "Berater";
    s.age = 25;
    s.position = 2;
    s.asking_fee = 1234567;
    s.asking_monthly_salary = 42000;
    s.quoted_fee = 1000000;
    s.quoted_monthly_salary = 38000;
    s.quoted_years = 3;
    s.offers[0] = {0, 12, 1, 900000, 35000, 1100000, 40000, 36, false, true, true, {1,2,3,4,5,6,7}};
    // A hole is intentional: the chooser must return stable raw slots, not visual indices.
    s.offers[2] = {2, 15, -1, 750000, 30000, 900000, 32000, 24, true, true, false, {}};
    s.buyer_names[0] = "Valencia";
    s.buyer_names[2] = "Dortmund";
    return s;
}

bmh::GameState game_fixture() {
    bmh::GameState game;
    game.manager_teams = {0};
    game.manager_records.resize(5 * 0xb0c);
    game.manager_records[0xa8] = 'C'; game.manager_records[0xa9] = 'o';
    game.manager_records[0xaa] = 'a'; game.manager_records[0xab] = 'c';
    game.manager_records[0xac] = 'h';
    game.manager_records[0x221] = 1;
    game.players = {{"Player", 80u | (90u << 7) | (85u << 14), 0, (9u << 21) | 2u}};
    game.player_name_offsets = {0};
    game.player_names = {'P','l','a','y','e','r',0};
    game.club_ids = {0};
    game.club_names = {'B','a','y','e','r','n',0};
    game.club_heap_78 = {0,0};
    game.memory.add_block(::bmh::layout::CurrencyMultiplier, 8);
    game.memory.set32(::bmh::layout::CurrencyMultiplier, 2);
    game.memory.set32(::bmh::layout::CurrencyDivisor, 4);
    return game;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        auto game = game_fixture();
        IncomingTransferDecisionView view;
        view.valid = true; view.list_entry = 8; view.player = 0; view.seller = 0;
        view.seller_manager = 0; view.viewer_manager = 0; view.preferred_bid_index = 0;
        view.has_preferred_bid = true; view.preferred_bid_passed_random_check = true;
        view.quoted_fee = 123456; view.quoted_monthly_salary = 45000; view.quoted_years = 3;
        view.offers[0] = {0, 0, -1, 120000, 42000, 150000, 46000, 36, false, true, true, {}};
        const auto projected = incoming_transfer_decision_from_game(game, view);
        check(projected.negotiator_name == "Bayern" && projected.seller_badge_sprite == 0,
              "Source club identity supplies negotiator and seller badge when host omits a label");
        check(projected.currency_numerator == 2 && projected.currency_denominator == 4,
              "currency conversion factors are copied from current GameState memory");
        auto free_game = game_fixture();
        auto source_texts = bmh::load_textd(std::filesystem::path(argv[1]) / "TEXTD.BLK");
        free_game.texts = &source_texts;
        auto free_view = view;
        free_view.seller = bmh::kNoTeam;
        free_view.seller_manager = -1;
        const auto free_projected = incoming_transfer_decision_from_game(free_game, free_view);
        check(free_projected.negotiator_name == source_texts[925] && free_projected.seller_badge_sprite == -1,
              "player-pool seller uses the original adviser label without indexing the club table");

        free_view.seller = 0x0fff; // Actual transfer-list pool token, legacy routine.
        const auto raw_free = incoming_transfer_decision_from_game(free_game,free_view);
        check(raw_free.negotiator_name==source_texts[925] && raw_free.seller_name.empty() &&
              raw_free.seller_badge_sprite==-1,
              "raw Source 0xfff pool seller resolves without indexing a club or national-team token");

        auto state = fixture();
        IncomingTransferScreen screen(argv[1], state);
        const auto frame = screen.render();
        check(frame.width == 320 && frame.height == 240 && frame.pixels.size() == 320u * 240u,
              "renders original 320x240 canvas with KB129 and KB31");
        check(state.title == IncomingTransferTitle::decision,
              "incoming transfer retains the Source TEXTD 229 decision caption by default");
        auto manager_title_state = state;
        manager_title_state.title = IncomingTransferTitle::player_purchase;
        IncomingTransferScreen purchase_title(argv[1], manager_title_state);
        bool title_pixels_differ = false;
        const auto purchase_frame = purchase_title.render();
        for (int y = 13; y < 23; ++y) for (int x = 70; x < 250; ++x)
            title_pixels_differ |= frame.pixels[size_t(y) * 320 + size_t(x)] !=
                                   purchase_frame.pixels[size_t(y) * 320 + size_t(x)];
        check(title_pixels_differ,
              "Source title projection changes only the displayed Original TEXTD caption");
        auto broad_portrait = state;
        broad_portrait.source_outgoing_editor_context = true;
        broad_portrait.manager_portrait = 12;
        IncomingTransferScreen portrait_context(argv[1], broad_portrait);
        check(portrait_context.render().width == 320,
              "explicit KB31 context accepts portrait IDs used by Source notices but not its static header art");
        auto game_portrait = game_fixture();
        game_portrait.manager_records[0x221] = 12;
        auto outgoing_projection = view;
        outgoing_projection.source_outgoing_editor_context = true;
        check(incoming_transfer_decision_from_game(game_portrait, outgoing_projection).manager_portrait == 12,
              "owner can project Source-valid portrait 12 for the outgoing KB31 notice context");
        bool ordinary_portrait_rejected = false;
        try { (void)incoming_transfer_decision_from_game(game_portrait, view); }
        catch (const bmh::FormatError&) { ordinary_portrait_rejected = true; }
        check(ordinary_portrait_rejected,
              "portrait range expansion is limited to explicit KB31 or pending Source context");
        auto pool_screen_state = state;
        pool_screen_state.seller = 0x0fff;
        pool_screen_state.seller_badge_sprite = -1;
        IncomingTransferScreen pool_screen(argv[1], pool_screen_state);
        const auto pool_frame = pool_screen.render();
        const auto widgets = bmh::load_vga(std::filesystem::path(argv[1]) / "KB" / "0.VGA");
        auto pool_base = bmh::load_vga(std::filesystem::path(argv[1]) / "KB" / "129.VGA");
        pool_base.blit(bmh::load_vga(std::filesystem::path(argv[1]) / "KB" / "31.VGA"), 0, 19);
        bool monitor_ink = false, pool_tile_exact = true;
        for (int y = 0; y < 38; ++y) for (int x = 0; x < 38; ++x) {
            const auto src = widgets.pixels[size_t(y) * size_t(widgets.width) + size_t(x + 8)];
            const auto expected = src == 0xff
                ? pool_base.pixels[size_t(y + 51) * size_t(pool_base.width) + size_t(x + 102)]
                : src;
            monitor_ink |= src != 0xff;
            pool_tile_exact &= pool_frame.pixels[size_t(y + 51) * size_t(pool_frame.width) + size_t(x + 102)] == expected;
        }
        bool frame_exact = true;
        for (int x = 101; x < 140; ++x)
            frame_exact &= pool_frame.pixels[size_t(50) * 320 + size_t(x)] == 0x46 &&
                           pool_frame.pixels[size_t(89) * 320 + size_t(x)] == 0x40;
        for (int y = 50; y < 89; ++y)
            frame_exact &= pool_frame.pixels[size_t(y) * 320 + 101] == 0x46 &&
                           pool_frame.pixels[size_t(y) * 320 + 140] == 0x40;
        frame_exact &= pool_frame.pixels[size_t(50) * 320 + 140] == 0x40 &&
                       pool_frame.pixels[size_t(89) * 320 + 101] == 0x40;
        check(monitor_ink, "KB0 monitor crop has visible pixels");
        check(pool_tile_exact, "pool TV crop matches KB0 with Source transparency");
        check(frame_exact, "pool TV fallback has Source legacy routine border colors and bounds");
        auto neutral_badge_state = state;
        neutral_badge_state.seller = 100;
        neutral_badge_state.seller_team_token = 100;
        neutral_badge_state.seller_badge_sprite = 0; // The raw Source fallback precedes selector lookup.
        IncomingTransferScreen neutral_badge(argv[1], neutral_badge_state);
        const auto neutral_frame = neutral_badge.render();
        check(neutral_frame.pixels[size_t(50) * 320 + 101] == 0x46 &&
              neutral_frame.pixels[size_t(89) * 320 + 140] == 0x40,
              "Source seller tokens 90..137 use legacy routine's framed neutral fallback before badge lookup");
        auto unused_selector_state = neutral_badge_state;
        unused_selector_state.seller_badge_sprite = 511;
        IncomingTransferScreen unused_selector(argv[1], unused_selector_state);
        check(unused_selector.render().pixels == neutral_frame.pixels,
              "neutral raw seller fallback does not consult or load its unused ba378 selector");
        auto selector_state = state;
        selector_state.seller = 20;
        selector_state.seller_team_token = 20;
        selector_state.seller_badge_sprite = 92; // A real ClubBadgeSelectors selector in the nominal fallback range.
        IncomingTransferScreen selector_badge(argv[1], selector_state);
        const auto selector_frame = selector_badge.render();
        const auto selector_atlas = bmh::load_vga(std::filesystem::path(argv[1]) / "W" / "11.VGA");
        bool selector_pixels_match = true;
        size_t selector_ink = 0;
        for (int y = 0; y < 38; ++y) for (int x = 0; x < 38; ++x) {
            const auto source = selector_atlas.pixels[size_t(y) * size_t(selector_atlas.width) + size_t(x + 4 * 38)];
            selector_ink += source != 0;
            selector_pixels_match &= selector_frame.pixels[size_t(y + 51) * 320 + size_t(x + 102)] == source;
        }
        check(selector_ink > 0 && selector_pixels_match,
              "ordinary raw seller with ba378 selector 92 uses W11 artwork rather than neutral fallback");
        auto animated = fixture();
        animated.offers[1] = {1, 13, -1, 710000, 31000, 800000, 33000, 12, false, true, true, {}};
        animated.buyer_names[1] = "Hamburg";
        IncomingTransferScreen outcome(argv[1], animated);
        const auto base_image = outcome.render();
        outcome.begin_outcome(3, 2, 0);
        const auto blink0 = outcome.outcome_frame_at(0);
        check(blink0.marks[0] == IncomingTransferOutcomeMark::blinking_question &&
              blink0.blink_phase == 0 && !blink0.complete && outcome.outcome_duration_ms() == 8904,
              "default Source delay models eight capture-calibrated 371 ms blink phases per resolved raw row");
        const auto blink1 = outcome.outcome_frame_at(371);
        check(blink1.marks[0] == IncomingTransferOutcomeMark::blinking_question &&
              blink1.blink_phase == 1 && blink0.colors[0] != blink1.colors[0],
              "question marker alternates Source palette colors by phase");
        const auto middle = outcome.outcome_frame_at(2968);
        check(middle.marks[0] == IncomingTransferOutcomeMark::rejected &&
              middle.marks[1] == IncomingTransferOutcomeMark::blinking_question &&
              !middle.complete,
              "preceding raw rows settle to NEIN before the next row blinks");
        const auto settled = outcome.outcome_frame_at(8904);
        check(settled.complete && settled.marks[0] == IncomingTransferOutcomeMark::rejected &&
              settled.marks[1] == IncomingTransferOutcomeMark::rejected &&
              settled.marks[2] == IncomingTransferOutcomeMark::accepted &&
              settled.colors[2] == 0x27,
              "accepted raw row becomes yellow OK after preceding refusals");
        const auto blinking_image = outcome.render_at_elapsed(0);
        const auto settled_image = outcome.render_at_elapsed(8904);
        check(blinking_image.pixels != settled_image.pixels,
              "passive marker animation changes only the rendered frame over elapsed time");
        const auto outcome_state = outcome.state_json();
        check(!outcome.click(10, 150, true, 8903) && !outcome.outcome_dismissed() &&
              outcome.state_json() == outcome_state,
              "result cannot be dismissed before animation completion and rendering is state-pure");
        check(!outcome.click(10, 150, true, 8904) && outcome.outcome_dismissed() &&
              outcome.outcome_frame_at(8904).marks[2] == IncomingTransferOutcomeMark::none &&
              outcome.render_at_elapsed(8904).pixels == base_image.pixels,
              "right click dismisses the completed passive result window");
        auto confirm_state=animated;
        confirm_state.viewer_manager=0; confirm_state.manager_portrait=1; confirm_state.badge_sprite=0;
        confirm_state.viewer_name="SyntheticViewer";
        const auto original_texts = bmh::load_textd(std::filesystem::path(argv[1]) / "TEXTD.BLK");
        IncomingTransferScreen manual_decline(argv[1], confirm_state);
        manual_decline.show_confirmation(::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField, settled_image);
        const auto decline_text = original_texts[(::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField - ::bmh::layout::TextPointerTable) / 4];
        check(manual_decline.state().notice_text == decline_text &&
              manual_decline.notice_active() &&
              manual_decline.render().pixels != settled_image.pixels,
              "manual seller refusal resolves format field text and draws its portrait confirmation over the result frame");
        IncomingTransferScreen confirm(argv[1],confirm_state);
        confirm.show_confirmation(::bmh::layout::PendingOffer_FormatError_ExecutableField2,settled_image);
        const auto success_text = original_texts[(::bmh::layout::PendingOffer_FormatError_ExecutableField2 - ::bmh::layout::TextPointerTable) / 4];
        bmh::ui::test::expect_notice_symbols(confirm.render(),bmh::load_vga(std::filesystem::path(argv[1])/"KB/0.VGA"));
        check(confirm.state().notice_text == success_text && confirm.notice_active() &&
              confirm.render().pixels != settled_image.pixels &&
              confirm.render().pixels != manual_decline.render().pixels,
              "manual accepted sale resolves separate format field result text in the same original portrait box");
        check(!confirm.key("ESC") && !confirm.dismiss_notice() && !confirm.confirmation_dismissed(),
              "confirmation waits for its original KLICK button instead of keyboard or timeout");
        check(!confirm.click(94,166) && !confirm.click(95,166,true) && !confirm.confirmation_dismissed(),
              "only a left click inside KLICK can acknowledge the confirmation");
        check(!confirm.click(95,166) && confirm.confirmation_dismissed() && !confirm.answer(),
              "KLICK acknowledges without submitting a new transfer decision");
        IncomingTransferScreen confirm_edge(argv[1],confirm_state);
        confirm_edge.show_confirmation(::bmh::layout::PendingOffer_FormatError_ExecutableField2,settled_image);
        check(!confirm_edge.click(221,176) && !confirm_edge.confirmation_dismissed(),
              "source button rejects the first pixel beyond its inclusive right edge");
        check(!confirm_edge.click(220,176) && confirm_edge.confirmation_dismissed(),
              "Source legacy routine includes right/bottom descriptor boundaries for Klick!");
        IncomingTransferScreen inert(argv[1],animated); inert.begin_outcome(3,2,0);
        check(!inert.key("ESC") && !inert.answer(), "passive outcome never turns ESC into a seller decline");
        auto accelerated = IncomingTransferScreen(argv[1], animated);
        accelerated.begin_outcome(3, 2, 1);
        const auto instant = accelerated.outcome_frame_at(0);
        check(instant.complete && instant.marks[0] == IncomingTransferOutcomeMark::rejected &&
              instant.marks[2] == IncomingTransferOutcomeMark::accepted &&
              accelerated.outcome_duration_ms() == 0,
              "nonzero Source speed setting removes phase delays without changing outcome order");
        auto refused = IncomingTransferScreen(argv[1], animated);
        refused.begin_outcome(3, -1, 1);
        const auto all_refused = refused.outcome_frame_at(0);
        check(all_refused.complete && all_refused.marks[0] == IncomingTransferOutcomeMark::rejected &&
              all_refused.marks[2] == IncomingTransferOutcomeMark::rejected,
              "fully refused outcome marks every supplied raw row NEIN");
        bool invalid_outcome_rejected = false;
        try { refused.begin_outcome(2, 2, 0); } catch (const bmh::FormatError&) { invalid_outcome_rejected = true; }
        check(invalid_outcome_rejected, "outcome rejects accepted slot outside the resolved row count");
        check(screen.state_json().find("\"preferred_bid_passed_random_check\"") != std::string::npos,
              "JSON names the Source W(90,110) outcome without conflating eligibility");
        check(!screen.click(3, 150) && !screen.click(191, 150) && !screen.click(4, 142),
              "chooser hitbox rejects points outside Source rectangle");
        check(screen.click(4, 143) == 0, "first source row returns raw bid slot zero");
        IncomingTransferScreen right_edge(argv[1], state);
        check(right_edge.click(190, 159) == 0, "Source's inclusive right edge keeps the first raw row selectable");

        IncomingTransferScreen gap(argv[1], state);
        check(!gap.click(10, 160), "empty raw slot one cannot be selected");
        check(gap.click(189, 177) == 2, "third visual row returns raw bid slot two");
        check(!gap.click(10, 211), "bottom boundary cannot become an out-of-range fifth slot");

        auto unsupported_state = state;
        unsupported_state.offers[2].transaction_supported = false;
        IncomingTransferScreen unsupported(argv[1], unsupported_state);
        check(!unsupported.click(10, 177), "Source-visible but unsupported transaction cannot be submitted");

        IncomingTransferScreen decline(argv[1], state);
        check(decline.click(200, 100, true) == -1, "right click chooses the Source decline response");
        check(!decline.click(10, 143), "answer is one-shot until Core returns a new view");

        state.notice_text = "Angebot kann nicht angenommen werden";
        IncomingTransferScreen warning(argv[1], state);
        check(warning.notice_active() && !warning.click(10, 143) && !warning.answer(),
              "first input dismisses Source warning without choosing a bid");
        check(!warning.notice_active() && warning.click(10, 143) == 0,
              "bid selection becomes available after warning dismissal");
        state.notice_text = "Angebot kann nicht angenommen werden";
        IncomingTransferScreen keyboard_warning(argv[1], state);
        check(!keyboard_warning.key("ESC") && !keyboard_warning.notice_active() && !keyboard_warning.answer(),
              "keyboard input closes only the notice, not the decision");
        state.notice_text.clear(); state.notice_text_address = ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3;
        IncomingTransferScreen address_warning(argv[1], state);
        check(address_warning.notice_active() && address_warning.state_json().find("notice_text") != std::string::npos,
              "Source retry notices resolve through TEXTD address indices");
        IncomingTransferScreen escape(argv[1], fixture());
        check(escape.key("ESC") == -1, "escape declines without mutating the offer snapshot");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "incoming transfer test exception: %s\n", e.what());
        ++failures;
    }
    return failures ? 1 : 0;
}
