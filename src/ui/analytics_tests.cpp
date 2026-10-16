#include "../../tests/test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/analytics.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.at(at + size_t(i)) = uint8_t(v >> (i * 8));
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: analytics_tests <German data directory>");
        const std::filesystem::path data(argv[1]);
        const auto exe = bmh::load_le(data / "BMH.exe");
        const auto textd = bmh::load_textd(data / "TEXTD.BLK");
        check(textd[65] == "Analytics", "Source statistics index 2 label");
        bmh::GameState game; game.manager_records.assign(4 * 0xb0c, 0);
        game.manager_teams = {0, 1, 2, 3}; game.stocks.assign(12 * 0x5c, 0);
        game.memory.add_block(::bmh::layout::Flags, 12); game.memory.set8(::bmh::layout::Flags, 0x7f);
        game.memory.add_block(::bmh::layout::FundBlock, 6 * 0x1c);
        game.memory.add_block(::bmh::layout::Analytics_SourceAssetMaskE_RuntimeField, 4 * 0x78);
        auto& m = game.manager_records;
        m[0x283]=2; m[0x284]=2; m[0x285]=2; m[0x286]=2; m[0x222]=4; m[0x223]=7;
        m[0x5c]=2; put32(m,0x314,1'500'000);
        m[0x248]=11; m[0x249]=14; m[0x270]=2; m[0x271]=3;
        put32(m,0x340,1000); put32(m,0x344,2000);
        put32(m,0x4a8,30); put32(m,0x4ac,31);
        put32(m,0x578,40); put32(m,0x57c,41);
        put32(m,0x3d8,50); put32(m,0x3dc,51);
        bmh::ui::DeskRequest owner; owner.kind=bmh::ui::DeskRequestKind::open_menu;
        owner.menu_kind=23; owner.manager_slot=0; owner.revision=9;
        const bmh::Image underlay(320,240,0x40);
        auto state=bmh::ui::analytics_from_game(game,exe,0,underlay); state.owner=owner;
        check(state.series[0].label==bmh::test::expected_text("analytics_tests-external-2").c_str() && state.series[1].label==bmh::test::expected_text("analytics_tests-external-1").c_str() &&
              state.series[2].label==bmh::test::expected_text("analytics_tests-external-3").c_str(), "Original EXE labels");
        check(state.series[0].values==std::vector<int32_t>({11,14}) &&
              state.series[1].values==std::vector<int32_t>({4000,7000}) &&
              state.series[2].values==std::vector<int32_t>({2000,6000}) &&
              state.series[3].values==std::vector<int32_t>({30,31}) &&
              state.series[4].values==std::vector<int32_t>({40,41}) &&
              state.series[5].values==std::vector<int32_t>({50,51}), "all six distinct Source manager-record series");
        check(state.series[0].absolute_max==exe.u8(::bmh::layout::LeagueSizes) &&
              state.series[2].absolute_max==50000 && state.series[4].absolute_max==2000000 &&
              state.series[5].absolute_max==1000000, "legacy routine live axis maxima");
        auto plot_state=state; plot_state.source_flags=1;
        bmh::ui::AnalyticsScreen plot_screen(data,plot_state); const auto plotted=plot_screen.render();
        const uint8_t source_color=plot_state.series[0].color;
        auto legend_state=state; legend_state.source_flags=0;
        bmh::ui::AnalyticsScreen legend_screen(data,legend_state);
        check(legend_screen.render().at(21,145)==legend_state.series[0].color,
              "Source legend sample remains visible when the metric is disabled");
        // legacy routine: denominator=max(count,0x22) for source case0, else
        // max(count,0x11); legacy routine paints an inclusive 3x3 marker.
        const int denominator=std::max(2,exe.u8(::bmh::layout::Analytics_FormatError_RuntimeField2)==0?0x22:0x11);
        const int expected_x=13+298/denominator;
        for(int y=135;y<=137;++y)for(int x=expected_x-1;x<=expected_x+1;++x)
            check(plotted.pixels[size_t(y)*size_t(plotted.width)+size_t(x)]==source_color,
                  "Source curve x-spacing and 3x3 point marker");
        auto zero_max_state=plot_state;
        zero_max_state.series[0].values={-2,0};
        bmh::ui::AnalyticsScreen zero_max_screen(data,zero_max_state);
        const auto zero_max_plot=zero_max_screen.render();
        const int zero_max_x=13+298/denominator;
        check(zero_max_plot.pixels[size_t(94)*320+13]==source_color &&
              zero_max_plot.pixels[size_t(94)*320+size_t(zero_max_x)]==source_color,
              "Source relative scaling maps every point to 50 when the maximum is zero");
        bmh::ui::AnalyticsScreen screen(data,state); const auto before=screen.render();
        check(screen.pointer_down(30,145),"metric request");
        const auto first=screen.pending(); check(first&&first->target_flags==0x7e,"typed single-metric target");
        check(screen.state().source_flags==0x7f&&screen.render().pixels==before.pixels,"no optimistic preference write");
        screen.acknowledge(first->target_flags,owner);
        check(screen.state().source_flags==0x7e&&screen.render().pixels!=before.pixels,"preference changes only after ACK");
        check(screen.pointer_down(270,150),"absolute request");
        const auto second=screen.pending(); check(second&&second->target_flags==0xbe,"paired absolute preference target");
        screen.acknowledge(second->target_flags,owner);
        check(screen.state().source_flags==0xbe,"absolute preference ACK");
        check(screen.pointer_down(10,10,true),"typed parent close");
        check(screen.pending()&&screen.pending()->kind==bmh::ui::AnalyticsRequestKind::close,
              "one right-click emits typed close");
        std::cout<<"Analytics: Source projection, flags, ACK, and close controls passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
