// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/flow.hpp"

#include <algorithm>

#include <functional>
#include <algorithm>
#include <iostream>

using namespace bmh;
using namespace bmh::ui;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void rejected(const std::function<void()>& action) {
    try { action(); } catch (const FormatError&) { return; }
    throw std::runtime_error("expected rejected operation");
}
void revision(PreparationState& s, uint64_t value) {
    s.desktop.revision = s.lineup.revision = s.tactics.revision = value;
    if (s.training) s.training->revision = value;
    if (s.pitch) s.pitch->revision = value;
    if (s.transfer_market) s.transfer_market->revision = value;
    if (s.bank) s.bank->revision = value;
    if (s.training_camp) s.training_camp->revision = value;
    for (auto& card : s.player_cards) card.lineup.revision = value;
}
PreparationState fixture(const LeImage& exe) {
    PreparationState s;
    auto& d = s.desktop;
    d.source = "engine_projection"; d.revision = 7;
    d.manager_slot = 0; d.runtime_club = 12; d.club_name = "UNITFC"; d.manager_name = "Nobody";
    d.manager_teams = {12};
    d.managers = {{12,12,0,0,0,"Nobody"}};
    d.league = 0; d.portrait = 0; d.date = desk_date(0, exe); d.strength = 80;
    d.options.managers = 1; d.options.world = false; d.options.league = true;
    d.automation[4] = 0x9f;
    for (int kind : {3, 16, 18, 12}) {
        DeskItem item; item.id = kind + 1; item.kind = kind; item.x = 20 + int(d.items.size()) * 45;
        item.y = 150; item.width = 32; item.height = 25; d.items.push_back(item);
    }
    s.lineup.manager_slot = s.tactics.manager_slot = 0;
    s.lineup.runtime_club = s.tactics.runtime_club = 12;
    s.lineup.revision = s.tactics.revision = d.revision;
    s.lineup.source = s.tactics.source = d.source;
    s.lineup.club_name = s.tactics.club_name = d.club_name;
    s.tactics.manager_name = d.manager_name;
    for (int row = 0; row < 24; ++row) {
        LineupPlayer p; p.row = row;
        if (row < 2) { p.slot = uint16_t(0x8000 | row); p.player_id = row; p.name = row ? "PLAYER" : "KEEPER"; p.board[0x0a] = 2; p.board[0x41] = row ? 3 : 0; p.board[0x42] = 4; }
        s.lineup.players.push_back(p);
    }
    TrainingState t; t.manager_slot = 0; t.runtime_club = 12; t.revision = d.revision;
    t.source = d.source; t.club_name = d.club_name; t.mode = d.options.mode; t.squad_capacity = 24;
    t.types.fill(2); t.areas.fill(2); t.automation = 0x9f;
    t.players = {{0,0,0,0,"KEEPER",2,0,0},{1,1,0,0,"PLAYER",2,0,0}};
    s.training = t;
    PitchExtras e; e.manager_slot = 0; e.runtime_club = 12; e.revision = d.revision;
    e.source = d.source; e.indoor = false; e.mode = d.options.mode; e.badge = d.badge;
    s.pitch = e;
    PlayerCardState card; card.lineup = s.lineup; card.row = 0; card.badge = d.badge;
    card.nationality_name = "DEUTSCHLAND"; card.club_short_name = "UNITFC";
    card.statistics = PlayerStatisticsContext{0,0x40};
    s.player_cards.push_back(card);
    return s;
}
void begin(StartupFlow& flow) {
    flow.click(150,215); flow.click(5,225,true); flow.click(5,225,true);
    check(flow.click(90,169).request == FlowRequest::new_career, "one career request");
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("data directory required");
        const std::filesystem::path data = argv[1]; const auto exe = load_le(asset_path(data,"BMH.exe"));
        StartupFlow flow(data); begin(flow);
        auto state = fixture(exe); state.desktop.options = flow.pending_career()->options;
        state.training->mode = state.desktop.options.mode;
        state.pitch->mode = state.desktop.options.mode;
        validate_preparation_context(state);
        flow.attach_initialized_career({state.desktop, {{0,0,12}}});
        auto broken = state; ++broken.tactics.runtime_club;
        rejected([&]{flow.attach_preparation_context(broken);});
        check(flow.screen_name() == "desktop", "failed attachment preserves idle desktop");
        broken = state; broken.training->automation ^= 4;
        rejected([&]{flow.attach_preparation_context(broken);});
        broken = state; broken.lineup.automatic_formation = 1;
        rejected([&]{flow.attach_preparation_context(broken);});
        broken = state; ++broken.player_cards[0].money_numerator;
        rejected([&]{flow.attach_preparation_context(broken);});
        flow.attach_preparation_context(state);
        {
            auto stadium_state=state;DeskItem item;item.id=23;item.kind=22;
            item.x=210;item.y=150;item.width=32;item.height=25;stadium_state.desktop.items.push_back(item);
            StadiumState page;page.manager_slot=0;page.runtime_club=12;page.revision=7;
            page.source=state.desktop.source;page.club_name=state.desktop.club_name;
            page.stable_club_identity=int(state.desktop.managers[0].club_identity_key);stadium_state.stadium=page;
            for(bool data_child:{false,true}){
                StartupFlow timed(data);begin(timed);timed.attach_initialized_career({stadium_state.desktop,{{0,0,12}}});timed.attach_preparation_context(stadium_state);
                timed.pointer_down(220,160);const auto open=timed.click(220,160);
                check(open.request==FlowRequest::submenu_action&&std::get<StadiumRequest>(open.submenu_action).kind==StadiumRequestKind::open,"stadium entry retains its normal opening ACK");
                const auto entry=timed.state_json();check(timed.process_timer_expired().request==FlowRequest::none&&timed.state_json()==entry,"stadium expiry cannot steal the opening ACK");
                auto accepted=stadium_state;revision(accepted,8);accepted.stadium=timed.stadium()->state();
                accepted.stadium->revision=8;accepted.stadium->opened=true;accepted.stadium->scope=44;accepted.stadium->sequence=1;
                timed.acknowledge_preparation(accepted);
                if(data_child){
                    timed.pointer_down(265,220);const auto held=timed.state_json();
                    check(!timed.process_timer_expired().changed&&timed.state_json()==held,"stadium expiry preserves the physical data selector");
                    const auto data_open=timed.click(265,220);check(std::get<StadiumRequest>(data_open.submenu_action).kind==StadiumRequestKind::open_data,"data selector releases its existing typed opening");
                    revision(accepted,9);accepted.stadium->revision=9;accepted.stadium->sequence=2;accepted.stadium->data.emplace();
                    timed.acknowledge_preparation(accepted);
                    timed.pointer_down(30,110);const auto curve_held=timed.state_json();
                    check(!timed.process_timer_expired().changed&&timed.state_json()==curve_held,"data expiry preserves a held curve button");
                    const auto curve=timed.click(30,110);check(std::get<StadiumRequest>(curve.submenu_action).kind==StadiumRequestKind::toggle_data_curve,"curve release keeps its own flags mutation");
                    const auto curve_pending=timed.state_json();check(timed.process_timer_expired().request==FlowRequest::none&&timed.state_json()==curve_pending,"expiry preserves the issued curve mutation ACK");
                    revision(accepted,10);accepted.stadium->revision=10;accepted.stadium->sequence=3;accepted.stadium->data->flags^=1;
                    timed.acknowledge_preparation(accepted);
                    const auto close=timed.process_timer_expired();check(close.request==FlowRequest::submenu_action&&std::get<StadiumRequest>(close.submenu_action).kind==StadiumRequestKind::close_data,"expiry closes data through its own flags ACK before the parent");
                    const auto closing=timed.state_json();check(timed.process_timer_expired().request==FlowRequest::none&&timed.state_json()==closing,"expiry does not repeat the pending data close");
                    revision(accepted,11);accepted.stadium->revision=11;accepted.stadium->sequence=4;accepted.stadium->data.reset();
                    timed.acknowledge_preparation(accepted);
                }
                const auto close=timed.process_timer_expired();check(close.request==FlowRequest::submenu_action&&std::get<StadiumRequest>(close.submenu_action).kind==StadiumRequestKind::close,"stadium outer expiry emits its owned close");
                const auto closing=timed.state_json();check(timed.process_timer_expired().request==FlowRequest::none&&timed.state_json()==closing,"stadium close remains pending until its existing ACK");
                const uint64_t next=accepted.desktop.revision+1;revision(accepted,next);accepted.stadium->revision=next;accepted.stadium->sequence++;accepted.stadium->opened=false;
                timed.acknowledge_preparation(accepted);
                check(timed.screen_name()=="desktop"&&timed.process_timer_expired().request==FlowRequest::kickoff,"stadium close returns to Desk without a fictitious UP barrier");
                check(timed.process_timer_expired().request==FlowRequest::none,"stadium expiry emits exactly one normal kickoff");
            }
        }
        {
            auto env_state=state;DeskItem item;item.id=25;item.kind=24;
            item.x=210;item.y=150;item.width=32;item.height=25;env_state.desktop.items.push_back(item);
            for(bool edit:{false,true}){
                StartupFlow timed(data);begin(timed);timed.attach_initialized_career({env_state.desktop,{{0,0,12}}});timed.attach_preparation_context(env_state);
                timed.pointer_down(220,160);const auto open=timed.click(220,160);
                check(open.request==FlowRequest::desk_action&&open.desk_action.menu_kind==24,"Umfeld expiry has its normal club Desk owner");
                EnvironmentView page;page.manager_slot=0;page.runtime_club=12;page.revision=7;page.club_name="UNITFC";
                timed.show_environment(page,open.desk_action);
                if(edit){
                    const int x=exe.u8(::bmh::layout::Environment_FormatError_RuntimeField+3)+31,y=exe.u8(::bmh::layout::Environment_FormatError_RuntimeField2+3)+5;
                    timed.pointer_down(x,y);const auto held=timed.state_json();
                    check(!timed.process_timer_expired().changed&&timed.state_json()==held,"Umfeld expiry preserves the physical staff press");
                    const auto action=timed.click(x,y);check(action.request==FlowRequest::environment_action,"staff release keeps its typed mutation");
                    const auto issued=timed.state_json();const auto expiry=timed.process_timer_expired();
                    check(!expiry.changed&&expiry.request==FlowRequest::none&&timed.pending_environment()&&timed.state_json()==issued,"Umfeld expiry preserves the pending mutation ACK without redispatching");
                }else{
                    const auto closed=timed.process_timer_expired();
                    check(closed.changed&&closed.request==FlowRequest::submenu_closed&&timed.screen_name()=="desktop","Umfeld Source expiry returns to the ordinary desk");
                    check(timed.process_timer_expired().request==FlowRequest::kickoff,"Umfeld expiry adds no fictitious mouse UP owner before the normal kickoff");
                    check(timed.process_timer_expired().request==FlowRequest::none,"Umfeld expiry emits exactly one kickoff");
                }
            }
        }
        {
            auto timed_state=state;DeskItem item;item.id=29;item.kind=28;
            item.x=210;item.y=150;item.width=32;item.height=25;timed_state.desktop.items.push_back(item);
            StartupFlow timed(data);begin(timed);timed.attach_initialized_career({timed_state.desktop,{{0,0,12}}});timed.attach_preparation_context(timed_state);
            timed.pointer_down(220,160);const auto open=timed.click(220,160);
            check(open.request==FlowRequest::desk_action&&open.desk_action.menu_kind==28,"balance expiry has a normal Desk opening owner");
            BalanceState page;page.identity={0,12,int(timed_state.desktop.managers[0].club_identity_key),timed_state.desktop.day_counter,7,10,0};
            page.draft.manager=0;page.draft.view=BalanceView::future;page.underlay=Image(320,240,0x22);page.club_name="UNITFC";
            timed.show_balance(page,open.desk_action);const auto close=timed.process_timer_expired();
            check(close.changed&&close.request==FlowRequest::balance_action&&close.balance_action.kind==BalanceAction::close&&close.balance_action.identity==page.identity,"flow expiry emits exactly the owned Bilanz close without a release barrier");
            const auto held=timed.state_json();const auto repeat=timed.process_timer_expired();
            check(!repeat.changed&&repeat.request==FlowRequest::none&&timed.state_json()==held,"flow expiry retains the staged close until the app ACK without redispatching");
        }
        {
            auto ads_state=state; DeskItem item;item.id=22;item.kind=21;
            item.x=210;item.y=150;item.width=32;item.height=25;ads_state.desktop.items.push_back(item);
            AdvertisingState ads;ads.manager_slot=0;ads.runtime_team_id=12;ads.revision=state.desktop.revision;
            ads.club_name=state.desktop.club_name;ads.manager_name=state.desktop.manager_name;
            ads.manager_portrait=0;ads.badge_sprite=0;
            for(int i=0;i<14;++i){AdvertisingContract c;c.record_slot=i;ads.boards.push_back(c);}
            ads.shirt.record_slot=14;ads.television.record_slot=15;
            for(int i=0;i<16;++i){auto& o=ads.offers[size_t(i)];o.manager=0;o.display_slot=i;o.contract_slot=i;
                o.offers.resize(i==15?5:10);}
            for(int i=0;i<3;++i){auto& s=ads.spending[size_t(i)];s.manager=0;s.target_slot=16+i;
                s.available=true;s.maximum_amount=i==0?350000:35000;}
            for(int slot=16;slot<=18;++slot){
                StartupFlow timed(data);begin(timed);timed.attach_initialized_career({ads_state.desktop,{{0,0,12}}});
                timed.attach_preparation_context(ads_state);
                timed.send_key(std::string(1,char(exe.u8(::bmh::layout::Desktop_Key_RuntimeField+21))));timed.show_advertising(ads);
                timed.click(exe.u16(::bmh::layout::Advertising_Click_RuntimeField3+slot*2)+1,exe.u16(::bmh::layout::Advertising_Click_RuntimeField4+slot*2)+1);timed.text("1234");
                check(timed.state_json().find("\"spending_calculator_open\":true")!=std::string::npos,
                      "advertising deadline fixture owns an unconfirmed Original spending calculator");
                const auto cancel=timed.process_timer_expired();
                check(cancel.changed&&cancel.request==FlowRequest::none&&!timed.pending_advertising_action()&&
                      timed.state_json().find("\"spending_calculator_open\":false")!=std::string::npos,
                      "event8 discards each spending calculator without a booking request");
                check(timed.process_timer_expired().request==FlowRequest::submenu_closed&&
                      desk_json(timed.desktop()->state())==desk_json(ads_state.desktop)&&
                      !timed.source_mouse_wait_release_pending(),"advertising expiry returns the unchanged Desk");
                check(timed.process_timer_expired().request==FlowRequest::kickoff&&
                      timed.process_timer_expired().request==FlowRequest::none,"advertising permits one normal kickoff");
            }
            StartupFlow confirmed(data);begin(confirmed);confirmed.attach_initialized_career({ads_state.desktop,{{0,0,12}}});
            confirmed.attach_preparation_context(ads_state);
            confirmed.send_key(std::string(1,char(exe.u8(::bmh::layout::Desktop_Key_RuntimeField+21))));confirmed.show_advertising(ads);
            confirmed.click(exe.u16(::bmh::layout::Advertising_Click_RuntimeField3+17*2)+1,exe.u16(::bmh::layout::Advertising_Click_RuntimeField4+17*2)+1);confirmed.text("1234");
            check(confirmed.send_key("ENTER").request==FlowRequest::advertising_action,
                  "confirmed advertising spending owns an authenticated command");
            const auto pending=confirmed.state_json();
            check(confirmed.process_timer_expired().request==FlowRequest::none&&confirmed.state_json()==pending,
                  "deadline cannot discard or duplicate confirmed advertising spending");
            auto accepted=ads_state;revision(accepted,8);auto updated_ads=ads;updated_ads.revision=8;
            updated_ads.spending[1].current_amount=1234;
            confirmed.acknowledge_advertising(updated_ads,accepted.desktop,accepted);
            check(confirmed.process_timer_expired().request==FlowRequest::submenu_closed&&
                  confirmed.desktop()->state().revision==8,"spending ACK precedes the expired advertising return");
        }
        for(const bool finance : {false,true}){
            auto stats_state=state;DeskItem item;item.id=24;item.kind=23;
            item.x=210;item.y=150;item.width=32;item.height=25;stats_state.desktop.items.push_back(item);
            StartupFlow timed(data);begin(timed);timed.attach_initialized_career({stats_state.desktop,{{0,0,12}}});
            timed.attach_preparation_context(stats_state);
            timed.pointer_down(220,160);
            const auto open=timed.click(220,160);
            timed.show_statistics_menu(open.desk_action);
            timed.pointer_down(228,219);const auto held=timed.state_json();
            check(timed.process_timer_expired().request==FlowRequest::none&&timed.state_json()==held,
                  "deadline cannot replace a held statistics selector control");
            check(timed.click(228,219).request==FlowRequest::statistics_menu_action,
                  "statistics fixture owns the normal financial-page selection");
            const auto pending=timed.state_json();
            check(timed.process_timer_expired().request==FlowRequest::none&&timed.state_json()==pending,
                  "expiry cannot close or repeat an outstanding statistics selection");
            if(finance){
                FinanceTotals page;page.manager_slot=0;timed.show_finance_totals(page);
                check(timed.process_timer_expired().request==FlowRequest::statistics_page_action&&
                      timed.pending_statistics_page_action()->kind==StatisticsPageActionKind::close,
                      "statistics child expiry emits its normal typed close");
                const auto closing=timed.state_json();
                check(timed.process_timer_expired().request==FlowRequest::none&&timed.state_json()==closing,
                      "child close retains its ACK without a duplicate request");
                timed.close_statistics_page();
            }else{
                timed.reject_statistics_menu("unsupported_selection");
                check(timed.process_timer_expired().request==FlowRequest::statistics_menu_action&&
                      timed.pending_statistics_menu()->close,"idle selector expiry emits its normal close request");
                const auto closing=timed.state_json();
                check(timed.process_timer_expired().request==FlowRequest::none&&timed.state_json()==closing,
                      "selector close cannot be repeated before ACK");
                timed.close_statistics_menu();
            }
            check(desk_json(timed.desktop()->state())==desk_json(stats_state.desktop)&&
                  !timed.source_mouse_wait_release_pending()&&timed.process_timer_expired().request==FlowRequest::kickoff&&
                  timed.process_timer_expired().request==FlowRequest::none,"statistics expiry returns one ordinary unchanged Desk turn");
        }
        {
            auto camp_state = state;
            DeskItem item; item.id = 16; item.kind = 15;
            item.x = 210; item.y = 150; item.width = 32; item.height = 25;
            camp_state.desktop.items.push_back(item);
            TrainingCampState camp; camp.manager_slot = state.desktop.manager_slot;
            camp.runtime_club = state.desktop.runtime_club; camp.revision = state.desktop.revision;
            camp.source = state.desktop.source; camp.club_name = state.desktop.club_name;
            camp.manager_name = state.desktop.manager_name; camp.balance = 80000;
            camp.prices.fill(50000); camp.focus = {0,0,0,15}; camp.focus_normalization_required = true;
            camp_state.training_camp = camp;
            StartupFlow timed(data); begin(timed);
            timed.attach_initialized_career({camp_state.desktop, {{0,0,12}}});
            timed.attach_preparation_context(camp_state);
            check(timed.send_key(std::string(1,char(exe.u8(::bmh::layout::Desktop_Key_RuntimeField+15)))).request == FlowRequest::submenu_action,
                  "camp deadline fixture owns the normal focus-normalization entry");
            const auto entry = timed.state_json();
            check(timed.process_timer_expired().request == FlowRequest::none && timed.state_json() == entry,
                  "deadline preserves the outstanding camp initialization");
            revision(camp_state,8); camp_state.training_camp->focus_normalization_required = false;
            timed.acknowledge_preparation(camp_state);
            const int x = exe.u8(::bmh::layout::TrainingCamp_Click_RuntimeField)+1, y = exe.u8(::bmh::layout::TrainingCamp_Click_RuntimeField2)+1;
            timed.click(x,y);
            check(timed.state_json().find("\"chosen_resort\":0") != std::string::npos,
                  "camp deadline fixture has a chosen but unbooked resort");
            check(timed.process_timer_expired().request == FlowRequest::submenu_closed &&
                  timed.screen_name() == "desktop" && !timed.source_mouse_wait_release_pending() &&
                  desk_json(timed.desktop()->state()) == desk_json(camp_state.desktop),
                  "expiry closes the chosen camp without booking or creating a release owner");
            check(timed.process_timer_expired().request == FlowRequest::kickoff &&
                  timed.process_timer_expired().request == FlowRequest::none,
                  "returned camp permits exactly one ordinary Desk kickoff");
            StartupFlow booking(data); begin(booking);
            booking.attach_initialized_career({camp_state.desktop, {{0,0,12}}});
            booking.attach_preparation_context(camp_state);
            booking.send_key(std::string(1,char(exe.u8(::bmh::layout::Desktop_Key_RuntimeField+15)))); booking.click(x,y);
            check(booking.click(280,175).request == FlowRequest::submenu_action,
                  "camp fixture owns an ordinary focus edit");
            const auto focus = booking.state_json();
            check(booking.process_timer_expired().request == FlowRequest::none && booking.state_json() == focus,
                  "expiry preserves a pending camp focus edit");
            booking.reject_preparation_action("unsupported_focus");
            booking.send_key("ENTER"); // dismiss the normal rejection before confirming a booking
            check(booking.click(255,218).request == FlowRequest::submenu_action,
                  "camp fixture owns a confirmed booking request");
            const auto pending = booking.state_json();
            check(booking.process_timer_expired().request == FlowRequest::none && booking.state_json() == pending,
                  "expiry cannot discard or duplicate a confirmed camp booking");
            auto accepted = camp_state; revision(accepted,9); accepted.training_camp->stays = 0x81;
            booking.acknowledge_preparation(accepted);
            check(booking.process_timer_expired().request == FlowRequest::submenu_closed &&
                  booking.desktop()->state().revision == 9,
                  "booking ACK completes before the camp's expired outer poll returns");
        }
        for (const int kind : {8,7}) {
            auto cal_state = state;
            DeskItem item; item.id = kind+1; item.kind = kind;
            item.x = 210; item.y = 150; item.width = 32; item.height = 25;
            cal_state.desktop.items.push_back(item);
            CalendarState calendar; calendar.current_day = 0;
            for (int day=0; day<365; ++day) {
                CalendarDayProjection p; p.season_day = day; p.day_counter = uint32_t(day);
                p.date = desk_date(uint32_t(day),exe); p.current_day = day==0;
                calendar.days.push_back(p);
            }
            if (kind==7) {
                calendar.reschedule_manager = 0;
                calendar.reschedule_options.push_back({LeagueMatchRef{0,0,0,0,12,1}, {1}});
            }
            StartupFlow timed(data); begin(timed);
            timed.attach_initialized_career({cal_state.desktop, {{0,0,12}}});
            timed.attach_preparation_context(cal_state);
            const auto open = timed.send_key(std::string(1,char(exe.u8(::bmh::layout::Desktop_Key_RuntimeField+kind))));
            timed.show_calendar(calendar,open.desk_action);
            if (kind==7) {
                const auto first = calendar_cell(calendar.days[0]);
                const auto target = calendar_cell(calendar.days[1]);
                timed.pointer_down(first.x+1,first.y+1);
                const auto held = timed.state_json();
                check(timed.process_timer_expired().request == FlowRequest::none && timed.state_json() == held,
                      "reschedule selection retains its physical UP before expiry");
                timed.click(first.x+1,first.y+1);
                check(timed.pointer_down(target.x+1,target.y+1).request == FlowRequest::reschedule_action,
                      "calendar fixture owns a real typed reschedule command");
                timed.click(target.x+1,target.y+1);
                const auto pending = timed.state_json();
                check(timed.process_timer_expired().request == FlowRequest::none && timed.state_json() == pending,
                      "expiry cannot close or repeat a pending reschedule command");
                timed.reject_reschedule();
            }
            check(timed.process_timer_expired().request == FlowRequest::submenu_closed &&
                  timed.screen_name() == "desktop" && !timed.source_mouse_wait_release_pending() &&
                  desk_json(timed.desktop()->state()) == desk_json(cal_state.desktop),
                  "calendar event8 returns the unchanged Desk without a mutation or UP barrier");
            check(timed.process_timer_expired().request == FlowRequest::kickoff &&
                  timed.process_timer_expired().request == FlowRequest::none,
                  "calendar expiry permits one ordinary kickoff");
        }
        {
            auto bank_state = state;
            DeskItem item; item.id = 15; item.kind = 14;
            item.x = 210; item.y = 150; item.width = 32; item.height = 25;
            bank_state.desktop.items.push_back(item);
            BankState bank; bank.manager_slot = state.desktop.manager_slot;
            bank.runtime_club = state.desktop.runtime_club; bank.revision = state.desktop.revision;
            bank.source = state.desktop.source; bank.club_name = state.desktop.club_name;
            bank.manager_name = state.desktop.manager_name; bank.balance = 80000; bank.deposit = 120000;
            bank.stocks[11].price = 1000; bank.stocks[11].max_to_buy = 5;
            bank.funds[0].price = 10000; bank.funds[0].max_to_buy = 5;
            GameState credit_game; credit_game.manager_teams = {12};
            credit_game.manager_records.assign(0xb0c, 0);
            credit_game.memory.add_block(::bmh::layout::Loans, 0x78);
            bank.credit = bank_credit_context(credit_game, exe, 0);
            bank_state.bank = bank;
            for (const int subview : {-1, 0, 1, 2, 3, 4}) {
                StartupFlow timed(data); begin(timed);
                timed.attach_initialized_career({bank_state.desktop, {{0,0,12}}});
                timed.attach_preparation_context(bank_state);
                check(timed.send_key("B").request == FlowRequest::submenu_opened && timed.screen_name() == "bank",
                      "bank deadline fixture opens the Original B owner");
                if (subview >= 0) {
                    timed.click((exe.u16(::bmh::layout::Bank_FinishCredit_RuntimeField + subview * 2) + exe.u16(::bmh::layout::Bank_FinishCredit_RuntimeField2 + subview * 2)) / 2,
                                (exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField3 + subview) + exe.u8(::bmh::layout::Bank_FinishCredit_RuntimeField4 + subview)) / 2);
                    if (subview != 1) {
                        if (subview == 4) {
                            timed.click(10,70); timed.send_key("ENTER");
                            check(timed.state_json().find("\"credit_edit_count\":1") != std::string::npos &&
                                  !timed.pending_bank_action(), "credit calculator confirmation stages only a local draft");
                            timed.click(10,70);
                        } else timed.click(80,178);
                        timed.text("2");
                        check(timed.state_json().find("\"calculator_open\":true") != std::string::npos,
                              "bank deadline fixture owns an unconfirmed amount/count calculator");
                        const auto cancel = timed.process_timer_expired();
                        check(cancel.request == FlowRequest::none && cancel.changed &&
                              timed.state_json().find("\"calculator_open\":false") != std::string::npos &&
                              !timed.pending_bank_action(),
                              "event8 discards the calculator without a money or asset request");
                    }
                    const auto back = timed.process_timer_expired();
                    check(back.request == FlowRequest::none && back.changed && timed.screen_name() == "bank",
                          "next event8 restores the bank room before its outer poll");
                    if (subview == 4)
                        check(timed.state_json().find("\"credit_edit_count\":0") != std::string::npos &&
                              timed.state_json().find("\"credit_open\":false") != std::string::npos,
                              "credit expiry discards the staged loan without submitting it");
                }
                check(timed.process_timer_expired().request == FlowRequest::submenu_closed &&
                      timed.screen_name() == "desktop" && !timed.source_mouse_wait_release_pending() &&
                      desk_json(timed.desktop()->state()) == desk_json(bank_state.desktop),
                      "bank expiry returns the unchanged authenticated Desk without a release barrier");
                check(timed.process_timer_expired().request == FlowRequest::kickoff &&
                      timed.process_timer_expired().request == FlowRequest::none,
                      "bank expiry permits exactly one ordinary Desk kickoff");
            }
            StartupFlow booking(data); begin(booking);
            booking.attach_initialized_career({bank_state.desktop, {{0,0,12}}});
            booking.attach_preparation_context(bank_state); booking.send_key("B");
            booking.click(270,90); booking.click(80,178); booking.text("50000");
            const auto request = booking.send_key("ENTER");
            check(request.request == FlowRequest::submenu_action &&
                  std::get<BankRequest>(request.submenu_action).amount == 50000,
                  "pending bank fixture owns an actually confirmed deposit");
            const auto pending = booking.state_json();
            check(booking.process_timer_expired().request == FlowRequest::none && booking.state_json() == pending,
                  "expiry cannot discard, change or duplicate a pending confirmed deposit");
            auto accepted = bank_state; revision(accepted,8);
            accepted.bank->balance -= 50000; accepted.bank->deposit += 50000;
            booking.acknowledge_preparation(accepted);
            check(booking.process_timer_expired().request == FlowRequest::none &&
                  booking.process_timer_expired().request == FlowRequest::submenu_closed &&
                  booking.desktop()->state().revision == 8,
                  "confirmed booking ACK returns before expiry closes its remaining owners");
        }
        for (const bool training : {false, true}) {
            StartupFlow timed(data); begin(timed);
            timed.attach_initialized_career({state.desktop, {{0,0,12}}});
            timed.attach_preparation_context(state);
            check(timed.send_key(training ? "I" : "D").request == FlowRequest::submenu_opened,
                  "deadline fixture opens the initialized training/tactics owner");
            const auto edit = timed.click(training ? 155 : 180, training ? 26 : 50);
            check(edit.request == FlowRequest::submenu_action,
                  "deadline fixture owns an ordinary preparation edit");
            const auto pending_edit = timed.state_json();
            check(timed.process_timer_expired().request == FlowRequest::none &&
                  timed.state_json() == pending_edit,
                  "expiry preserves the pending edit and cannot repeat or discard it");
            auto accepted = state; revision(accepted, 8);
            if (training) {
                accepted.training->areas[0] = 1;
                accepted.training->automation &= uint8_t(~4);
                accepted.desktop.automation[4] = accepted.training->automation;
            } else accepted.tactics.values[5] = 1;
            timed.acknowledge_preparation(accepted);
            check(timed.process_timer_expired().request == FlowRequest::submenu_closed &&
                  timed.screen_name() == "desktop" && !timed.source_mouse_wait_release_pending() &&
                  desk_json(timed.desktop()->state()) == desk_json(accepted.desktop),
                  "event8 returns the accepted preparation to Desk without a new command or UP barrier");
            const auto kickoff = timed.process_timer_expired();
            check(kickoff.request == FlowRequest::kickoff && kickoff.desk_action.manager_slot == 0 &&
                  kickoff.desk_action.revision == 8,
                  "returned preparation gives Desk one normal authenticated kickoff");
            const auto pending_turn = timed.state_json();
            check(timed.process_timer_expired().request == FlowRequest::none &&
                  timed.state_json() == pending_turn,
                  "repeated expiry cannot duplicate the Desk kickoff");
        }
        auto open = flow.send_key("A");
        check(flow.screen_name() == "lineup" && open.request == FlowRequest::submenu_action &&
              std::get<LineupRequest>(open.submenu_action).kind == LineupRequestKind::prepare_lineup,
              "engine-attached lineup entry is an explicit command");
        check(flow.state_json().find("\"engine_attached\":false") == std::string::npos,
              "nested context reports the same engine attachment");
        const auto pending = flow.state_json();
        flow.click(120,214,true); flow.send_key("A");
        check(flow.state_json() == pending, "pending lineup entry blocks close and duplicate commands");
        rejected([&]{flow.acknowledge_preparation(state);});
        auto updated = state; revision(updated,8); updated.lineup.entry_prepared = true;
        broken = updated; ++broken.desktop.season_day;
        rejected([&]{flow.acknowledge_preparation(broken);});
        check(flow.state_json() == pending, "bad acknowledgement is atomic");
        flow.acknowledge_preparation(updated); state = updated;
        {
            StartupFlow timed(data); begin(timed);
            timed.attach_initialized_career({state.desktop, {{0,0,12}}});
            timed.attach_preparation_context(state);
            check(timed.send_key("A").request==FlowRequest::submenu_action,
                  "timer fixture owns the normal lineup entry request");
            const auto entry_pending=timed.state_json();
            check(timed.process_timer_expired().request==FlowRequest::none &&
                  timed.state_json()==entry_pending,"deadline cannot replace an outstanding lineup entry");
            auto ready=state;revision(ready,9);ready.lineup.entry_prepared=true;
            timed.acknowledge_preparation(ready);
            timed.click(80,33);timed.click(100,215);
            check(timed.screen_name()=="player_statistics" &&
                  timed.process_timer_expired().request==FlowRequest::submenu_closed &&
                  timed.screen_name()=="player_card",
                  "deadline first returns the local player-statistics child to its card");
            check(timed.process_timer_expired().request==FlowRequest::submenu_closed &&
                  timed.screen_name()=="lineup",
                  "next deadline returns the local card to its lineup, without a player action");
            timed.click(20,33);
            check(timed.state_json().find("\"selected_row\":0")!=std::string::npos,"timer fixture has a local player selection");
            check(timed.process_timer_expired().request==FlowRequest::none &&
                  timed.screen_name()=="lineup" && timed.state_json().find("\"selected_row\":null")!=std::string::npos &&
                  timed.state_json().find("\"revision\":9")!=std::string::npos,
                  "first event8 cancels lineup selection without a squad command or revision change");
            check(timed.process_timer_expired().request==FlowRequest::submenu_closed &&
                  timed.screen_name()=="desktop" && !timed.source_mouse_wait_release_pending(),
                  "next event8 closes lineup without inventing a physical release owner");
            const auto kickoff=timed.process_timer_expired();
            check(kickoff.request==FlowRequest::kickoff && kickoff.desk_action.manager_slot==0 &&
                  kickoff.desk_action.revision==9,"returned Desk owns one ordinary authenticated kickoff");
            const auto owned=timed.state_json();
            check(timed.process_timer_expired().request==FlowRequest::none && timed.state_json()==owned,
                  "repeated expired polls cannot emit a second kickoff");
        }
        check(flow.click(80,33).request == FlowRequest::submenu_opened && flow.screen_name() == "player_card",
              "ordinary player card opens as local modal over the current lineup");
        rejected([&]{flow.acknowledge_preparation(updated);});
        const auto player_frame = flow.render().pixels;
        check(flow.click(100,215).request == FlowRequest::submenu_opened && flow.screen_name() == "player_statistics",
              "statistics opens as a second local modal with an explicit destination");
        check(flow.state_json().find("\"revision\":8") != std::string::npos && !flow.move(10,10),
              "statistics keeps authoritative revision and blocks parent hover");
        check(flow.click(60,147).request == FlowRequest::none && flow.screen_name() == "player_statistics",
              "chart toggles emit no engine mutation");
        rejected([&]{flow.acknowledge_preparation(updated);});
        check(flow.send_key("ESC").request == FlowRequest::submenu_closed && flow.screen_name() == "player_card",
              "first ESC returns one level to card");
        const auto restored_player = flow.render().pixels;
        check(std::equal(player_frame.begin(),player_frame.begin()+198*320,restored_player.begin()),
              "statistics back restores card content and original parent context");
        check(flow.send_key("ESC").request == FlowRequest::submenu_closed && flow.screen_name() == "lineup",
              "closing player card restores parent instead of closing preparation");
        auto pitch_entry=flow.click(80,219);
        check(pitch_entry.request==FlowRequest::submenu_action&&
              std::holds_alternative<PitchRequest>(pitch_entry.submenu_action)&&
              std::get<PitchRequest>(pitch_entry.submenu_action).kind==PitchRequestKind::select_pitch_page&&
              std::get<PitchRequest>(pitch_entry.submenu_action).page_control==1&&flow.screen_name()=="pitch",
              "Lineup-to-Pitch entry emits the authenticated Source Team word transition");
        auto pitch_entry_ack=state;revision(pitch_entry_ack,9);
        pitch_entry_ack.pitch->native_display_word=std::get<PitchRequest>(pitch_entry.submenu_action).new_display_word;
        flow.acknowledge_preparation(pitch_entry_ack);state=pitch_entry_ack;
        check((state.pitch->native_display_word&0x08)!=0,
              "only the accepted Source word places the presenter on its pitch/body branch");
        flow.click(30,164); flow.move(40,164);
        auto placement = flow.click(40,164);
        check(placement.request == FlowRequest::submenu_action && std::get<PitchRequest>(placement.submenu_action).row == 1,
              "pitch placement emits the real player identity through common request boundary");
        const auto placement_pending = flow.state_json();
        check(flow.process_timer_expired().request==FlowRequest::none &&
              flow.state_json()==placement_pending,"deadline cannot cancel or repeat pending pitch placement");
        flow.click(200,128); flow.send_key("ESC");
        check(flow.state_json() == placement_pending, "pending pitch blocks TEAM and closing");
        auto placed = state; revision(placed,10); placed.lineup.players[1].board[0x41] = 4;
        broken = placed; broken.pitch->revision = 8;
        rejected([&]{flow.acknowledge_preparation(broken);});
        broken = placed; ++broken.player_cards[0].money_denominator;
        rejected([&]{flow.acknowledge_preparation(broken);});
        check(flow.state_json() == placement_pending, "bad dependent pitch revision is atomic");
        flow.acknowledge_preparation(placed); state = placed;
        auto team_return=flow.click(200,128);
        check(team_return.request==FlowRequest::submenu_action&&
              std::get<PitchRequest>(team_return.submenu_action).page_control==1,
              "Pitch Team-return control requests the same authenticated XOR 0x0c transition");
        auto team_ack=state;revision(team_ack,11);
        team_ack.pitch->native_display_word=std::get<PitchRequest>(team_return.submenu_action).new_display_word;
        flow.acknowledge_preparation(team_ack);state=team_ack;
        check(flow.screen_name()=="pitch"&&(state.pitch->native_display_word&0x08)==0,
              "Team page remains an interactive full-sheet Pitch view after the accepted word transition");
        const auto team_sheet=flow.state_json();
        check(flow.send_key("ESC").request==FlowRequest::none&&flow.state_json()==team_sheet,
              "desktop Team sheet ignores Escape after the authenticated page transition");
        check(flow.click(310,230,true).request == FlowRequest::submenu_closed && flow.screen_name() == "desktop",
              "acknowledged entry closes locally on the Original right click");
        check(flow.send_key("D").request == FlowRequest::submenu_opened && flow.screen_name() == "tactics",
              "original D opens tactics locally");
        const auto unchanged = flow.render().pixels;
        auto change = flow.click(180,50);
        check(change.request == FlowRequest::submenu_action && std::get<TacticsRequest>(change.submenu_action).index == 5,
              "tactic click emits a typed command");
        check(flow.render().pixels == unchanged && flow.click(180,50).request == FlowRequest::none,
              "pending tactic neither changes pixels nor repeats");
        flow.click(120,214,true); check(flow.screen_name() == "tactics", "pending tactic blocks close");
        broken = state; revision(broken,12); broken.desktop.day_counter = 1;
        rejected([&]{flow.acknowledge_preparation(broken);});
        flow.reject_preparation_action("controller_rejected");
        check(flow.render().pixels == unchanged, "explicit rejection preserves values");
        change = flow.click(180,50);
        updated = state; revision(updated,12); updated.tactics.values[5] = 1;
        flow.acknowledge_preparation(updated); state = updated;
        flow.click(120,214,true); check(flow.screen_name() == "desktop", "updated tactic closes");
        check(flow.send_key("I").request == FlowRequest::submenu_opened && flow.screen_name() == "training",
              "initialized original I opens training without hidden execution");
        change = flow.click(155,26);
        check(change.request == FlowRequest::submenu_action && std::get<TrainingRequest>(change.submenu_action).value == 1,
              "condition direct-slot command");
        updated = state; revision(updated,13); updated.training->areas[0] = 1;
        updated.training->automation &= uint8_t(~4); updated.desktop.automation[4] = updated.training->automation;
        broken = updated; broken.desktop.automation[4] = state.desktop.automation[4];
        rejected([&]{flow.acknowledge_preparation(broken);});
        flow.acknowledge_preparation(updated); state = updated;
        flow.click(120,214,true); check(flow.screen_name() == "desktop", "training returns to fresh desktop");
        check(flow.desktop()->state().automation[4] == 0x9b && flow.desktop()->state().revision == 13,
              "dependent desktop fields update at the same authoritative revision");
        open = flow.send_key("A");
        check(open.request == FlowRequest::submenu_action, "each original lineup entry requests its effects explicitly");
        flow.reject_preparation_action("unsupported_entry"); flow.click(310,230,true);
        auto mode1 = state; mode1.desktop.options.mode = 1; mode1.training.reset();
        mode1.pitch->mode = 1;
        validate_preparation_context(mode1);
        check(!mode1.training, "Modus I is a valid desktop context without individual training");

        StartupFlow aa_flow(data); begin(aa_flow);
        auto aa = fixture(exe); aa.desktop.options = aa_flow.pending_career()->options;
        aa.lineup.saved_display_flags=aa.pitch->native_display_word=0x10;
        aa.pitch->indoor=true;
        aa.pitch->formation_mask = 3;
        for(int row=2;row<11;++row){auto& p=aa.lineup.players[row];p.player_id=row;
            p.slot=uint16_t(0x8000|row);p.name="PLAYER";p.board[0x41]=uint8_t(row);p.board[0x42]=4;}
        aa_flow.attach_initialized_career({aa.desktop, {{0,0,12}}});
        aa_flow.attach_preparation_context(aa); aa_flow.send_key("A");
        revision(aa,8); aa.lineup.entry_prepared = true; aa_flow.acknowledge_preparation(aa);
        const auto enter_pitch=aa_flow.click(80,219);
        check(enter_pitch.request==FlowRequest::submenu_action&&
              std::get<PitchRequest>(enter_pitch.submenu_action).page_control==1,
              "AA controls become available only after the typed Lineup-to-Pitch transition");
        auto aa_entered=aa;revision(aa_entered,9);
        aa_entered.pitch->native_display_word=std::get<PitchRequest>(enter_pitch.submenu_action).new_display_word;
        aa_flow.acknowledge_preparation(aa_entered);aa=aa_entered;
        auto weight = aa_flow.click(250,215);
        check(weight.request == FlowRequest::submenu_action &&
              std::get<PitchRequest>(weight.submenu_action).kind == PitchRequestKind::set_automatic_weight&&
              std::get<PitchRequest>(weight.submenu_action).indoor,
              "Hall AA weights route their live mode through the common preparation command boundary");
        auto aa_pending = aa_flow.state_json();
        auto aa_updated = aa; revision(aa_updated,10);
        rejected([&]{aa_flow.acknowledge_preparation(aa_updated);});
        check(aa_flow.state_json() == aa_pending, "unconfirmed AA byte preserves all shared views atomically");
        aa_updated.pitch->automatic_weights = 1;
        aa_flow.acknowledge_preparation(aa_updated); aa = aa_updated;
        auto select = aa_flow.click(290,120);
        check(select.request == FlowRequest::submenu_action &&
              std::get<PitchRequest>(select.submenu_action).formation == 1,
              "formation selection reaches the engine with the zero-based template index");
        aa_updated = aa; revision(aa_updated,11); aa_updated.lineup.automatic_formation = 2;
        aa_pending = aa_flow.state_json();
        rejected([&]{aa_flow.acknowledge_preparation(aa_updated);});
        check(aa_flow.state_json() == aa_pending, "formation acknowledgement requires matching desktop automation");
        aa_updated.desktop.automation[0] = 2;
        for(auto& card : aa_updated.player_cards) card.lineup = aa_updated.lineup;
        aa_flow.acknowledge_preparation(aa_updated);
        aa = aa_updated;
        rejected([&]{aa_flow.hold(250,166,-1);});
        rejected([&]{aa_flow.hold(250,166,60001);});
        check(aa_flow.hold(250,166,1999).request==FlowRequest::none,
              "short hold on an empty slot does not save a formation");
        aa_flow.pointer_down(250,166);
        check(aa_flow.pitch_held_left(250,166,25).request==FlowRequest::none,
              "holding25 BIOS ticks is not an own-formation save");
        auto store=aa_flow.pitch_held_left(250,166,26);
        check(store.request==FlowRequest::submenu_action&&
              std::get<PitchRequest>(store.submenu_action).kind==PitchRequestKind::save_formation,
              "held26th BIOS tick routes a save without waiting for mouse UP");
        const auto save_request=std::get<PitchRequest>(store.submenu_action);
        aa_updated=aa;revision(aa_updated,12);aa_updated.desktop.automation[0]=5;
        aa_updated.lineup.automatic_formation=5;aa_updated.pitch->formation_mask|=0x10;
        std::copy(save_request.formation_positions.begin(),save_request.formation_positions.end(),
                  aa_updated.pitch->formations.begin()+4*22);
        for(auto& card:aa_updated.player_cards)card.lineup=aa_updated.lineup;
        aa_flow.acknowledge_preparation(aa_updated);
        check(aa_flow.pitch_held_left(250,166,100).request==FlowRequest::none&&
              aa_flow.click(290,120).request==FlowRequest::none,
              "continued hold and its eventual UP over a different preset are neutral after ACK");
        const auto hall_instructions=aa_flow.click(210,154);
        check(hall_instructions.request==FlowRequest::submenu_action&&
              std::get<PitchRequest>(hall_instructions.submenu_action).page_control==3,
              "Hall Anweis page transition reaches the shared command boundary");
        revision(aa_updated,13);
        aa_updated.pitch->native_display_word=std::get<PitchRequest>(hall_instructions.submenu_action).new_display_word;
        aa_flow.acknowledge_preparation(aa_updated);
        check(aa_flow.click(290,120).request==FlowRequest::none&&
              aa_flow.hold(250,166,2000).request==FlowRequest::none,
              "Hall Anweis flow ignores invisible preset and held-own-slot actions");
        const auto hall_aa=aa_flow.click(210,141);
        check(hall_aa.request==FlowRequest::submenu_action&&
              std::get<PitchRequest>(hall_aa.submenu_action).page_control==2,
              "Hall Aufst page restores the explicit native AA page");
        revision(aa_updated,14);
        aa_updated.pitch->native_display_word=std::get<PitchRequest>(hall_aa.submenu_action).new_display_word;
        aa_flow.acknowledge_preparation(aa_updated);
        aa_flow.click(310,230,true);
        check(aa_flow.screen_name() == "desktop" && aa_flow.desktop()->state().revision == 14 &&
              aa_flow.desktop()->state().automation[0] == 5,
              "AA acknowledgement updates dependent desktop and team views at one revision");

        StartupFlow transfer_flow(data); begin(transfer_flow);
        auto transfer_state = fixture(exe); transfer_state.desktop.options = transfer_flow.pending_career()->options;
        transfer_state.desktop.items.push_back({17, 250, 150, 32, 25, 17});
        TransferMarketState transfer; transfer.manager_slot = transfer_state.desktop.manager_slot;
        transfer.runtime_club = transfer_state.desktop.runtime_club; transfer.revision = transfer_state.desktop.revision;
        transfer.source = transfer_state.desktop.source; transfer.manager_name = transfer_state.desktop.manager_name;
        transfer.club_name = transfer_state.desktop.club_name;
        transfer.items.push_back({0, 50, 20, 21, 2, 75, "NEW PLAYER", "OTHER CLUB", 100, 100, 100, 0, false, false});
        transfer.items.back().source_slot = 2928;
        transfer.source_view_flags = 0x1f0; transfer.source_market_mode = 3;
        transfer.squad.push_back({0, 0, 0x8000, "KEEPER", 30, 0, 80, false});
        transfer_state.transfer_market = transfer;
        transfer_flow.attach_initialized_career({transfer_state.desktop, {{0,0,12}}});
        transfer_flow.attach_preparation_context(transfer_state);
        {
            StartupFlow timed(data);begin(timed);
            auto paged=transfer_state;
            for(int i=1;i<7;++i) {
                auto item=paged.transfer_market->items.front();
                item.list_entry=i;item.source_slot=2928+i;item.player=50+i;
                paged.transfer_market->items.push_back(item);
            }
            timed.attach_initialized_career({paged.desktop,{{0,0,12}}});
            timed.attach_preparation_context(paged);timed.click(255,155);
            const auto page=timed.process_timer_expired(220,32);
            check(page.changed && page.request==FlowRequest::none && timed.screen_name()=="transfer_market" &&
                  timed.state_json().find("\"market_page\":1")!=std::string::npos,
                  "format field deadline inside market viewport pages without closing or posting an offer");
            check(timed.process_timer_expired(184,32).request==FlowRequest::submenu_closed &&
                  timed.screen_name()=="desktop",
                  "moving outside the Source viewport lets the expired market return to Desk");
            check(timed.process_timer_expired().request==FlowRequest::kickoff &&
                  timed.process_timer_expired().request==FlowRequest::none,
                  "expired transfer return still owns exactly one normal kickoff");
        }
        check(transfer_flow.click(255,155).request == FlowRequest::submenu_opened &&
              transfer_flow.screen_name() == "transfer_market", "menu 17 reaches its UI flow");
        const auto preview = transfer_flow.move_action(220,32);
        const auto preview_pending=transfer_flow.state_json();
        check(transfer_flow.process_timer_expired(0,0).request==FlowRequest::none &&
              transfer_flow.state_json()==preview_pending,
              "timer preserves an outstanding transfer hover quote instead of closing its owner");
        check(preview.request == FlowRequest::submenu_action &&
              std::get<TransferMarketRequest>(preview.submenu_action).kind == TransferMarketRequestKind::preview_quote &&
              std::get<TransferMarketRequest>(preview.submenu_action).expected_player == 50,
              "Source market hover preserves the selected Native player identity");
        TransferMarketPreview quoted; quoted.manager_slot = 0; quoted.runtime_club = 12;
        quoted.source_slot = 2928; quoted.player = 50; quoted.seller = 20;
        quoted.revision = 7; quoted.delta = -100; quoted.projected_balance = -100; quoted.purchase = true;
        transfer_flow.acknowledge_transfer_market_preview(transfer_state, quoted);
        check(transfer_flow.click(220,32).request == FlowRequest::none,
              "Source click marks the already previewed market player");
        auto bid = transfer_flow.click(200,207);
        check(bid.request == FlowRequest::submenu_action &&
              std::get<TransferMarketRequest>(bid.submenu_action).kind == TransferMarketRequestKind::open_offer &&
              std::get<TransferMarketRequest>(bid.submenu_action).bid.player == 50 &&
              std::get<TransferMarketRequest>(bid.submenu_action).bid.source_slot == 2928 &&
              std::get<TransferMarketRequest>(bid.submenu_action).selected_players.size() == 1,
              "typed Source buy opening preserves the selected player and explicit pool identity");
        transfer_flow.reject_preparation_action("offer_too_low");
        check(transfer_flow.send_key("ESC").request == FlowRequest::submenu_closed &&
              transfer_flow.screen_name() == "desktop", "rejected market action leaves the flow usable");

        if (argc > 2 && std::string(argv[2]).size()) {
            const auto bytes = read_file(argv[2]);
            StartupFlow reference(data); reference.load_reference(argv[2]);
            check(reference.send_key("A").request == FlowRequest::submenu_opened && reference.screen_name() == "lineup",
                  "read-only reference opens projected lists without pretending original entry effects");
            auto navigation = reference.click(80,219);
            check(navigation.request == FlowRequest::submenu_action && reference.screen_name() == "lineup",
                  "unknown transient pitch context remains a visible pending navigation");
            reference.reject_preparation_action("missing_transient_context");
            const auto parent = reference.render().pixels;
            check(reference.click(80,33).request == FlowRequest::submenu_opened && reference.screen_name() == "player_card",
                  "reference owns original card metadata and opens modal");
            reference.send_key("ESC"); check(reference.render().pixels == parent, "reference card restores generated parent frame");
            rejected([&]{reference.acknowledge_preparation(state);});
            reference.click(310,230,true); reference.send_key("I");
            check(reference.screen_name() == "training", "reference training navigation");
            auto request = reference.click(155,26);
            check(request.request == FlowRequest::submenu_action && std::get<TrainingRequest>(request.submenu_action).value == 1,
                  "reference records a real desired input but cannot execute it");
            reference.send_key("ESC"); check(reference.screen_name() == "training", "reference pending also blocks close");
            reference.reject_preparation_action("reference_read_only"); reference.send_key("ESC");
            reference.send_key("D"); check(reference.screen_name() == "tactics", "reference tactics navigation");
            check(bytes == read_file(argv[2]), "all reference interactions leave source save unchanged");
        }
        std::cout << "Preparation navigation, explicit entry effects, atomic acknowledgement and dependent views passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "preparation test: " << e.what() << '\n'; return 1; }
}
