// SPDX-License-Identifier: GPL-3.0-only
#include "ui/mailbox_screen.hpp"

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#include "game/messages.hpp"
#include "game/state.hpp"
#include "ui/setup.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
void rejected(const std::function<void()>& action) {
    try { action(); } catch (const FormatError&) { return; }
    throw std::runtime_error("invalid mailbox screen state was accepted");
}
void expect_digit(const Image& rendered, const Image& atlas, int digit, int x, const char* why) {
    for (int y = 0; y < 5; ++y) for (int xx = 0; xx < 4; ++xx) {
        const auto source_at = size_t(0xca + y) * size_t(atlas.width) + size_t(0x27 + digit * 4 + xx);
        const auto target_at = size_t(45 + y) * size_t(rendered.width) + size_t(x + xx);
        const uint8_t source = atlas.pixels.at(source_at);
        if (source != 0) check(rendered.pixels.at(target_at) == source, why);
    }
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "mailbox screen tests require original data");
        const std::filesystem::path data = argv[1];
        GameState game;
        game.manager_teams = {12, 13};
        game.mailbox.assign(kMailboxEntries * kMailboxEntry, 0);
        for (int i = 0; i < 4; ++i) post_composed(game, 0, i == 3 ? "Gr}~e aus M}nchen" : "Unit notice");
        game.mailbox[3 * kMailboxEntry + 4] = 1;
        post_composed(game, 1, "Nur Manager eins");
        const auto bytes = game.mailbox;
        const auto owned = mailbox_state_from_game(game, 0);
        Image parent(320, 240, 0x46);
        MailboxScreen screen(data, owned, "Jürgen", parent);
        check(screen.page_count() == 2 && screen.page() == 1 &&
              screen.state_json().find("\"visible_entries\":[3]") != std::string::npos,
              "new messages open on the last original three-article page and filter by recipient");
        auto rendered = screen.render();
        const auto atlas = load_vga(asset_path(data / "KB", "32.VGA"));
        check(rendered.at(51, 22) == atlas.pixels[0] && rendered.at(0, 0) == 0x46 &&
              rendered.at(184, 53) == 0 && rendered.at(54, 120) == 0,
              "original magazine popup position, dividers and underlying desk are retained");
        const auto font = load_font(asset_path(data / "F", "BMH3.FNT"));
        Image expected = rendered;
        // First displayed article starts at x55 / baseline61, body baseline83.
        // Re-draw with an independent known Original-byte string; UTF-8 must not reach Font::draw.
        font.draw(expected, 55, 83 - font.baseline, "Gr}~e aus M}nchen", 0xf3);
        for (int y = 83 - font.baseline; y < 83 - font.baseline + font.height; ++y)
            for (int x = 55; x < 182; ++x)
                check(expected.at(x, y) == rendered.at(x, y), "kept body uses correct UTF-8 conversion and baseline");
        const std::string date = "01.07.2026";
        font.draw(expected, 182 - font.text_width(date), 61 - font.baseline, date, 0xf3);
        for (int y = 61 - font.baseline; y < 61 - font.baseline + font.height; ++y)
            for (int x = 182 - font.text_width(date); x < 182; ++x)
                check(expected.at(x, y) == rendered.at(x, y), "message date keeps the original calendar epoch and right alignment");
        game.mailbox[3 * kMailboxEntry + 10] = 'X';
        check(screen.render().pixels == rendered.pixels && owned.messages.back().text == "Grüße aus München",
              "screen owns content independently of later source writes");
        check(screen.click(60, 205) && screen.page() == 0 &&
              screen.state_json().find("\"visible_entries\":[0,1,2]") != std::string::npos,
              "original left button moves to the earlier three-message page");
        check(screen.click(260, 205) && screen.page() == 1 && screen.key("RIGHT") && screen.page() == 0,
              "original right button and local arrow navigation wrap pages");
        check(screen.click(85, 205) && screen.state_json().find("\"action_mode\":1") != std::string::npos && !screen.pending(),
              "keep button selects a local mode without changing a message");
        check(screen.state().messages[3].kept_for_manager && bytes[3 * kMailboxEntry + 4] == 1,
              "keep flag remains owned and unchanged by input");
        check(!screen.click(-1, 0) && !screen.key("ENTER"), "unrelated input never submits an action");
        check(screen.click(60, 70) && screen.pending() &&
              screen.pending()->expected_entry == owned.messages[0].raw_entry &&
              screen.pending()->entry_index == 0 && screen.pending()->kind == MailboxActionKind::toggle_keep,
              "article click binds action to the exact owned physical entry");
        check(!screen.key("ESC") && !screen.click(110, 205), "pending actions own modal input");
        auto bad_ack = owned; bad_ack.revision = 1; bad_ack.messages[0].entry_index = 41;
        bool ack_rejected = false;
        try { screen.acknowledge(bad_ack); } catch (const FormatError&) { ack_rejected = true; }
        check(ack_rejected && screen.pending() && screen.state().revision == 0,
              "malformed acknowledgement preserves the pending command and old projection");
        screen.reject("Abgelehnt");
        check(!screen.pending() && screen.state().revision == 0, "rejection restores usable local input");
        check(screen.click(0, 0, true) && screen.close_requested() && !screen.key("RIGHT"),
              "original right-button close does not fall through to the desk");

        // Exercise the projector -> screen boundary for empty text and the original 0xa0
        // word separator. These bytes must not leak as invalid UTF-8 into the screen.
        GameState encoding; encoding.manager_teams = {12};
        encoding.mailbox.assign(kMailboxEntries * kMailboxEntry, 0);
        post_composed(encoding, 0, "");
        post_composed(encoding, 0, std::string("K\xa0}nig"));
        const auto encoding_bytes = encoding.mailbox;
        const auto encoded = mailbox_state_from_game(encoding, 0);
        check(encoded.messages.size() == 2 && encoded.messages[0].text.empty() &&
              encoded.messages[1].text == "K ünig", "empty text and 0xa0 separator project to owned UTF-8");
        MailboxScreen encoding_screen(data, encoded, "Jürgen", parent);
        check(encoding_screen.render().pixels == encoding_screen.render().pixels &&
              encoding_screen.state().messages[1].text == "K ünig" && encoding.mailbox == encoding_bytes,
              "screen accepts projector UTF-8 and never mutates the mailbox");

        // Manager filtering remains per recipient bit even for the screen constructor.
        const auto only_manager_one = mailbox_state_from_game(game, 1);
        check(only_manager_one.messages.size() == 1 && only_manager_one.messages[0].entry_index == 4,
              "second manager sees only its own message");
        MailboxScreen manager_one_screen(data, only_manager_one, "Jürgen", parent);
        check(manager_one_screen.page_count() == 1 && manager_one_screen.state().messages[0].entry_index == 4,
              "manager-specific projection is retained by the screen");

        GameState seven; seven.manager_teams = {12};
        seven.mailbox.assign(kMailboxEntries * kMailboxEntry, 0);
        for (int i = 0; i < 7; ++i) post_composed(seven, 0, "Nachricht " + std::to_string(i));
        const auto seven_bytes = seven.mailbox;
        MailboxScreen seven_screen(data, mailbox_state_from_game(seven, 0), "SIM", parent);
        check(seven_screen.page_count() == 3 && seven_screen.page() == 2 &&
              seven_screen.state_json().find("\"visible_entries\":[6]") != std::string::npos,
              "seven messages occupy three pages and initially show the final page");
        check(seven_screen.click(51, 198) && seven_screen.page() == 1 &&
              seven_screen.click(72, 223) && seven_screen.page() == 0,
              "both inclusive corners of the original previous-page button work");
        check(!seven_screen.click(50, 198) && !seven_screen.click(73, 223) && seven_screen.page() == 0,
              "points just outside original previous-page bounds are inert");
        check(seven_screen.click(250, 198) && seven_screen.page() == 1 &&
              seven_screen.click(270, 223) && seven_screen.page() == 2,
              "both inclusive corners of the original next-page button work");
        check(!seven_screen.click(249, 198) && !seven_screen.click(271, 223) && seven_screen.page() == 2,
              "points just outside original next-page bounds are inert");
        check(seven.mailbox == seven_bytes && seven_screen.state().messages.size() == 7,
              "page navigation does not change saved messages");

        GameState forty; forty.manager_teams = {12};
        forty.mailbox.assign(kMailboxEntries * kMailboxEntry, 0);
        for (int i = 0; i < 40; ++i) post_composed(forty, 0, "Meldung " + std::to_string(i));
        MailboxScreen forty_screen(data, mailbox_state_from_game(forty, 0), "SIM", parent);
        check(forty_screen.page_count() == 14 && forty_screen.page() == 13,
              "forty messages span fourteen pages and open at the last page");
        for (int i = 0; i < 4; ++i) check(forty_screen.key("LEFT"), "navigate from page fourteen to page ten");
        check(forty_screen.page() == 9, "page ten is selected using local page navigation");
        const auto ten_pixels = forty_screen.render();
        // legacy routine uses 4px glyphs with 5px spacing, centered on x=256.
        expect_digit(ten_pixels, atlas, 1, 252, "legacy routine tens digit x for page ten");
        expect_digit(ten_pixels, atlas, 0, 257, "legacy routine units digit x for page ten");
        check(forty_screen.key("RIGHT") && forty_screen.page() == 10,
              "page eleven navigation remains local and bounded");
        for (int i = 0; i < 10; ++i) check(forty_screen.key("LEFT"), "return to page one");
        check(forty_screen.page() == 0, "page one is reachable");
        expect_digit(forty_screen.render(), atlas, 1, 255, "legacy routine single digit x for page one");

        GameState paragraph;paragraph.manager_teams={12};
        paragraph.mailbox.assign(kMailboxEntries*kMailboxEntry,0);
        post_composed(paragraph,0,"WWWW WWWW WWWW WWWW WWWW");
        auto paragraph_state=mailbox_state_from_game(paragraph,0);
        MailboxScreen filled_article(data,paragraph_state,"SIM",parent);
        paragraph_state.messages[0].text.clear();
        auto expected_article=MailboxScreen(data,paragraph_state,"SIM",parent).render();
        // width127: four24px words + three2px spaces =102. Source inserts
        //13 spaces in round-robin order, giving6/5/5 spaces and width128.
        font.draw(expected_article,55,83-font.baseline,"WWWW      WWWW     WWWW     WWWW",0);
        font.draw(expected_article,55,90-font.baseline,"WWWW",0);
        check(filled_article.render().pixels==expected_article.pixels,
              "whole mailbox matches independent Source block spacing, inclusive width+1 and unstretched final line");
        auto hostile_state=mailbox_state_from_game(paragraph,0);
        hostile_state.messages[0].text=std::string(149,'W');
        auto bounded_article=MailboxScreen(data,hostile_state,"SIM",parent).render();
        for(int y=78;y<84;++y)for(int x=184;x<320;++x)
            check(bounded_article.at(x,y)==expected_article.at(x,y),
                  "unbreakable malformed article cannot hang or write through the neighboring article");

        MailboxState empty; empty.manager_slot = 0;
        MailboxScreen empty_screen(data, empty, "SIM", parent);
        check(empty_screen.page_count() == 1 && empty_screen.page() == 0 &&
              empty_screen.state().messages.empty() && empty_screen.render().pixels.size() == 320 * 240,
              "empty inbox renders without posting synthetic messages into the engine");
        auto invalid = owned; invalid.messages[0].recipient_mask = 2;
        rejected([&] { MailboxScreen bad(data, invalid, "SIM", parent); });
        invalid = owned; invalid.messages[1].entry_index = 0;
        rejected([&] { MailboxScreen bad(data, invalid, "SIM", parent); });
        invalid = owned; invalid.manager_slot = 4;
        rejected([&] { MailboxScreen bad(data, invalid, "SIM", parent); });
        std::cout << "Owned Original-asset mailbox pages passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
