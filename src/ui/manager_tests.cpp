// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <stdexcept>
#include <numeric>

#include "ui/flow.hpp"

using namespace bmh::ui;
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("ui_manager_tests requires original data directory");
        const std::filesystem::path data = argv[1];
        ManagerScreen s(data, 0);
        check(s.choice().master_club == 0 && s.choice().portrait == 0, "default available identity");
        check(s.render().pixels.size() == 320 * 240, "original manager framebuffer");
        check(!s.click(320, 10, true) && !s.text("ignored"), "invalid point and inactive text input");
        for (int i = 0; i < 5; ++i) s.click(30, 140);
        for (int i = 0; i < 3; ++i) s.click(220, 140);
        check(s.choice().start_league == 0 && s.choice().strength == 1, "league and strength wrap");
        check(s.click(30, 65) && s.choice().name.empty(), "starting edit clears name like original");
        check(s.text("BMHLAB") && s.key("BACKSPACE") && s.choice().name == "BMHLA", "text and backspace");
        check(!s.click(30, 140) && !s.click(5, 225, true), "name edit consumes modal input until Enter");
        auto before = s.choice();
        bool rejected = false;
        try { s.text("Aé"); } catch (const bmh::FormatError&) { rejected = true; }
        check(rejected && s.choice() == before, "unsupported UTF-8 is rejected atomically");
        s.key("ENTER"); s.click(30, 65); s.text(std::string(30, 'I')); s.key("ENTER");
        check(s.choice().name == std::string(25, 'I'), "observed 25-character narrow-name limit");
        s.click(30, 65); s.text(std::string(30, 'W')); s.key("ENTER");
        const auto small = bmh::load_font(asset_path(data / "F", "BMH3.FNT"));
        check(small.text_width(s.choice().name) < 65 && s.choice().name.size() < 25, "pixel width also limits name");
        check(utf8_to_original("ÄÖÜäöüß") == "[\\]{|}~", "original German glyph encoding");
        check(original_to_utf8("UNIT [\\] CLUB") == "UNIT ÄÖÜ CLUB", "database string to JSON encoding");

        check(s.click(200, 95), "open club picker from badge");
        s.click(100, 15); s.click(5, 225, true);
        check(s.choice().master_club == 1 && s.view() == ManagerView::selection, "club choice survives right-click close");
        auto chosen = s.choice();
        s.click(5, 225, true); check(!s.click(20, 20), "confirmation requires a button");
        s.click(210, 169, true);
        check(s.choice() == chosen && s.view() == ManagerView::selection, "No preserves choices with either mouse button");
        s.click(5, 225, true); s.click(90, 169);
        check(s.view() == ManagerView::accepted && !s.click(90, 169), "accepted choice cannot repeat");

        ManagerChoice first{0, "ONE", 0, 0, 1, 0};
        ManagerScreen second(data, 1, {first});
        check(second.choice().master_club == 1 && second.choice().portrait == 1, "next manager skips used defaults");
        check(!second.click(30, 165) && second.choice().portrait == 1, "used portrait is unavailable");
        second.click(200, 95); second.click(70, 25); second.click(5, 225, true);
        check(second.choice().master_club == 1, "used club is unavailable");
        second.click(200, 95); second.click(180, 188); second.click(70, 25); second.click(5, 225, true);
        check(second.choice().master_club == 138, "extra teams use master database indices, not visible cell IDs");
        auto duplicate = first; duplicate.slot = 1;
        rejected = false;
        try { ManagerScreen invalid(data, 2, {first, duplicate}); } catch (const bmh::FormatError&) { rejected = true; }
        check(rejected, "malformed prior choices rejected");
        SelectionContext short_context; short_context.club_order = {0};
        rejected = false;
        try { ManagerScreen invalid(data, 0, {}, short_context); } catch (const bmh::FormatError&) { rejected = true; }
        check(rejected, "incomplete context rejected before default club lookup");

        // A fully observable sequence for every supported setup count. No engine involved.
        for (int count = 1; count <= 4; ++count) {
            StartupFlow flow(data);
            flow.click(150, 215); // Disable national-team game explicitly.
            for (int i = 1; i < count; ++i) flow.click(260, 135);
            flow.click(5, 225, true);
            for (int i = 0; i < count; ++i) {
                check(flow.screen_name() == "manager_selection", "manager order reaches each selection");
                flow.click(5, 225, true);
                auto result = flow.click(90, 169);
                check(result.request == (i + 1 == count ? FlowRequest::new_career : FlowRequest::manager_confirmed), "per-manager initialization boundary precedes next selection");
                if (i + 1 < count) {
                    check(flow.pending_manager() && flow.screen_name() == "manager_pending", "wait for authoritative reordered clubs");
                    check(flow.click(90, 169).request == FlowRequest::none, "manager confirmation cannot repeat");
                    SelectionContext context; context.revision = i + 1; context.source = "test_fixture";
                    context.club_order.resize(168); std::iota(context.club_order.begin(), context.club_order.end(), 0);
                    auto stale = context; stale.revision = 0;
                    rejected = false;
                    try { flow.resume_manager_selection(stale); } catch (const bmh::FormatError&) { rejected = true; }
                    check(rejected && flow.pending_manager(), "stale context leaves pending state intact");
                    flow.resume_manager_selection(context);
                }
            }
            const auto* request = flow.pending_career();
            check(request && int(request->managers.size()) == count && !request->options.world && request->options.league, "handoff preserves options and manager count");
            for (int i = 0; i < count; ++i)
                check(request->managers[i].slot == i && request->managers[i].master_club == i && request->managers[i].portrait == i, "stable order and unique available choices");
            check(flow.click(90, 169).request == FlowRequest::none && !flow.text("ignored"), "pending engine request not emitted twice or modified");
            check(flow.render().pixels.size() == 320 * 240, "final view remains renderable");
        }
        SelectionContext reordered;
        reordered.club_order.resize(168); std::iota(reordered.club_order.begin(), reordered.club_order.end(), 0);
        std::swap(reordered.club_order[0], reordered.club_order[9]); reordered.revision = 1;
        ManagerScreen after_swap(data, 1, {first}, reordered);
        check(after_swap.choice().master_club == 9 && after_swap.choice().selection_club == 0,
              "observed first-manager club swap separates selection and master IDs");
        after_swap.click(200, 95); after_swap.click(230, 65); after_swap.click(5, 225, true);
        check(after_swap.choice().master_club == 9, "used master club remains locked after moving to another cell");
        std::puts("UI manager tests passed: modal state, names, master IDs, uniqueness, 1-4 managers, one-shot handoff");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1;
    }
}
