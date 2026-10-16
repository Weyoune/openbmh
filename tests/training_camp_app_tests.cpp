// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <stdexcept>
#include <algorithm>
#include <chrono>

#include "app/career_session.hpp"
#include "formats/le.hpp"
#include "game/save.hpp"
#include "ui/flow.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void content_lifecycle(const std::filesystem::path& data) {
    const auto exe = load_le(data / "BMH.exe");
    app::CareerSession live(data, 0x12345678);
    SetupOptions options;
    options.world = false;
    ManagerChoice choice;
    choice.name = "CampTest";
    std::string error;
    check(live.confirm_manager(options, choice, error), error.c_str());
    CareerInitializationResult initialized;
    check(live.finish(initialized, error), error.c_str());
    for (size_t r = 0; r < 8; ++r) {
        check(live.state().training_camp_base_prices[r] == int32_t(exe.u32(::bmh::layout::DeskActions_ConfigureTrainingCampData_RuntimeField2 + uint32_t(r * 4))),
              "new career installs the selected edition's resort prices");
        for (size_t q = 0; q < 4; ++q)
            check(live.state().training_camp_quality[r][q] == exe.u8(::bmh::layout::DeskActions_ConfigureTrainingCampData_RuntimeField + uint32_t(r * 4 + q)),
                  "new career installs the selected edition's resort qualities");
    }
    const auto native = save_stream(live.state());
    const auto expected_prices = live.state().training_camp_base_prices;
    const auto expected_quality = live.state().training_camp_quality;
    live.state().training_camp_base_prices.fill(123);
    for (auto& grades : live.state().training_camp_quality) grades.fill(1);
    check(save_stream(live.state()) == native, "runtime resort content never changes the Native save stream");
    const auto file = std::filesystem::temp_directory_path() / ("openbmh-camp-content-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".FBM");
    WatcomRandom serialize(731);
    write_savegame(file, make_savegame(live.state(), serialize));
    app::CareerSession reloaded(data, 731, file);
    std::filesystem::remove(file);
    check(reloaded.state().training_camp_base_prices == expected_prices &&
          reloaded.state().training_camp_quality == expected_quality,
          "fresh save loading restores resort content from the selected EXE, not defaults or save bytes");
    auto projected = training_camp_from_game(reloaded.state(), exe, 0, 1);
    check(projected.quality == expected_quality, "UI stars project the same table that booking effects use");
    if (exe.cstring(::bmh::layout::CurrencySuffixText) == " EU") {
        GameState defaults;
        check(expected_prices == defaults.training_camp_base_prices &&
              expected_quality == defaults.training_camp_quality,
              "German supported edition price and quality content stays byte-identical to the former constants");
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) throw std::runtime_error("training_camp_app_tests DATA [--content-only]");
        const std::filesystem::path data = argv[1];
        content_lifecycle(data);
        if (argc == 3) {
            check(std::string(argv[2]) == "--content-only", "unknown training camp test option");
            std::puts("training camp content: actual new-career/save-reload tables and Native isolation OK");
            return 0;
        }
        StartupFlow flow(data);
        flow.click(150, 215); // Disable world competition for the ordinary club desk.
        flow.click(5, 225, true); // Setup -> first manager.
        flow.click(5, 225, true); // Close the manager-name editor.
        const auto confirmed = flow.click(90, 169);
        check(confirmed.request == FlowRequest::new_career && flow.pending_career(), "UI confirms one manager");

        app::CareerSession session(data, 0x12345678);
        std::string error;
        check(session.confirm_manager(flow.pending_career()->options, flow.pending_career()->managers[0], error),
              "career controller accepts UI manager choice");
        CareerInitializationResult initialized;
        check(session.finish(initialized, error), "career reaches the first desk");
        uint64_t revision = initialized.desktop.revision;
        auto preparation = std::move(initialized.preparation);
        flow.attach_initialized_career(std::move(initialized));
        flow.attach_preparation_context(std::move(preparation));

        const auto camp_item = std::find_if(flow.desktop()->state().items.begin(), flow.desktop()->state().items.end(),
            [](const DeskItem& item) { return item.kind == 15 && !item.disabled; });
        check(camp_item != flow.desktop()->state().items.end(), "camp menu is available on the career desk");
        const int camp_x = camp_item->x + camp_item->width / 2, camp_y = camp_item->y + camp_item->height / 2;
        const auto opened = flow.click(camp_x, camp_y);
        check(flow.screen_name() == "training_camp", "desk enters the camp screen");
        if (opened.request == FlowRequest::submenu_action) {
            const auto& request = std::get<TrainingCampRequest>(opened.submenu_action);
            check(request.kind == TrainingCampRequestKind::set_focus, "first camp draw emits derived-focus normalization");
            check(session.dispatch_training_camp(request, revision, error), "controller accepts focus normalization");
            flow.acknowledge_preparation(session.preparation_projection(revision));
        } else check(opened.request == FlowRequest::submenu_opened, "camp opens without a pending engine action");

        LeImage exe = load_le(asset_path(data, "BMH.exe"));
        const int resort_x = exe.u8(::bmh::layout::TrainingCamp_Click_RuntimeField) + 1, resort_y = exe.u8(::bmh::layout::TrainingCamp_Click_RuntimeField2) + 1;
        check(flow.state_json().find("\"selected_resort\":-1")!=std::string::npos,
              "entry focus is available before a resort is hovered");
        auto focus = flow.click(255 + 7 * 3, exe.u8(::bmh::layout::TrainingCamp_FocusControls_RuntimeField) + 2);
        check(focus.request == FlowRequest::submenu_action, "focus slider produces a typed app request");
        auto focus_request = std::get<TrainingCampRequest>(focus.submenu_action);
        check(focus_request.kind == TrainingCampRequestKind::set_focus && focus_request.revision == revision &&
              focus_request.manager_slot == 0 && focus_request.runtime_club == session.state().manager_teams[0],
              "focus request carries live manager, club and revision");
        check(session.dispatch_training_camp(focus_request, revision, error), "controller writes focus once");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        const size_t record = size_t(session.state().memory.u8(::bmh::layout::ActiveManager)) * 0xb0c;
        check(session.state().manager_records[record + 0xc1] == 7 &&
              session.state().manager_records[record + 0xc4] == uint8_t(15 - (7 +
                  session.state().manager_records[record + 0xc2] + session.state().manager_records[record + 0xc3]) / 3),
              "confirmed focus is persisted with the derived fourth value");

        const int32_t balance_before = int32_t(uint32_t(session.state().manager_records[record + 0x314]) |
            uint32_t(session.state().manager_records[record + 0x315]) << 8 |
            uint32_t(session.state().manager_records[record + 0x316]) << 16 |
            uint32_t(session.state().manager_records[record + 0x317]) << 24);
        const uint8_t visits_before = session.state().manager_records[record + 0x21f];
        const auto selected = flow.click(resort_x, resort_y);
        check(selected.request == FlowRequest::none, "map click selects resort locally");
        const auto booking = flow.click(240, 218);
        check(booking.request == FlowRequest::submenu_action, "original OK button emits one booking request");
        const auto booking_request = std::get<TrainingCampRequest>(booking.submenu_action);
        check(booking_request.kind == TrainingCampRequestKind::book && booking_request.resort == 0 &&
              booking_request.revision == revision, "booking retains selection and authoritative context");
        check(session.dispatch_training_camp(booking_request, revision, error), "controller performs affordable booking");
        flow.acknowledge_preparation(session.preparation_projection(revision));
        const int32_t balance_after = int32_t(uint32_t(session.state().manager_records[record + 0x314]) |
            uint32_t(session.state().manager_records[record + 0x315]) << 8 |
            uint32_t(session.state().manager_records[record + 0x316]) << 16 |
            uint32_t(session.state().manager_records[record + 0x317]) << 24);
        check(balance_after < balance_before && session.state().manager_records[record + 0x21f] == uint8_t((visits_before + 1) | 0x80),
              "booking updates balance and visit/week state exactly once");
        check(flow.state_json().find("\"feedback\":\"booked\"") != std::string::npos,
              "acknowledgement returns the booked feedback projection");
        const auto native_after_booking=save_stream(session.state());
        const auto game_after_booking=session.game();const auto rand_after_booking=session.rand();
        const auto booked_revision=revision;
        const auto close = flow.click(150,170); // legacy routine returns after57eca's KLICK.
        check(close.request == FlowRequest::submenu_closed && !flow.in_preparation_menu() &&
              flow.screen_name()=="desktop", "success acknowledgement returns the camp caller to the desk");
        check(save_stream(session.state())==native_after_booking && revision==booked_revision &&
              session.game().seed()==game_after_booking.seed()&&session.game().calls()==game_after_booking.calls()&&
              session.rand().seed()==rand_after_booking.seed()&&session.rand().calls()==rand_after_booking.calls(),
              "success acknowledgement preserves complete Native/G/W/revision without another booking");
        const auto state_after = session.state().manager_records;
        flow.click(camp_x, camp_y);
        check(flow.screen_name() == "training_camp", "camp can be opened again after returning to desk");
        const auto before_abort = session.state().manager_records;
        const auto abort = flow.send_key("ESC");
        check(abort.request == FlowRequest::submenu_closed && session.state().manager_records == before_abort &&
              session.state().manager_records == state_after, "ESC before booking leaves engine state unchanged");
        std::puts("training camp app: entry, focus, one booking, returned projection and abort OK");
        return 0;
    } catch (const std::exception& e) { std::fprintf(stderr, "FAIL: %s\n", e.what()); return 1; }
}
