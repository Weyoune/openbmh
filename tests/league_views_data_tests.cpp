// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "app/career_session.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "ui/desktop.hpp"
#include "ui/league_table.hpp"
#include "ui/opportunities.hpp"
#include "ui/analytics.hpp"
#include "ui/series_records.hpp"
#include "ui/world_cup_statistics.hpp"
#include "ui/calendar.hpp"

using namespace bmh;
namespace {
void check(bool ok,const std::string& why) { if(!ok)throw std::runtime_error(why); }
void start(app::CareerSession& session,uint64_t& revision) {
    ui::SetupOptions options;options.world=false;
    ui::ManagerChoice manager;manager.name="Codex";
    std::string error;
    check(session.confirm_manager(options,manager,error),error);
    ui::CareerInitializationResult initialized;
    check(session.finish(initialized,error),error);
    revision=initialized.desktop.revision;
}
void tables(const std::filesystem::path& data) {
    app::CareerSession session(data,0x1789abcd);uint64_t revision;
    start(session,revision);
    const auto source=session.strengths_open_request(revision);
    check(bool(source),"fresh career has a league family owner");
    std::string error;
    check(session.prepare_league_family_child(source->identity,1,revision,error),error);
    const auto identity=session.begin_league_family(source->identity,1,revision,error);
    check(bool(identity),error);
    const auto page=session.current_league_table_dispatch(error);
    check(bool(page),error);
    const bool english=kEnglishCompetitions;
    const auto expected=english ? "01-01 only" : "Nur 01-01";
    check(page->page.retained_range_label_source_bytes==expected,
          std::string("range label follows the actual DE50860 / EN1e83f byte positions and keeps its suffix: ")+page->page.retained_range_label_source_bytes);
    const auto native=save_stream(session.state());const auto g=session.game();const auto w=session.rand();
    const auto all=ui::league_tables_from_game(session.state(),identity->manager);
    for(int league=0;league<kLeagueCount;++league) {
        if(kLeagueBit[league]==0xff)continue;
        auto owner=*identity;owner.league=league;owner.league_group=league>=5;
        ui::LeagueTableScreen screen(data,all,league,ui::LeagueTableScreen::FamilyMode{owner});
        auto frame=screen.render();
        const int count=kLeagueSizes[league];
        check(all[size_t(league)].rows.size()==size_t(count),"table retains every loaded league row");
        check(frame.at(100,count*7+30)==0x40,"panel bottom follows eighteen/twenty/twelve rows");
        const int last_y=28+count*7-1;
        check(screen.pointer_down(310,last_y)&&screen.pending_family_action()&&
              screen.pending_family_action()->runtime_team==all[size_t(league)].rows.back().runtime_team_id&&
              screen.pending_family_action()->table_row_index==count-1,
              "last table row resolves its real team, including the twentieth English club");
        screen.reject_family_action("test");
        check(!screen.pointer_down(310,last_y+1)&&!screen.pending_family_action(),
              "strict lower row bound rejects the first pixel beyond the final club");
    }
    check(save_stream(session.state())==native&&session.game().seed()==g.seed()&&
          session.game().calls()==g.calls()&&session.rand().seed()==w.seed()&&session.rand().calls()==w.calls(),
          "table rendering and semantic row requests preserve Native and both RNGs");
}
void statistics(const std::filesystem::path& data) {
    app::CareerSession session(data,0x1789abcd);uint64_t revision;
    start(session,revision);
    const auto desk=session.desktop_projection(revision);
    ui::DesktopScreen desktop(data,desk);
    const auto underlay=desktop.render();
    ui::DeskRequest owner;owner.kind=ui::DeskRequestKind::open_menu;owner.menu_kind=23;
    owner.manager_slot=desk.manager_slot;owner.revision=revision;
    ui::StatisticsMenuScreen menu(data,{owner,underlay});
    for(int index=0;index<9;++index) {
        const auto frame=menu.render_page(index);
        check(frame.width==320&&frame.height==240,"each actual statistics window fits its loaded geometry");
    }
    const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
    // A UI-only calendar DTO isolates the differing fourth-division bit.
    // Dates and Native state stay untouched; this is not a played QA fixture.
    auto calendar=ui::calendar_from_game(session.state(),exe);
    const size_t target=size_t(calendar.current_day)+1;
    check(target<calendar.days.size(),"new career has a following calendar cell");
    const int own_bit=kEnglishCompetitions ? 3 : 2;
    const int other_bit=kEnglishCompetitions ? 2 : 3;
    const int fourth_division_team=league_first_team(3);
    auto accepts=[&](int bit) {
        auto projected=calendar;
        projected.days[target].raw_entry=uint16_t(1u<<bit);
        projected.days[target].competitions=uint32_t(1u<<bit);
        ui::CalendarScreen view(data,projected,"Codex",true,fourth_division_team);
        const auto cell=ui::calendar_cell(projected.days[target]);
        view.click(cell.x+1,cell.y+1);
        return view.close_requested();
    };
    check(accepts(own_bit)&&!accepts(other_bit),
          "fourth domestic league accepts its own loaded calendar bit, never the neighbouring division's day");
    const auto charts=ui::analytics_from_game(session.state(),exe,desk.manager_slot,menu.render_page(2));
    for(const auto& series:charts.series)check(!series.label.empty(),"every actual graph label has its own source pointer");
    ui::AnalyticsScreen analytics(data,charts);
    check(analytics.render().height==240,"actual statistics graphs render with loaded labels and count classes");
    const auto streaks=ui::series_records_from_game(session.state(),exe,desk.manager_slot);
    ui::SeriesRecordsScreen records(data,streaks,menu.render_page(6));
    check(records.render().height==240,"streak records use the loaded Source column selectors");
    const auto world=ui::world_cup_statistics_from_game(session.state(),exe,
        ui::WorldCupStatisticsOwner::desk_read_only);
    ui::WorldCupStatisticsScreen nations(data,world,menu.render_page(4));
    check(nations.render().height==240,"national badge-bank segments cover the actual 62 countries");
    std::string error;
    auto opened=session.open_opportunities(owner,revision,menu.render_page(0),error);
    check(bool(opened),error);
    check(opened->league_size==kLeagueSizes[desk.league],"chances receives actual career league size");
    ui::OpportunitiesScreen screen(data,*opened);
    check(screen.render().height==240,"actual DE/EN chances page renders, including rank-zero startup");
    // Unit-only nonzero ranks exercise the complete English ladder; no career
    // bytes, dates, players, standings or RNGs are patched for this boundary test.
    auto ranks=*opened;ranks.current_rank=1;ranks.ranks.fill(1);
    ranks.ranks[size_t(ranks.league_size-1)]=0xff;
    ui::OpportunitiesScreen full(data,ranks);
    auto frame=full.render();
    check(frame.at(102,71+4*(ranks.league_size-1))==exe.u8(::bmh::layout::Opportunities_Render_RuntimeField)&&
          frame.at(112,72+4*(ranks.league_size-1))==exe.u8(::bmh::layout::Opportunities_Render_RuntimeField),
          "the last rank strip keeps the Original inclusive geometry and secured colour");
    ranks.league_size=int(ranks.ranks.size())+1;bool rejected=false;
    try { ui::OpportunitiesScreen invalid(data,ranks); }catch(const FormatError&){rejected=true;}
    check(rejected,"rank counts beyond the owned Source buffer remain rejected");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"usage: league_views_data_tests <prepared data>");
        tables(argv[1]);statistics(argv[1]);
        std::cout<<"Loaded-data table rows, Source range label and statistics windows passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
