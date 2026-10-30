// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "../../tests/test_inputs.hpp"
#include "ui/training_camp.hpp"
#include "ui/source_notice_test_helpers.hpp"
#include "ui/setup.hpp"
#include "game/desk_actions.hpp"
#include "game/messages.hpp"

#include <iostream>
#include <functional>

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void rejected(const std::function<void()>& fn) {
    try { fn(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected rejected operation");
}
TrainingCampState fixture() {
    TrainingCampState s;
    s.manager_slot = 1; s.runtime_club = 12; s.revision = 42; s.source = "engine_projection";
    s.club_name = "UNITFC"; s.manager_name = "Player1"; s.balance = 1000000; s.focus = {0, 1, 14, 10};
    s.prices = {50000, 250000, 150000, 400000, 300000, 450000, 550000, 650000};
    return s;
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("original data directory required");
        const std::filesystem::path data = argv[1];
        TrainingCampScreen screen(data, fixture());
        const auto exe = load_le(asset_path(data, "BMH.exe"));
        auto entry = screen.render();
        check(entry.pixels == screen.render().pixels, "rendering is deterministic and has no game effects");
        check(screen.selected_resort() == -1 && screen.chosen_resort() == -1,
              "entry has no resort while retaining the fixed Original controls");
        auto expected = load_vga(asset_path(data / "KB", "129.VGA"));
        const auto texts = load_textd(asset_path(data, "TEXTD.BLK"));
        const auto font = load_font(asset_path(data / "F", "BMH3.FNT"));
        auto content = fixture();
        content.currency_multiplier = int32_t(exe.u32(::bmh::layout::CurrencyMultiplier));
        content.currency_divisor = int32_t(exe.u32(::bmh::layout::CurrencyDivisor));
        content.currency_suffix = exe.cstring(::bmh::layout::CurrencySuffixText);
        GameState money;
        money.currency_suffix = content.currency_suffix;
        money.memory.add_block(::bmh::layout::CurrencyMultiplier, 8);
        money.memory.set32(::bmh::layout::CurrencyMultiplier, uint32_t(content.currency_multiplier));
        money.memory.set32(::bmh::layout::CurrencyDivisor, uint32_t(content.currency_divisor));
        check(format_number(money, 12843000, 1) == "12.843.000",
              "ordinary numbers have neither a currency affix nor currency conversion");
        if (content.currency_suffix == " &") {
            check(content.currency_multiplier == 1 && content.currency_divisor == 2,
                  "FLTD money checks use its loaded one-half currency conversion");
            check(format_amount(money, 12843000) == "& 6.421.500" &&
                  format_amount(money, -12843000) == "& -6.421.500" &&
                  format_amount(money, 0) == "& 0",
                  "FLTD common money formatter prefixes pounds, including negative and zero amounts");
            check(format_number(money, 2000, 0x108) == "&  %%%%1.000" &&
                  format_number(money, 2000, 0x188) == "&  %%%%1.000" &&
                  format_number(money, 12843000, 0x141) == "& 6421500",
                  "FLTD finance padding and optional grouping remain inside the currency prefix");
        } else {
            check(content.currency_multiplier == 1 && content.currency_divisor == 1,
                  "German money checks use its loaded identity currency conversion");
            check(format_amount(money, 12843000) == "12.843.000" + content.currency_suffix &&
                  format_amount(money, -12843000) == "-12.843.000" + content.currency_suffix &&
                  format_amount(money, 0) == "0" + content.currency_suffix &&
                  format_number(money, 2000, 0x108) == " %%%%2.000" + content.currency_suffix &&
                  format_number(money, 12843000, 0x141) == "12843000" + content.currency_suffix,
                  "German money formatting retains its suffix, padding, sign and optional grouping");
        }
        for (size_t r = 0; r < 8; ++r) {
            content.prices[r] = int32_t(exe.u32(::bmh::layout::DeskActions_ConfigureTrainingCampData_RuntimeField2 + uint32_t(r * 4))) + 100000;
            for (size_t q = 0; q < 4; ++q) content.quality[r][q] = exe.u8(::bmh::layout::DeskActions_ConfigureTrainingCampData_RuntimeField + uint32_t(r * 4 + q));
            TrainingCampScreen place(data, content);
            const int x = exe.u8(::bmh::layout::TrainingCamp_Click_RuntimeField + uint32_t(r)), y = exe.u8(::bmh::layout::TrainingCamp_Click_RuntimeField2 + uint32_t(r));
            check(place.move(x + 9, y + 8) && place.selected_resort() == int(r),
                  "each loaded map hitbox identifies its matching resort");
            auto rendered = place.render();
            auto atlas = load_vga(asset_path(data / "KB", r < 4 ? "8.VGA" : "9.VGA"));
            const int sx = int(r % 2) * 110, sy = int((r % 4) / 2) * 68;
            for (int yy = 0; yy < 68; ++yy) for (int xx = 0; xx < 110; ++xx)
                if (atlas.at(sx + xx, sy + yy) != 0xc0)
                    check(rendered.at(198 + xx, 25 + yy) == atlas.at(sx + xx, sy + yy),
                          "all eight loaded resort names and hitboxes use their original photograph cell");
        }
        if (content.currency_suffix == " &") {
            const auto source_colors = read_file(bmh::test::external_input("camp-label-colors.bin"));
            check(source_colors.size() == 9, "external label-colour expectation has nine entries");
            for (size_t i = 0; i < source_colors.size(); ++i)
                check(exe.u8(::bmh::layout::TrainingCamp_FocusControls_TextPointer + uint32_t(i)) == source_colors[i],
                      "FLTD label colours skip its three alignment bytes");
            check(training_camp_price_text(content, 0) == "& %50.000" &&
                  training_camp_price_text(content, 7) == "& 350.000",
                  "FLTD prices use its actual base price, pound prefix and one-half conversion");
        } else {
            auto currency_sample = content;
            currency_sample.prices[0] = 150000;
            check(training_camp_price_text(currency_sample, 0) == "150.000" + texts[17],
                  "German currency suffix is retained independently of edition resort prices");
        }
        auto widgets = load_vga(asset_path(data / "KB", "52.VGA"));
        for(int i=0;i<9;++i){
            const auto& label=texts[243+size_t(i)];
            const int baseline=exe.u8(::bmh::layout::TrainingCamp_FocusControls_RuntimeField2+uint32_t(i));
            font.draw(expected,200,baseline+2-font.baseline,label,0x40);
            Image mask(font.text_width(label),font.height);
            font.draw(mask,0,0,label,1);
            int gradient=0;
            while(gradient<32&&exe.u8(::bmh::layout::Advertising_Text_RuntimeField2+uint32_t(gradient))!=exe.u8(::bmh::layout::TrainingCamp_FocusControls_TextPointer+uint32_t(i)))++gradient;
            for(int y=0;y<mask.height;++y)for(int x=0;x<mask.width;++x)
                if(mask.at(x,y))expected.at(198+x,baseline-font.baseline+y)=
                    exe.u8(::bmh::layout::Advertising_Text_RuntimeField+uint32_t(gradient*6+y));
        }
        for(int i=0;i<4;++i){
            const int y=exe.u8(::bmh::layout::TrainingCamp_FocusControls_RuntimeField+uint32_t(i));
            for(int yy=0;yy<9;++yy)for(int xx=0;xx<52;++xx)
                if(widgets.at(xx,yy)!=0xc0)expected.at(255+xx,y+yy)=widgets.at(xx,yy);
            const int handle_x=255+3*fixture().focus[size_t(i)];
            for(int yy=0;yy<8;++yy)for(int xx=0;xx<16;++xx)
                if(widgets.at(xx,9+yy)!=0)expected.at(handle_x+xx,y+yy)=widgets.at(xx,9+yy);
        }
        for(int y=106;y<213;++y)for(int x=198;x<319;++x)
            check(entry.at(x,y)==expected.at(x,y),
                  "entry renders nine Source labels with shadows and all four rail/handle composites");
        auto map=load_vga(asset_path(data/"KB","12.VGA"));
        for(int y=0;y<map.height;++y)for(int x=0;x<map.width;++x)
            if(map.at(x,y)!=0xc0)check(entry.at(1+x,19+y)==map.at(x,y),
                                      "Original full map begins with source column zero at destination one");
        const int hx = exe.u8(::bmh::layout::TrainingCamp_Click_RuntimeField), hy = exe.u8(::bmh::layout::TrainingCamp_Click_RuntimeField2);
        check(screen.move(hx + 1, hy + 1) && screen.selected_resort() == 0,
              "hover locally selects resort details without producing a game request");
        auto hovered=screen.render();
        check(screen.move(300,220)&&screen.selected_resort()==-1&&screen.chosen_resort()==-1,
              "leaving an unchosen resort clears only temporary details");
        auto left=screen.render();
        for(int y=96;y<152;++y)for(int x=198;x<319;++x)
            check(left.at(x,y)==entry.at(x,y),"hover leave restores name, price and star area");
        for(int y=25;y<95;++y)for(int x=198;x<310;++x)
            check(left.at(x,y)==hovered.at(x,y),"Source hover leave retains the last photograph and shadow");
        TrainingCampScreen booking(data, fixture());
        check(booking.move(hx + 1, hy + 1), "resort selection is local hover state");
        check(booking.click(hx + 1, hy + 1).kind == TrainingCampRequestKind::none && booking.chosen_resort() == 0,
              "first map click selects the resort and reveals the original confirmation button");
        check(!booking.move(300,220)&&booking.selected_resort()==0,
              "a confirmed resort keeps its details when the pointer leaves");
        auto req = booking.click(240, 218);
        check(req.kind == TrainingCampRequestKind::book && req.manager_slot == 1 && req.runtime_club == 12 &&
              req.revision == 42 && req.resort == 0 && req.focus == fixture().focus,
              "booking emits one typed request with manager, revision and selected focus");
        check(booking.state().stays == 0 && booking.state().balance == fixture().balance,
              "booking request does not optimistically mutate projection");
        check(booking.key("ENTER").kind == TrainingCampRequestKind::none, "pending booking blocks duplicate input");
        booking.reject_action("insufficient_funds");
        check(booking.state().revision == 42 && booking.state().stays == 0 &&
              booking.state().balance == fixture().balance && booking.state().feedback == TrainingCampFeedback::insufficient_funds &&
              booking.state_json().find("insufficient_funds") != std::string::npos,
              "payment rejection shows original account warning and preserves game projection/revision");

        auto focus_screen = TrainingCampScreen(data, fixture());
        const int y = exe.u8(::bmh::layout::TrainingCamp_FocusControls_RuntimeField);
        req = focus_screen.click(255 + 7 * 3, y + 2);
        check(req.kind == TrainingCampRequestKind::set_focus && req.focus == std::array<uint8_t, 4>{7, 1, 14, 8} &&
              req.manager_slot == 1 && req.runtime_club == 12 && req.revision == 42,
              "focus can change before any resort hover and derives slider four without applying state locally");
        check(focus_screen.selected_resort()==-1&&focus_screen.chosen_resort()==-1,
              "entry focus editing does not select or book a resort");
        auto updated = fixture(); ++updated.revision; updated.focus = req.focus;
        focus_screen.acknowledge(updated);
        check(focus_screen.state().focus == req.focus && !focus_screen.pending(), "authoritative focus acknowledgment updates the screen");
        auto right_edge=TrainingCampScreen(data,fixture());
        req=right_edge.click(306,174);
        check(req.kind==TrainingCampRequestKind::set_focus&&req.focus[1]==15,
              "Source slider input clamps its extended x hitbox and includes row gaps");
        auto derived=TrainingCampScreen(data,fixture());
        check(derived.click(276,204).kind==TrainingCampRequestKind::none&&!derived.pending(),
              "fourth focus slider remains derived on entry");
        auto stale_screen = TrainingCampScreen(data, fixture());
        stale_screen.move(hx + 1, hy + 1);
        stale_screen.click(255 + 7 * 3, y + 2);
        auto stale = fixture(); stale.focus = {7, 1, 14, 8};
        rejected([&] { stale_screen.acknowledge(stale); });

        auto raw = fixture(); raw.focus = {3, 6, 9, 9}; raw.focus_normalization_required = true;
        TrainingCampScreen normalize(data, raw);
        const auto normalize_request = normalize.initialize_focus();
        check(normalize_request.kind == TrainingCampRequestKind::set_focus &&
              normalize_request.focus == std::array<uint8_t, 4>{3, 6, 9, 9} && normalize_request.revision == raw.revision,
              "engine entry normalizes original derived slider four as a drawless request");

        auto gated = fixture(); gated.stays = 0x80;
        TrainingCampScreen weekly(data, gated);
        weekly.click(hx + 1, hy + 1);
        check(!weekly.pending() && weekly.state().feedback == TrainingCampFeedback::booked_this_week,
              "weekly gate is applied before any booking request");
        bmh::ui::test::expect_notice_symbols(weekly.render(),load_vga(asset_path(data/"KB","0.VGA")));
        const auto before_expiry=weekly.state();
        check(weekly.timed_notice_open()&&weekly.expire_timed_notice()&&!weekly.timed_notice_open()&&
              !weekly.close_requested()&&!weekly.pending()&&weekly.state().revision==before_expiry.revision&&
              weekly.state().stays==before_expiry.stays&&weekly.state().balance==before_expiry.balance,
              "timed warning restores the camp locally without another booking or parent close");
        auto complete=fixture();complete.stays=0x81;complete.feedback=TrainingCampFeedback::booked;
        TrainingCampScreen success_notice(data,complete);
        check(!success_notice.timed_notice_open()&&!success_notice.expire_timed_notice(),
              "successful booking waits for interactive acknowledgement rather than warning expiry");
        TrainingCampScreen acknowledged_success(data,complete);
        acknowledged_success.click(150,170);
        check(acknowledged_success.close_requested()&&!acknowledged_success.pending(),
              "legacy routine returns to the desk after57eca acknowledges the successful booking");
        success_notice.click(0,0,true);
        check(success_notice.close_requested()&&!success_notice.pending(),
              "right close after successful booking preserves the outer camp handback");
        TrainingCampScreen right_warning(data,gated);right_warning.click(hx+1,hy+1);
        right_warning.click(0,0,true);
        check(!right_warning.timed_notice_open()&&!right_warning.close_requested()&&!right_warning.pending(),
              "right-down warning acknowledgement restores the camp instead of closing it");
        auto capped = fixture(); capped.stays = 3;
        TrainingCampScreen limit(data, capped);
        limit.click(hx + 1, hy + 1);
        check(!limit.pending() && limit.state().feedback == TrainingCampFeedback::visit_limit,
              "three-visit gate is independent from focus values");
        auto no = TrainingCampScreen(data, fixture());
        check(no.key("ESC").kind == TrainingCampRequestKind::none && no.close_requested() && !no.pending(),
              "ESC exits without booking");
        std::cout << "training camp UI: typed requests, gates, rejection and authoritative update OK\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
