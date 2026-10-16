// SPDX-License-Identifier: GPL-3.0-only
#include "test_inputs.hpp"
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "app/controller.hpp"
#include "app/career_session.hpp"
#include "formats/fbm.hpp"
#include "game/messages.hpp"
#include "game/balance_plan.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "ui/settings.hpp"
#include "ui/flow.hpp"

using namespace bmh;

namespace {
void check(bool ok,const char* message) {
    if(!ok) throw std::runtime_error(message);
}

void start_career(const std::filesystem::path& data,ui::StartupFlow& flow,
                  std::unique_ptr<app::CareerSession>& session,uint64_t& revision,
                  openbmh::audio::Playback& playback,std::optional<uint64_t>& audio_session,
                  int difficulty=2) {
    flow.click(150,215);
    check(flow.screen_name()=="setup","career difficulty is selected on the real setup screen");
    check(difficulty>=0&&difficulty<=9,"test career difficulty is in the setup range");
    const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
    const int button=difficulty<flow.options().difficulty?1:5;
    const int x=int(exe.u16(::bmh::layout::Setup_FormatError_RuntimeField3+button*2))+1;
    const int y=int(exe.u8(::bmh::layout::Setup_FormatError_RuntimeField4+button))+1;
    for(int guard=0;flow.options().difficulty!=difficulty&&guard<10;++guard) {
        const auto changed=flow.click(x,y);
        check(changed.changed,"original setup difficulty arrow accepts the requested test level");
    }
    check(flow.options().difficulty==difficulty,"real setup options carry the requested career difficulty");
    flow.click(5,225,true);
    flow.click(5,225,true);
    const auto start=flow.click(90,169);
    const auto error=app::serve_flow_request(flow,start,session,data,data,0x51ad10e3,
        revision,playback,audio_session,false);
    check(error.empty()&&session&&flow.screen_name()=="desktop",
          "block editor test starts a production career through Flow/Controller");
}

ui::StadiumRequest request_from(const ui::StadiumState& state,ui::StadiumRequestKind kind,
                                int slot,uint32_t total,uint8_t features) {
    ui::StadiumRequest request;
    request.kind=kind;
    request.manager_slot=state.manager_slot;
    request.runtime_club=state.runtime_club;
    request.stable_club_identity=state.stable_club_identity;
    request.revision=state.revision;
    request.scope=state.scope;
    request.sequence=state.sequence;
    request.block_slot=slot;
    request.block_total_places=total;
    request.block_features=features;
    request.object=-1;request.option=-1;request.price=0;request.accept_tip=false;
    if(kind!=ui::StadiumRequestKind::expansion_select) {
        check(state.block_expansion.has_value(),"block action binds an accepted editor quote");
        request.expected_block_quote=state.block_expansion->quote;
    }
    return request;
}

ui::StadiumState stadium_state(app::CareerSession& session,uint64_t revision) {
    const auto preparation=session.preparation_projection(revision);
    check(preparation.stadium.has_value(),"preparation projection includes the stadium view");
    return *preparation.stadium;
}

uint32_t read32(const std::vector<uint8_t>& bytes,size_t at) {
    return uint32_t(bytes.at(at))|uint32_t(bytes.at(at+1))<<8|
        uint32_t(bytes.at(at+2))<<16|uint32_t(bytes.at(at+3))<<24;
}

void write32(std::vector<uint8_t>& bytes,size_t at,uint32_t value) {
    for(unsigned i=0;i<4;++i) bytes.at(at+i)=uint8_t(value>>(i*8));
}

void test_panel_setting_projection_uses_real_writer(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data,flow,session,revision,playback,audio_session);

    const int manager=flow.desktop()->state().manager_slot;
    const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
    const auto runtime=session->club_modal_runtime_checkpoint();
    const auto effective_byte=[&](uint32_t address) {
        if(session->state().memory.has(address)) return session->state().memory.u8(address);
        const auto found=runtime.find(address);
        return found==runtime.end()?exe.u8(address):found->second;
    };
    const auto initial=session->preparation_projection(revision);
    check(initial.stadium && initial.stadium->solid_panel_background==(effective_byte(::bmh::layout::SolidPanelBackground)!=0),
          "initial stadium style follows memory/runtime/EXE byte precedence");
    constexpr size_t l7=size_t(11*9+6);
    auto settings_state=ui::settings_from_game(session->state(),manager,revision,exe);
    check(settings_state.cells[l7]<=1 && settings_state.display_cells &&
          !((*settings_state.display_cells)[l7]&0x80),
          "real L7 stadium-panel setting is available in this career");
    ui::SettingsScreen screen(data,settings_state);
    const auto before_game=session->state();
    const auto before_native=save_stream(session->state());
    const auto before_w=session->rand();
    const auto before_g=session->game();
    check(screen.click(67+6*24+5,25+11*10+5)&&screen.pending()&&
          screen.pending()->row==11&&screen.pending()->column==6,
          "SettingsScreen emits the real L7 request for a2560");
    const auto request=*screen.pending();
    std::string error;
    check(session->dispatch_settings(request,revision,error)&&error.empty(),
          "CareerSession accepts the source L7 write through its supported settings path");
    screen.acknowledge(ui::settings_from_game(session->state(),manager,revision,exe));
    const auto after_native=save_stream(session->state());
    auto expected=before_game;
    expected.memory.set8(::bmh::layout::SolidPanelBackground,uint8_t(request.expected_raw[l7]^1));
    const auto projected=session->preparation_projection(revision);
    check(projected.stadium && projected.stadium->solid_panel_background==
              (session->state().memory.u8(::bmh::layout::SolidPanelBackground)!=0) &&
          after_native==save_stream(expected) && after_native!=before_native &&
          session->rand().seed()==before_w.seed() && session->game().seed()==before_g.seed() &&
          session->rand().calls()==before_w.calls() && session->game().calls()==before_g.calls(),
          "a2560 projection follows the acknowledged real writer; only Native L7 changes, RNG stays fixed");
}

bool same_offer(const StandBlockOffer& a,const StandBlockOffer& b) {
    return a.percent==b.percent&&a.base_price==b.base_price&&a.offered_price==b.offered_price&&
        a.price==b.price&&a.base_days==b.base_days&&a.construction_days==b.construction_days&&
        a.decision==b.decision;
}

void test_preview_cancel_zero_and_cross_family(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data,flow,session,revision,playback,audio_session);

    const int manager=flow.desktop()->state().manager_slot;
    const size_t rec=size_t(manager)*0xb0c;
    const auto source_exe=load_le(ui::asset_path(data,"BMH.exe"));
    const auto runtime=session->club_modal_runtime_checkpoint();
    const auto source_byte=[&](uint32_t address) {
        if(session->state().memory.has(address))return session->state().memory.u8(address);
        const auto found=runtime.find(address);
        return found==runtime.end()?source_exe.u8(address):found->second;
    };
    const auto unmodified_panel_projection=stadium_state(*session,revision);
    check(unmodified_panel_projection.solid_panel_background==(source_byte(::bmh::layout::SolidPanelBackground)!=0),
          "stadium projection uses effective memory/runtime/EXE a2560 precedence");
    const Bytes native_before_panel_projection=save_stream(session->state());
    const auto w_before_panel_projection=session->rand();
    const auto g_before_panel_projection=session->game();
    const auto repeated_panel_projection=stadium_state(*session,revision);
    check(repeated_panel_projection.solid_panel_background==unmodified_panel_projection.solid_panel_background&&
          save_stream(session->state())==native_before_panel_projection&&
          session->rand().seed()==w_before_panel_projection.seed()&&
          session->game().seed()==g_before_panel_projection.seed()&&
          session->rand().calls()==w_before_panel_projection.calls()&&
          session->game().calls()==g_before_panel_projection.calls(),
          "read-only a2560 projection leaves Native and both RNGs unchanged");
    constexpr int slot=1; // B; Source maximum 18,000 places.
    session->state().manager_records.at(rec+0x34+slot)=0;
    session->state().manager_records.at(rec+0x44+slot)=0;
    session->state().manager_records.at(rec+0x4c+slot)=10;
    session->state().manager_records.at(rec+0x54+slot)=0;
    write32(session->state().manager_records,rec+0x314,100000000);
    write32(session->state().manager_records,rec+0x31c,0);
    session->state().manager_records.at(rec+0x171)=0xff; // sponsor share must not be consumed by A–H.
    session->state().manager_records.at(rec+0x2ac)=0;

    const auto stadium_item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& item){return item.kind==22&&!item.disabled;});
    check(stadium_item!=flow.desktop()->state().items.end(),"real desktop exposes the stadium menu");
    const auto opening=flow.click(stadium_item->x+stadium_item->width/2,
                                  stadium_item->y+stadium_item->height/2);
    check(opening.request==ui::FlowRequest::submenu_action&&
          std::holds_alternative<ui::StadiumRequest>(opening.submenu_action),
          "stadium entry is an actual typed Flow request");
    auto error=app::serve_flow_request(flow,opening,session,data,data,0x51ad10e3,
        revision,playback,audio_session,false);
    check(error.empty()&&flow.in_stadium(),"controller accepts the stadium owner before block actions");

    const Bytes native_after_open=save_stream(session->state());
    const auto w_after_open=session->rand();
    const auto g_after_open=session->game();
    auto page=stadium_state(*session,revision);
    check(page.manager_slot==manager&&page.blocks[size_t(slot)].construction_weeks==0&&
          page.blocks[size_t(slot)].flags==0&&page.blocks[size_t(slot)].capacity_level==10,
          "controlled idle B row projects its live Source record");
    const int source_team=int16_t(uint16_t(session->state().manager_records.at(rec+0x290))|
                                  uint16_t(session->state().manager_records.at(rec+0x291))<<8);
    const int source_league=source_team>0x164?0:team_league(source_team);
    check(page.source_level==session->state().level&&
          page.season_tickets==uint16_t(session->state().manager_records.at(rec+0x2ac))+
              (uint16_t(session->state().manager_records.at(rec+0x2ad))<<8)&&
          page.decorated_stadium_header==!page.plain_background&&
          page.solid_panel_background==unmodified_panel_projection.solid_panel_background&&
          page.solid_panel_background==(source_byte(::bmh::layout::SolidPanelBackground)!=0)&&
          page.plain_background==(source_byte(::bmh::layout::PlainBackground)!=0)&&page.compact_dialog_frame==(source_byte(::bmh::layout::CompactDialogFrame)!=0)&&
          page.gradient_text==(source_byte(::bmh::layout::TextGradientDisabled)==0&&source_byte(::bmh::layout::TextGradientMode)==2)&&
          page.live_league_round==session->state().league_rounds.at(size_t(source_league))&&
          page.ticket_refund_rate==std::bit_cast<int32_t>(source_exe.u32(::bmh::layout::CareerSession_FormatError_RuntimeField5+uint32_t(source_league)*4)),
          "stadium projection exposes separate Source style and immutable compensation context");

    GameRandom expected_header=session->game();
    expected_header.set_seed(int32_t(session->state().memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
    const int expected_header_value=expected_header.range(3,10);
    auto select=request_from(page,ui::StadiumRequestKind::expansion_select,slot,0,0);
    select.expected_block_quote.reset();
    check(session->dispatch_stadium(select,revision,error)&&error.empty(),
          "typed block selection opens a Source quote owner");
    page=stadium_state(*session,revision);
    check(page.block_expansion&&page.block_expansion->quote.slot==StadiumBlockSlot::B&&
          page.block_expansion->quote.current_capacity_thousands==10&&
          page.block_expansion->header_text_variant==expected_header_value,
          "accepted B editor binds the live capacity and draws its header variant once");
    check(page.block_expansion->quote.price_per_1000==375000&&page.block_expansion->quote.base_weeks==37,
          "B uses the literal Source A-H table entries 9 and 23");
    const auto& initial_block=*page.block_expansion;
    const auto amount_text=[&](uint32_t amount) {
        return format_number(session->state(),int32_t(amount),0x181)+source_exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField);
    };
    check(initial_block.current_places_text==format_number(session->state(),10000,5)&&
          initial_block.new_places_text==format_number(session->state(),0,5)&&
          initial_block.target_places_text==format_number(session->state(),10000,5)&&
          initial_block.per_thousand_cost_text==format_number(session->state(),375000,0x181)&&
          initial_block.new_places_cost_text==amount_text(initial_block.quote.new_places_cost)&&
          initial_block.seats_cost_text==amount_text(initial_block.quote.seats_cost)&&
          initial_block.roof_cost_text==amount_text(initial_block.quote.roof_cost)&&
          initial_block.total_cost_text==amount_text(initial_block.quote.base_price)&&
          initial_block.discount_text==source_exe.cstring(::bmh::layout::CareerSession_FormatNumber_TextPointer)+
              format_number(session->state(),100-initial_block.quote.company_price_percent,1)+
              source_exe.cstring(::bmh::layout::CareerSession_FormatNumber_ExecutableField2),
          "editor projection uses exact Original amount width, currency and discount fragments");
    check(save_stream(session->state())==native_after_open&&
          session->rand().seed()==w_after_open.seed()&&session->rand().calls()==w_after_open.calls()&&
          session->game().seed()==expected_header.seed()&&session->game().calls()==expected_header.calls(),
          "editor entry changes no Native/W state and consumes only its Source G header draw");

    const Bytes native_editor=save_stream(session->state());
    const auto w_editor=session->rand();
    const auto g_editor=session->game();
    auto changed_total=request_from(page,ui::StadiumRequestKind::expansion_set_total,slot,15333,0);
    check(session->dispatch_stadium(changed_total,revision,error)&&error.empty(),
          "manual nonmultiple TOTAL places are accepted as a draft");
    page=stadium_state(*session,revision);
    const auto& total_quote=page.block_expansion->quote;
    check(total_quote.total_target_places==15333&&total_quote.new_places==5333&&
          total_quote.new_places_cost==5*total_quote.price_per_1000,
          "manual total retains 15,333 while Source floors only the new-place thousand units");
    check(save_stream(session->state())==native_editor&&session->rand().seed()==w_editor.seed()&&
          session->rand().calls()==w_editor.calls()&&session->game().seed()==g_editor.seed()&&
          session->game().calls()==g_editor.calls(),
          "total edit remains a Native- and RNG-neutral staged draft");

    auto seats=request_from(page,ui::StadiumRequestKind::expansion_toggle_feature,slot,15333,1);
    check(session->dispatch_stadium(seats,revision,error)&&error.empty(),"absent seat feature can be staged");
    page=stadium_state(*session,revision);
    const auto& seat_quote=page.block_expansion->quote;
    check(seat_quote.desired_features==1&&seat_quote.seats_cost==
          (seat_quote.price_per_1000*15*45)/100&&seat_quote.base_price==
          (seat_quote.new_places_cost+seat_quote.seats_cost)*uint32_t(seat_quote.company_price_percent)/100,
          "seat retrofit charges the whole TOTAL before the company discount");
    check(save_stream(session->state())==native_editor&&session->rand().seed()==w_editor.seed()&&
          session->rand().calls()==w_editor.calls()&&session->game().seed()==g_editor.seed()&&
          session->game().calls()==g_editor.calls(),
          "feature toggle remains presentation state without Native or RNG writes");
    auto roof=request_from(page,ui::StadiumRequestKind::expansion_toggle_feature,slot,15333,3);
    check(session->dispatch_stadium(roof,revision,error)&&error.empty(),
          "the remaining absent roof bit is staged as a separate one-bit action");
    page=stadium_state(*session,revision);
    check(page.block_expansion->desired_features==3&&save_stream(session->state())==native_editor&&
          session->rand().seed()==w_editor.seed()&&session->rand().calls()==w_editor.calls()&&
          session->game().seed()==g_editor.seed()&&session->game().calls()==g_editor.calls(),
          "sequential 0-to-1-to-3 feature edits remain Native/RNG neutral");

    const auto stable_native=save_stream(session->state());
    const auto stable_w=session->rand();const auto stable_g=session->game();
    const uint64_t stable_revision=revision;
    ui::StadiumRequest legacy_open;
    legacy_open.kind=ui::StadiumRequestKind::open;
    legacy_open.manager_slot=manager;legacy_open.runtime_club=page.runtime_club;
    legacy_open.revision=revision;legacy_open.object=-1;legacy_open.option=-1;
    legacy_open.price=0;legacy_open.accept_tip=false;
    std::string legacy_error;
    check(!session->dispatch_stadium(legacy_open,revision,legacy_error)&&!legacy_error.empty()&&
          revision==stable_revision&&save_stream(session->state())==stable_native&&
          session->rand().seed()==stable_w.seed()&&session->rand().calls()==stable_w.calls()&&
          session->game().seed()==stable_g.seed()&&session->game().calls()==stable_g.calls()&&
          session->preparation_projection(revision).stadium->block_expansion,
          "legacy absent-envelope open cannot discard an active block editor or consume Source G");
    const ui::DeskRequest nested_balance{ui::DeskRequestKind::open_menu,manager,-1,28,-1,revision};
    std::string nested_error;
    const auto nested=session->open_balance_plan(nested_balance,revision,flow.stadium()->render(),nested_error);
    check(!nested&&!nested_error.empty()&&revision==stable_revision&&
          save_stream(session->state())==stable_native&&session->rand().seed()==stable_w.seed()&&
          session->rand().calls()==stable_w.calls()&&session->game().seed()==stable_g.seed()&&
          session->game().calls()==stable_g.calls()&&session->preparation_projection(revision).stadium->block_expansion,
          "nested balance entry is rejected without consuming the block owner or mutating session state");
    const ui::DeskRequest nested_other_games{ui::DeskRequestKind::open_menu,manager,-1,5,-1,revision};
    const auto other_games=session->open_other_games(nested_other_games,revision,
        flow.stadium()->render(),nested_error);
    check(!other_games&&nested_error=="other games: stale or unavailable menu-5 owner"&&
          revision==stable_revision&&save_stream(session->state())==stable_native&&
          session->rand().seed()==stable_w.seed()&&session->rand().calls()==stable_w.calls()&&
          session->game().seed()==stable_g.seed()&&session->game().calls()==stable_g.calls()&&
          session->preparation_projection(revision).stadium->block_expansion&&
          session->preparation_projection(revision).stadium->sequence==page.sequence,
          "menu-5 entry cannot replace a staged A-H editor or consume its Native/RNG owner");
    const ui::DeskRequest kickoff{ui::DeskRequestKind::advance_turn,manager,-1,-1,-1,revision};
    const auto turn=session->dispatch_kickoff(kickoff,revision,nested_error);
    check(turn.kind==TurnDeskKind::Failed&&revision==stable_revision&&
          save_stream(session->state())==stable_native&&session->rand().seed()==stable_w.seed()&&
          session->rand().calls()==stable_w.calls()&&session->game().seed()==stable_g.seed()&&
          session->game().calls()==stable_g.calls(),
          "kickoff cannot bypass a staged block editor owner");

    auto cancel=request_from(page,ui::StadiumRequestKind::expansion_cancel,slot,15333,3);
    check(session->dispatch_stadium(cancel,revision,error)&&error.empty(),"block editor cancellation is accepted");
    page=stadium_state(*session,revision);
    check(!page.block_expansion&&save_stream(session->state())==native_after_open&&
          session->rand().seed()==w_after_open.seed()&&session->rand().calls()==w_after_open.calls()&&
          session->game().seed()==expected_header.seed()&&session->game().calls()==expected_header.calls(),
          "cancel restores the exact post-stadium-open Native and Source RNG checkpoints");

    page=stadium_state(*session,revision);
    select=request_from(page,ui::StadiumRequestKind::expansion_select,slot,0,0);
    check(session->dispatch_stadium(select,revision,error)&&error.empty(),"zero-price control enters B editor");
    page=stadium_state(*session,revision);
    const auto zero_before=save_stream(session->state());
    const auto zero_w=session->rand();const auto zero_g=session->game();
    auto confirm=request_from(page,ui::StadiumRequestKind::expansion_confirm,slot,10000,0);
    check(session->dispatch_stadium(confirm,revision,error)&&error.empty(),"zero-price confirm exits without an offer");
    page=stadium_state(*session,revision);
    check(!page.block_expansion&&save_stream(session->state())==zero_before&&
          session->rand().seed()==zero_w.seed()&&session->rand().calls()==zero_w.calls()&&
          session->game().seed()==zero_g.seed()&&session->game().calls()==zero_g.calls(),
          "zero-price confirmation clears the modal without payment, tip draws or Native writes");
}

void test_literal_eight_block_tables(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data,flow,session,revision,playback,audio_session);
    const int manager=flow.desktop()->state().manager_slot;
    const size_t rec=size_t(manager)*0xb0c;
    for(size_t slot=0;slot<8;++slot) {
        session->state().manager_records.at(rec+0x34+slot)=0;
        session->state().manager_records.at(rec+0x44+slot)=0;
        session->state().manager_records.at(rec+0x4c+slot)=0;
        session->state().manager_records.at(rec+0x54+slot)=0;
    }
    const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& entry){return entry.kind==22&&!entry.disabled;});
    check(item!=flow.desktop()->state().items.end(),"all-eight fixture opens the genuine stadium entry");
    const auto opening=flow.click(item->x+item->width/2,item->y+item->height/2);
    check(app::serve_flow_request(flow,opening,session,data,data,0x51ad10e3,revision,
        playback,audio_session,false).empty(),"all-eight fixture gets a stadium owner");
    const auto executable = load_le(ui::asset_path(data, "BMH.exe"));
    std::string error;
    for(int slot=0;slot<8;++slot) {
        auto page=stadium_state(*session,revision);
        auto select=request_from(page,ui::StadiumRequestKind::expansion_select,slot,0,0);
        select.expected_block_quote.reset();
        check(session->dispatch_stadium(select,revision,error)&&error.empty(),"each literal A-H slot opens");
        page=stadium_state(*session,revision);
        const auto& quote=page.block_expansion->quote;
        check(quote.price_per_1000==executable.u32(layout::CareerSession_DispatchStadium_RuntimeField + (size_t(slot)+8)*4)&&
              quote.base_weeks==executable.u8(layout::CareerSession_DispatchStadium_RuntimeField2 + (size_t(slot)+22)*5),
              "each A-H slot matches its externally supplied executable table");
        auto cancel=request_from(page,ui::StadiumRequestKind::expansion_cancel,slot,
            quote.total_target_places,quote.desired_features);
        check(session->dispatch_stadium(cancel,revision,error)&&error.empty(),"each table quote cancels cleanly");
    }
}

void test_no_tip_commit_exact_source_fields(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data,flow,session,revision,playback,audio_session);
    const int manager=flow.desktop()->state().manager_slot;
    const size_t rec=size_t(manager)*0xb0c;
    constexpr int slot=1;
    session->state().manager_records.at(rec+0x34+slot)=0;
    session->state().manager_records.at(rec+0x44+slot)=0;
    session->state().manager_records.at(rec+0x4c+slot)=10;
    session->state().manager_records.at(rec+0x54+slot)=0;
    write32(session->state().manager_records,rec+0x314,100000000);
    write32(session->state().manager_records,rec+0x31c,0);
    session->state().manager_records.at(rec+0x171)=0xff;
    session->state().manager_records.at(rec+0x2ac)=0;
    const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& entry){return entry.kind==22&&!entry.disabled;});
    check(item!=flow.desktop()->state().items.end(),"commit fixture has the stadium entry");
    const auto open=flow.click(item->x+item->width/2,item->y+item->height/2);
    check(app::serve_flow_request(flow,open,session,data,data,0x51ad10e3,revision,playback,audio_session,false).empty(),
          "controller accepts stadium before the block commit fixture");
    auto page=stadium_state(*session,revision);
    std::string error;
    auto request=request_from(page,ui::StadiumRequestKind::expansion_select,slot,0,0);
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"block commit fixture enters B");
    page=stadium_state(*session,revision);
    request=request_from(page,ui::StadiumRequestKind::expansion_set_total,slot,15333,0);
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"block commit fixture accepts total");
    page=stadium_state(*session,revision);
    request=request_from(page,ui::StadiumRequestKind::expansion_toggle_feature,slot,15333,1);
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"block commit fixture stages seats as one bit");
    page=stadium_state(*session,revision);
    request=request_from(page,ui::StadiumRequestKind::expansion_toggle_feature,slot,15333,3);
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"block commit fixture stages roof as the second bit");
    page=stadium_state(*session,revision);
    const auto quote=page.block_expansion->quote;
    check(quote.base_price>0&&quote.base_weeks>0&&quote.total_target_places==15333,
          "nonzero A-H offer has the immutable accepted manual-total quote");

    WatcomRandom expected_w=session->rand();
    const StandBlockOffer expected_pending=prepare_stand_block_offer(quote.base_price,quote.base_weeks,expected_w);
    request=request_from(page,ui::StadiumRequestKind::expansion_confirm,slot,15333,3);
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"first Ausbauen confirmation presents the Source offer");
    page=stadium_state(*session,revision);
    check(page.block_expansion&&page.block_expansion->offer&&
          same_offer(*page.block_expansion->offer,expected_pending)&&
          page.construction_tip_dialog&&
          page.construction_tip_dialog->source_manager_flags==uint8_t(manager)&&
          page.construction_tip_dialog->source_flags==0x204&&
          page.construction_tip_dialog->compact_dialog_frame==page.compact_dialog_frame&&
          page.construction_tip_dialog->body==message_text(session->state(),::bmh::layout::CareerSession_FormatError_TextPointer8)+
              format_number(session->state(),expected_pending.percent,1)+message_text(session->state(),::bmh::layout::CareerSession_FormatError_TextPointer9)&&
          page.construction_tip_price==expected_pending.offered_price&&
          session->rand().seed()==expected_w.seed()&&session->rand().calls()==expected_w.calls(),
          "confirmation retains editor/quote, uses the plain Source manager/0x204 question envelope, and draws once");

    ui::StadiumRequest legacy_open;
    legacy_open.kind=ui::StadiumRequestKind::open;legacy_open.manager_slot=manager;
    legacy_open.runtime_club=page.runtime_club;legacy_open.revision=revision;
    legacy_open.object=-1;legacy_open.option=-1;
    const Bytes pending_native=save_stream(session->state());
    const auto pending_w=session->rand();const auto pending_g=session->game();
    const uint64_t pending_revision=revision;
    check(!session->dispatch_stadium(legacy_open,revision,error)&&!error.empty()&&
          revision==pending_revision&&save_stream(session->state())==pending_native&&
          session->rand().seed()==pending_w.seed()&&session->rand().calls()==pending_w.calls()&&
          session->game().seed()==pending_g.seed()&&session->game().calls()==pending_g.calls()&&
          session->preparation_projection(revision).stadium->block_expansion->offer,
          "legacy absent-envelope open cannot discard a pending construction tip");

    const auto deadline_native=save_stream(session->state());
    const auto deadline_w=session->rand();const auto deadline_g=session->game();
    const uint64_t deadline_revision=revision;
    const GameState deadline_state=session->state();
    const auto deadline_runtime=session->club_modal_runtime_checkpoint();
    const auto set_deadline_word=[&](uint32_t address,uint32_t value) {
        if(session->state().memory.has(address+3)) session->state().memory.set32(address,value);
        else {
            auto runtime=session->club_modal_runtime_checkpoint();
            for(unsigned i=0;i<4;++i) runtime[address+i]=uint8_t(value>>(i*8));
            session->restore_club_modal_runtime(std::move(runtime));
        }
    };
    set_deadline_word(::bmh::layout::CareerSession_DispatchStrengths_ExecutableField2,1);set_deadline_word(::bmh::layout::CareerSession_FormatError_ExecutableField2,1);
    const bool not_expired=session->stadium_tip_deadline_expired();
    set_deadline_word(::bmh::layout::CareerSession_FormatError_ExecutableField2,0);
    const bool expired=session->stadium_tip_deadline_expired();
    session->state()=deadline_state;
    session->restore_club_modal_runtime(deadline_runtime);
    check(!not_expired&&expired&&revision==deadline_revision&&save_stream(session->state())==deadline_native&&
          session->rand().seed()==deadline_w.seed()&&session->rand().calls()==deadline_w.calls()&&
          session->game().seed()==deadline_g.seed()&&session->game().calls()==deadline_g.calls(),
          "pending block tip uses the effective Source deadline without changing Native or RNG");

    auto resolved=expected_pending;
    resolve_stand_block_offer(resolved,false,expected_w);
    const GameState before_commit=session->state();
    const auto game_before=session->game();
    request=request_from(page,ui::StadiumRequestKind::expansion_answer_tip,slot,15333,3);
    request.accept_tip=false;
    auto malformed=request;
    malformed.block_total_places++;
    const auto old_revision=revision;
    check(!session->dispatch_stadium(malformed,revision,error)&&!error.empty()&&revision==old_revision&&
          session->state().manager_records==before_commit.manager_records&&
          session->rand().seed()==expected_w.seed()&&session->rand().calls()==expected_w.calls()&&
          session->game().seed()==game_before.seed()&&session->game().calls()==game_before.calls()&&
          same_offer(*session->preparation_projection(revision).stadium->block_expansion->offer,expected_pending),
          "forged answer total rejects while preserving the pending owner and consumed offer draw");
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"NO resolves and commits the accepted A-H quote");
    page=stadium_state(*session,revision);
    const size_t current=rec+0x4c+slot;
    const size_t weeks=rec+0x34+slot,flags=rec+0x44+slot,future=rec+0x54+slot;
    const size_t money=rec+0x314,expense=rec+0x8a0;
    GameState expected=before_commit;
    expected.manager_records.at(weeks)=resolved.construction_days;
    expected.manager_records.at(flags)=uint8_t(expected.manager_records.at(flags)|(uint8_t(3)<<4));
    expected.manager_records.at(future)=uint8_t(15333/1000);
    const uint8_t usable=[&] {
        uint8_t sum=0;
        for(size_t i=0;i<8;++i)
            if(expected.manager_records.at(rec+0x34+i)==0)
                sum=uint8_t(sum+expected.manager_records.at(rec+0x4c+i));
        return sum;
    }();
    expected.manager_records.at(rec+0x5c)=usable;
    if(expected.level!=0) {
        write32(expected.manager_records,money,read32(expected.manager_records,money)-quote.base_price);
        write32(expected.manager_records,expense,read32(expected.manager_records,expense)+quote.base_price);
    }
    check(!page.block_expansion&&save_stream(session->state())==save_stream(expected)&&
          session->state().manager_records.at(current)==before_commit.manager_records.at(current)&&
          session->state().manager_records.at(0x171+rec)==0xff,
          "NO commit matches the complete expected Native stream and writes only Source build/flags/future/account/stat fields");
    check(session->rand().seed()==expected_w.seed()&&session->rand().calls()==expected_w.calls()&&
          session->game().seed()==game_before.seed()&&session->game().calls()==game_before.calls(),
          "NO construction uses base price/weeks with no answer-shortening or sponsor RNG draws");
}

void test_yes_level_zero_and_funds_refusal(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data,flow,session,revision,playback,audio_session,0);
    check(session->state().level==0,"level-zero YES career was initialized from its real setup option");
    const int manager=flow.desktop()->state().manager_slot;
    const size_t rec=size_t(manager)*0xb0c;
    for(size_t slot=0;slot<8;++slot) {
        session->state().manager_records.at(rec+0x34+slot)=0;
        session->state().manager_records.at(rec+0x44+slot)=0;
        session->state().manager_records.at(rec+0x54+slot)=0;
    }
    session->state().manager_records.at(rec+0x4c+1)=10;
    session->state().manager_records.at(rec+0x4c+2)=0;
    session->state().manager_records.at(rec+0x2ac)=0;
    session->state().manager_records.at(rec+0x171)=0;
    write32(session->state().manager_records,rec+0x314,100000000);
    const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& entry){return entry.kind==22&&!entry.disabled;});
    check(item!=flow.desktop()->state().items.end(),"YES fixture has a genuine stadium menu entry");
    const auto opening=flow.click(item->x+item->width/2,item->y+item->height/2);
    check(opening.request==ui::FlowRequest::submenu_action&&
          std::holds_alternative<ui::StadiumRequest>(opening.submenu_action)&&
          std::get<ui::StadiumRequest>(opening.submenu_action).kind==ui::StadiumRequestKind::open&&
          flow.matches_pending_stadium(std::get<ui::StadiumRequest>(opening.submenu_action)),
          "YES menu entry emits the actual pending typed Stadium open request");
    check(app::serve_flow_request(flow,opening,session,data,data,0x51ad10e3,revision,
        playback,audio_session,false).empty(),"YES fixture opens a real stadium owner");
    std::string error;
    auto page=stadium_state(*session,revision);
    check(flow.in_stadium()&&flow.stadium()&&!flow.stadium()->pending()&&page.opened&&page.scope!=0&&
          page.sequence==1&&page.revision==revision,
          "YES menu open is acknowledged into the active nonzero Stadium scope");
    auto request=request_from(page,ui::StadiumRequestKind::expansion_select,1,0,0);
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"YES path opens B");
    page=stadium_state(*session,revision);
    request=request_from(page,ui::StadiumRequestKind::expansion_toggle_feature,1,10000,1);
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"YES path stages one priced feature");
    page=stadium_state(*session,revision);
    const auto quote=page.block_expansion->quote;
    check(quote.base_price>0,"YES fixture binds a positive-price accepted quote");
    WatcomRandom expected_random=session->rand();
    const StandBlockOffer expected_pending=prepare_stand_block_offer(quote.base_price,quote.base_weeks,expected_random);
    request=request_from(page,ui::StadiumRequestKind::expansion_confirm,1,10000,1);
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"YES path reaches its Source question");
    page=stadium_state(*session,revision);
    check(page.construction_tip_dialog&&page.source_level==0,"question preserves the explicit level-0 context");
    auto accepted=expected_pending;
    resolve_stand_block_offer(accepted,true,expected_random);
    GameState expected=session->state();
    auto exe=load_le(ui::asset_path(data,"BMH.exe"));
    StadiumBlockExpansionConstants constants;
    for(unsigned i=0;i<8;++i) {
        constants.price_per_1000[i]=exe.u32(::bmh::layout::CareerSession_DispatchStadium_RuntimeField+(i+8)*4);
        constants.weeks[i]=exe.u8(::bmh::layout::CareerSession_DispatchStadium_RuntimeField2+(i+22)*5);
        constants.max_places[i]=uint32_t(exe.u8(::bmh::layout::CareerSession_Invalid_RuntimeField+i))*1000;
    }
    check(build_stadium_block_expansion(expected,manager,StadiumBlockSlot::B,constants,
        quote,accepted,expected_random),"independent Source Engine simulation accepts YES at level 0");
    const auto game_before=session->game();
    request=request_from(page,ui::StadiumRequestKind::expansion_answer_tip,1,10000,1);
    request.accept_tip=true;
    check(session->dispatch_stadium(request,revision,error)&&error.empty(),"App accepts the typed YES answer");
    check(!stadium_state(*session,revision).block_expansion&&
          save_stream(session->state())==save_stream(expected)&&
          session->rand().seed()==expected_random.seed()&&session->rand().calls()==expected_random.calls()&&
          session->game().seed()==game_before.seed()&&session->game().calls()==game_before.calls(),
          "level-0 YES matches the Source engine's complete Native and RNG result");

    // The level-one insufficient-funds control gets its own real setup and
    // attached projection so the Source difficulty cannot become stale.
    ui::StartupFlow funds_flow(data);
    std::unique_ptr<app::CareerSession> funds_session;
    uint64_t funds_revision=0;
    std::optional<uint64_t> funds_audio_session;
    start_career(data,funds_flow,funds_session,funds_revision,playback,funds_audio_session,1);
    check(funds_session->state().level==1,"level-one funds career was initialized from its real setup option");
    const int funds_manager=funds_flow.desktop()->state().manager_slot;
    const size_t funds_rec=size_t(funds_manager)*0xb0c;
    for(size_t slot=0;slot<8;++slot) {
        funds_session->state().manager_records.at(funds_rec+0x34+slot)=0;
        funds_session->state().manager_records.at(funds_rec+0x44+slot)=0;
        funds_session->state().manager_records.at(funds_rec+0x54+slot)=0;
    }
    funds_session->state().manager_records.at(funds_rec+0x4c+2)=0;
    funds_session->state().manager_records.at(funds_rec+0x2ac)=0;
    funds_session->state().manager_records.at(funds_rec+0x171)=0;
    const auto funds_item=std::find_if(funds_flow.desktop()->state().items.begin(),funds_flow.desktop()->state().items.end(),
        [](const ui::DeskItem& entry){return entry.kind==22&&!entry.disabled;});
    check(funds_item!=funds_flow.desktop()->state().items.end(),"funds fixture has a genuine stadium menu entry");
    const auto funds_opening=funds_flow.click(funds_item->x+funds_item->width/2,
                                              funds_item->y+funds_item->height/2);
    check(funds_opening.request==ui::FlowRequest::submenu_action&&
          std::holds_alternative<ui::StadiumRequest>(funds_opening.submenu_action)&&
          std::get<ui::StadiumRequest>(funds_opening.submenu_action).kind==ui::StadiumRequestKind::open&&
          funds_flow.matches_pending_stadium(std::get<ui::StadiumRequest>(funds_opening.submenu_action)),
          "funds menu entry emits the actual pending typed Stadium open request");
    check(app::serve_flow_request(funds_flow,funds_opening,funds_session,data,data,0x51ad10e3,
        funds_revision,playback,funds_audio_session,false).empty(),"level-one funds fixture opens a real stadium owner");
    page=stadium_state(*funds_session,funds_revision);
    check(funds_flow.in_stadium()&&funds_flow.stadium()&&!funds_flow.stadium()->pending()&&
          page.opened&&page.scope!=0&&page.sequence==1&&page.revision==funds_revision,
          "level-one funds menu open is acknowledged into the active nonzero Stadium scope");
    request=request_from(page,ui::StadiumRequestKind::expansion_select,2,0,0);
    check(funds_session->dispatch_stadium(request,funds_revision,error)&&error.empty(),"funds control opens C");
    page=stadium_state(*funds_session,funds_revision);
    request=request_from(page,ui::StadiumRequestKind::expansion_set_total,2,1000,0);
    check(funds_session->dispatch_stadium(request,funds_revision,error)&&error.empty(),"funds control stages a new place");
    page=stadium_state(*funds_session,funds_revision);
    check(page.block_expansion->quote.base_price>0,"funds refusal has a nonzero independent quote");
    auto funds_expected_random=funds_session->rand();
    (void)prepare_stand_block_offer(page.block_expansion->quote.base_price,
        page.block_expansion->quote.base_weeks,funds_expected_random);
    request=request_from(page,ui::StadiumRequestKind::expansion_confirm,2,1000,0);
    check(funds_session->dispatch_stadium(request,funds_revision,error)&&error.empty(),"funds control opens the tip question");
    page=stadium_state(*funds_session,funds_revision);
    funds_session->state().manager_records.at(funds_rec+0x314)=0;
    funds_session->state().manager_records.at(funds_rec+0x315)=0;
    funds_session->state().manager_records.at(funds_rec+0x316)=0;
    funds_session->state().manager_records.at(funds_rec+0x317)=0;
    write32(funds_session->state().manager_records,funds_rec+0x31c,0);
    const auto refusal_native=save_stream(funds_session->state());
    const auto game_at_refusal=funds_session->game();
    request=request_from(page,ui::StadiumRequestKind::expansion_answer_tip,2,1000,0);
    request.accept_tip=false;
    bool insufficient_notice=false;
    const ConstructionNoticeHook payment_notice=[&](const GameState&,const ConstructionNotice& notice) {
        insufficient_notice=notice.kind==ConstructionNoticeKind::InsufficientFunds;
    };
    check(funds_session->dispatch_stadium(request,funds_revision,error,payment_notice)&&error.empty(),
          "funds refusal remains a consumed Source answer, not a malformed request");
    check(insufficient_notice&&save_stream(funds_session->state())==refusal_native&&
          funds_session->state().manager_records.at(funds_rec+0x34+2)==0&&
          funds_session->state().manager_records.at(funds_rec+0x44+2)==0&&
          funds_session->state().manager_records.at(funds_rec+0x54+2)==0&&
          funds_session->rand().seed()==funds_expected_random.seed()&&funds_session->rand().calls()==funds_expected_random.calls()&&
          funds_session->game().seed()==game_at_refusal.seed()&&funds_session->game().calls()==game_at_refusal.calls()&&
          !stadium_state(*funds_session,funds_revision).block_expansion,
          "insufficient funds preserves complete Native construction/account data and consumes only NO semantics");
}

void test_controller_compensation_ack_rollback(const std::filesystem::path& data) {
    ui::StartupFlow flow(data);
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    start_career(data,flow,session,revision,playback,audio_session);
    const int manager=flow.desktop()->state().manager_slot;
    const size_t rec=size_t(manager)*0xb0c;
    const int team=session->state().manager_teams.at(size_t(manager));
    const int league=team>0x164?0:team_league(team);
    session->state().league_rounds.at(size_t(league))=18; // Source retains negative signed weeks after round 17.
    constexpr int slot=1;
    for(size_t i=0;i<8;++i) {
        session->state().manager_records.at(rec+0x34+i)=0;
        session->state().manager_records.at(rec+0x44+i)=0;
        session->state().manager_records.at(rec+0x4c+i)=0;
        session->state().manager_records.at(rec+0x54+i)=0;
    }
    session->state().manager_records.at(rec+0x4c+slot)=10;
    session->state().manager_records.at(rec+0x314)=0xff;
    session->state().manager_records.at(rec+0x315)=0xff;
    session->state().manager_records.at(rec+0x2ac)=1;
    write32(session->state().manager_records,rec+0x314,100000000);
    write32(session->state().manager_records,rec+0x31c,0);
    session->state().manager_records.at(rec+0x171)=0;

    const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& entry){return entry.kind==22&&!entry.disabled;});
    check(item!=flow.desktop()->state().items.end(),"controller fixture has the stadium menu");
    const auto opened=flow.click(item->x+item->width/2,item->y+item->height/2);
    check(app::serve_flow_request(flow,opened,session,data,data,0x51ad10e3,revision,
        playback,audio_session,false).empty()&&flow.in_stadium(),
        "controller opens the real stadium preparation screen");
    const auto route_click=[&](int x,int y) {
        const auto down=flow.pointer_down(x,y);
        check(down.changed,"real stadium control accepts pointer-down");
        return flow.click(x,y);
    };
    const auto serve=[&](const ui::FlowResult& result,
                         SeasonTicketCompensationNoticeHook compensation={}) {
        return app::serve_flow_request(flow,result,session,data,data,0x51ad10e3,revision,
            playback,audio_session,false,{}, {}, {}, {}, {}, {}, {}, {}, {},
            app::PendingOfferEntryPolicy::legacy_unpresented, {},
            app::HumanBuyerMarketEntryPolicy::legacy_unpresented, {}, {}, {},
            std::move(compensation));
    };
    auto action=route_click(230,22+slot*24+10);
    check(action.request==ui::FlowRequest::submenu_action&&
          std::holds_alternative<ui::StadiumRequest>(action.submenu_action)&&serve(action).empty(),
          "typed live Flow opens A–H editor through controller ACK");
    action=route_click(160,79);
    check(action.request==ui::FlowRequest::submenu_action&&serve(action).empty()&&
          flow.stadium()->state().block_expansion->quote.base_price>0,
          "physical seat toggle makes the controller fixture a positive-price build");
    action=route_click(30,140);
    check(action.request==ui::FlowRequest::submenu_action&&serve(action).empty()&&
          flow.stadium()->state().block_expansion->offer.has_value(),
          "typed build button stages the original pending offer");
    const auto before=save_stream(session->state());
    const auto w_before=session->rand();
    const auto g_before=session->game();
    const auto before_revision=revision;
    action=route_click(200,171); // Original dialog NO button.
    check(action.request==ui::FlowRequest::submenu_action&&
          std::holds_alternative<ui::StadiumRequest>(action.submenu_action),
          "Original tip NO creates one controller-owned typed answer");
    bool notice_called=false;
    bool negative_compensation=false;
    const SeasonTicketCompensationNoticeHook fail_notice=[&](const GameState&,const SeasonTicketCompensationNotice& notice) {
        notice_called=true;
        negative_compensation=notice.signed_amount<0;
        throw std::runtime_error("injected compensation presentation failure");
    };
    (void)serve(action,fail_notice);
    check(notice_called&&negative_compensation&&revision==before_revision&&save_stream(session->state())==before&&
          session->rand().seed()==w_before.seed()&&session->rand().calls()==w_before.calls()&&
          session->game().seed()==g_before.seed()&&session->game().calls()==g_before.calls()&&
          flow.in_stadium()&&flow.stadium()->state().block_expansion&&
          flow.stadium()->state().block_expansion->offer.has_value()&&!flow.stadium()->pending(),
          "post-commit presenter failure rolls back full Native/RNG/revision and retains retryable UI state");

    action=route_click(200,171);
    check(serve(action).empty()&&flow.in_stadium()&&
          !flow.stadium()->state().block_expansion,
          "the same Source answer can retry and commit after the failing presenter is removed");
    check(session->state().manager_records.at(rec+0x34+slot)>0&&
          session->state().manager_records.at(rec+0x5c)==0&&
          session->state().manager_records.at(rec+0x171)==0,
          "successful controller retry commits the block and Source compensation without sponsor usage");
}
}

namespace {
void test_periphery_funds_answer_consumed(const std::filesystem::path& data) {
    ui::StartupFlow flow(data); std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0; openbmh::audio::Playback playback; std::optional<uint64_t> audio;
    start_career(data,flow,session,revision,playback,audio,1);
    const auto bank=*session->preparation_projection(revision).bank;
    const auto bank_item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& entry){return entry.kind==14&&!entry.disabled;});
    flow.click(bank_item->x+bank_item->width/2,bank_item->y+bank_item->height/2);
    flow.click(250,80); flow.click(80,178); flow.text(std::to_string(bank.balance));
    const auto deposit_action=flow.send_key("ENTER");
    const auto deposit=std::get<ui::BankRequest>(deposit_action.submenu_action);
    std::string error;
    check(deposit.amount==bank.balance && deposit.amount>0 && session->dispatch_bank(deposit,revision,error),
          "normal bank deposit creates a poor periphery career without editing manager bytes");
    flow.acknowledge_preparation(session->preparation_projection(revision));
    flow.send_key("ESC"); flow.send_key("ESC");
    const auto item=std::find_if(flow.desktop()->state().items.begin(),flow.desktop()->state().items.end(),
        [](const ui::DeskItem& entry){return entry.kind==22&&!entry.disabled;});
    const auto opening=flow.click(item->x+item->width/2,item->y+item->height/2);
    check(app::serve_flow_request(flow,opening,session,data,data,0x51ad10e3,revision,playback,audio,false).empty(),
          "poor career opens the stadium through its real desktop item");
    const auto click=[&](int x,int y){flow.pointer_down(x,y);return flow.click(x,y);};
    click(30,220); click(160,220);
    const auto quote=click(239,172);
    check(quote.request==ui::FlowRequest::submenu_action &&
          std::get<ui::StadiumRequest>(quote.submenu_action).kind==ui::StadiumRequestKind::quote_periphery,
          "poor career quotes the real parking building before any funds decision");
    check(session->dispatch_stadium(std::get<ui::StadiumRequest>(quote.submenu_action),revision,error),
          "Source quote is allowed even with no account money");
    flow.acknowledge_preparation(session->preparation_projection(revision));
    const auto page=stadium_state(*session,revision);
    check(page.periphery_quote_dialog && page.periphery_quote_dialog->source_flags==1 && page.quoted_price>0,
          "periphery quote projects the exact Original flags1 question");
    const auto before=save_stream(session->state()); const auto w=session->rand(); const auto g=session->game();
    const auto revision_before=revision; bool funds=false;
    const auto answer=click(100,170); const auto request=std::get<ui::StadiumRequest>(answer.submenu_action);
    check(request.kind==ui::StadiumRequestKind::build_periphery && request.price==page.quoted_price,
          "real Original JA retains the unchanged owned quote");
    check(session->dispatch_stadium(request,revision,error,[&](const GameState& current,const ConstructionNotice& event){
        funds=event.kind==ConstructionNoticeKind::InsufficientFunds;
        check(save_stream(current)==before && event.gross_price==uint32_t(page.quoted_price),
              "funds notice observes the exact refused quote before native construction writes");
    }) && funds && error.empty(), "genuine Source funds refusal consumes JA successfully");
    flow.acknowledge_preparation(session->preparation_projection(revision));
    check(revision==revision_before+1 && save_stream(session->state())==before &&
          session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
          session->game().seed()==g.seed() && session->game().calls()==g.calls() &&
          !flow.stadium()->state().quoted_object && !flow.stadium()->pending(),
          "refused periphery JA changes only its modal/revision and preserves complete Native/RNG");
    check(!session->dispatch_stadium(request,revision,error), "consumed poor quote cannot be built twice");
}
}

namespace {
void test_original_stadium_data(const std::filesystem::path& data) {
    const auto original=bmh::test::external_input("stadium.fbm");
    app::CareerSession session(data,0x1789abcd,original);
    uint64_t revision=1;std::string error;
    auto state=stadium_state(session,revision);
    ui::StadiumScreen screen(data,state);
    const auto open=screen.initialize();
    check(session.dispatch_stadium(open,revision,error), "normal Original STADION2 owns its stadium through typed open");
    screen.acknowledge(stadium_state(session,revision));
    const auto before=save_stream(session.state());const auto w=session.rand();const auto g=session.game();
    const auto accept=[&](ui::StadiumRequest request) {
        check(session.dispatch_stadium(request,revision,error)&&error.empty(),error.c_str());
        screen.acknowledge(stadium_state(session,revision));
    };
    accept(screen.click(265,220));
    check(save_stream(session.state())==before && screen.state().data &&
          screen.state().data->flags==0xbf && screen.state().data->ticket_price==25,
          "data entry reads the uploaded REL/25 state and leaves the complete Native stream unchanged");
    const auto page=*screen.state().data;
    check(page.series[0].size()==15 && page.series[1].size()==15 && page.series[2].size()==15 &&
          page.series[3].size()==15 && page.series[4].size()==43 && page.series[5].size()==43,
          "Original six histories keep their fifteen-match / forty-three-week Source sample counts");
    check(stadium_required_attendance(session.state(),0)==18164 && page.required_attendance_text=="18.164",
          "Original price25 yields Source-required attendance18164, independently observed in screenshot017");
    check(page.table[6][0].find("7.392.860")!=std::string::npos && page.table[6][1].find("1.122.680")!=std::string::npos &&
          page.table[4][1].find('5')!=std::string::npos && page.table[4][2]=="---",
          "Source table uses genuine season-income totals, five cup games and missing Euro placeholders");
    accept(screen.click(268,114));
    check(screen.state().data->flags==0x7f && save_stream(session.state())==before,
          "ABS toggle changes the runtime draft, not saved flags before the Source exit");
    for (int curve=0;curve<6;++curve) {
        const auto mask=screen.state().data->flags;
        const int x=30+(curve%2)*120,y=110+(curve/2)*11;
        accept(screen.click(x,y));check(screen.state().data->flags==uint8_t(mask^(1<<curve)),
            "each curve toggle preserves the other five visible series");accept(screen.click(x,y));
    }
    const auto price=screen.click(110,228); // Source admission13 at x107..121
    auto stale=price;stale.sequence--;const auto price_before=save_stream(session.state());
    check(!session.dispatch_stadium(stale,revision,error)&&save_stream(session.state())==price_before,
          "stale data ownership rejects without any native mutation");
    accept(price);
    auto expected=session.state();expected.manager_records[0x26e]=25;
    check(save_stream(expected)==before && stadium_required_attendance(session.state(),0)==34932 &&
          screen.state().data->required_attendance_text=="34.932" && session.state().level==0 &&
          int32_t(read32(session.state().manager_records,0x314))==50000000,
          "entry13 changes only the normal record price and reproduces Original34932 while level1 keeps50million");
    const auto data_request=[&](ui::StadiumRequestKind kind,int object=-1) {
        ui::StadiumRequest request;
        const auto& owner=screen.state();request.kind=kind;request.manager_slot=owner.manager_slot;
        request.runtime_club=owner.runtime_club;request.stable_club_identity=owner.stable_club_identity;
        request.revision=owner.revision;request.scope=owner.scope;request.sequence=owner.sequence;
        request.object=object;return request;
    };
    auto cross=data_request(ui::StadiumRequestKind::quote_periphery,2);
    const auto owned=save_stream(session.state());
    check(!session.dispatch_stadium(cross,revision,error)&&save_stream(session.state())==owned,
          "data modal rejects an unrelated construction request without rolling any state");
    auto invalid=data_request(ui::StadiumRequestKind::set_data_ticket_price,9);
    check(!session.dispatch_stadium(invalid,revision,error)&&save_stream(session.state())==owned,
          "stadium data preserves the separate Original10..25 range instead of the desk8..25 range");
    accept(screen.key("ESC"));
    expected=session.state();expected.manager_records[0x26e]=25;expected.memory.set8(::bmh::layout::FixtureStadiumAdjacentRuntimeByte,0xbf);
    check(!screen.state().data && save_stream(expected)==before && session.state().memory.u8(::bmh::layout::FixtureStadiumAdjacentRuntimeByte)==0x7f,
          "Source data exit persists only its own mask byte and preserves all other Native data");
    check(session.rand().seed()==w.seed() && session.rand().calls()==w.calls() &&
          session.game().seed()==g.seed() && session.game().calls()==g.calls(),
          "all data projections/toggles/prices/return consume no W/G after stadium open");
    accept(screen.click(265,220));
    check(screen.state().data->flags==0x7f && screen.state().data->ticket_price==13,
          "a repeated data visit restores the genuinely persisted ABS/13 values");
    accept(screen.key("ESC"));
    const auto closed=session.stadium_checkpoint();
    // Source-context fixtures exercise mutually exclusive child ownership;
    // they are never exported as ordinary career evidence.
    for (int child=0;child<4;++child) {
        auto context=closed;
        if (child==0) context.stadium.investment=StadiumInvestment::Floodlights;
        if (child==1) context.stadium.periphery_quote=std::make_pair(2,1000);
        if (child==2) context.stadium.tip.emplace();
        if (child==3) context.stadium.block_expansion.emplace();
        session.restore_stadium_checkpoint(context);
        const auto native=save_stream(session.state());const auto rev=revision;
        check(!session.dispatch_stadium(data_request(ui::StadiumRequestKind::open_data),revision,error) &&
              revision==rev && save_stream(session.state())==native && !session.stadium_checkpoint().stadium.data_open,
              "investment/quote/tip/editor owners reject data entry without consuming the current context");
    }
    session.restore_stadium_checkpoint(closed);
    auto national=closed;national.state.memory.set8(::bmh::layout::GameMode,1);national.stadium.mode=1;
    session.restore_stadium_checkpoint(national);
    auto modal_request=data_request(ui::StadiumRequestKind::open_data);
    check(session.dispatch_stadium(modal_request,revision,error) &&
          stadium_state(session,revision).data && !stadium_state(session,revision).investments_enabled,
          "Source mode1 keeps stadium data available while peripheral construction is disabled");
    auto current=stadium_state(session,revision);
    modal_request.revision=current.revision;modal_request.sequence=current.sequence;
    modal_request.kind=ui::StadiumRequestKind::close_data;
    check(session.dispatch_stadium(modal_request,revision,error),"mode1 data returns to its stadium parent");
    session.restore_stadium_checkpoint(closed);revision=closed.stadium.revision;
    auto corrupt=closed;corrupt.stadium.data_open=true;corrupt.state.manager_records[0x283]=20;
    session.restore_stadium_checkpoint(corrupt);
    const auto corrupt_native=save_stream(session.state());
    bool projection_failed=false;
    try { session.dispatch_stadium(data_request(ui::StadiumRequestKind::set_data_ticket_price,17),revision,error); }
    catch(const FormatError&) { projection_failed=true; }
    check(projection_failed &&
          save_stream(session.state())==corrupt_native && session.stadium_checkpoint().stadium.data_open &&
          session.stadium_checkpoint().stadium.data_flags==closed.stadium.data_flags,
          "projection failure rolls back an immediate ticket write and retains the complete data owner");
    session.restore_stadium_checkpoint(closed);
    // Source fixture: all capacity is temporarily unavailable during block construction.
    auto zero_capacity=session.state();zero_capacity.manager_records[0x5c]=0;
    const auto exe=load_le(ui::asset_path(data,"BMH.exe"));
    const auto zero=ui::stadium_data_from_game(zero_capacity,exe,0,0x7f);
    check(!ui::stadium_data_points(zero,exe,0).empty() && zero.table==page.table,
          "legal zero-capacity source state retains all genuine historical statistics");
    for (int team : {138,356}) {
        auto outside_league=session.state();outside_league.manager_teams[0]=team;
        const auto native=save_stream(outside_league);bool rejected=false;
        try { (void)stadium_required_attendance(outside_league,0); }
        catch(const FormatError&) { rejected=true; }
        check(rejected && save_stream(outside_league)==native,
              "public attendance API rejects a manager outside the nine Source leagues before indexing their home counts");
    }
}
}

namespace {
void test_process_deadline_periphery_question(const std::filesystem::path& data) {
    Bytes manual_native;
    uint32_t manual_w_seed=0,manual_g_seed=0;uint64_t manual_w_calls=0,manual_g_calls=0;
    for(int answer_kind=0;answer_kind<3;++answer_kind) {
        ui::StartupFlow flow(data);std::unique_ptr<app::CareerSession> session;
        uint64_t revision=0;openbmh::audio::Playback playback;std::optional<uint64_t> audio;
        start_career(data,flow,session,revision,playback,audio,0);
        const auto serve=[&](const ui::FlowResult& request) {
            check(app::serve_flow_request(flow,request,session,data,data,0x51ad10e3,
                revision,playback,audio,false).empty(),"periphery deadline keeps its production dispatcher");
        };
        const auto& items=flow.desktop()->state().items;
        const auto item=std::find_if(items.begin(),items.end(),[](const ui::DeskItem& i){return i.kind==22&&!i.disabled;});
        check(item!=items.end(),"normal career exposes a genuine stadium desktop entry");
        serve(flow.click(item->x+item->width/2,item->y+item->height/2));
        const auto click=[&](int x,int y){flow.pointer_down(x,y);serve(flow.click(x,y));};
        click(30,220);click(160,220);click(239,172);
        const auto& page=flow.stadium()->state();
        check(page.quoted_object && page.quoted_price>0 && page.periphery_quote_dialog &&
              page.periphery_quote_dialog->source_flags==1 && !page.construction_tip_dialog,
              "normal parking selection opens the Source flags1 purchase question");
        const int object=*page.quoted_object;const int32_t price=page.quoted_price;
        const auto before=save_stream(session->state());const auto w=session->rand();const auto g=session->game();
        ui::FlowResult answer;
        if(answer_kind==0) {
            flow.pointer_down(205,170);answer=flow.click(205,170);
        } else {
            flow.pointer_down(100,170);
            const auto held=flow.state_json();
            check(flow.stadium()->construction_tip_held() && !flow.process_timer_expired().changed && flow.state_json()==held,
                  "held periphery Yes keeps its physical release owner during raw event8");
            if(answer_kind==1) {
                (void)flow.click(0,0);answer=flow.process_timer_expired();
            } else {
                // Main calls this existing question deadline before accepting
                // a late physical UP, overriding ExpireQuestion's Yes with Source No.
                answer=flow.expire_stadium_tip();
            }
        }
        check(answer.request==ui::FlowRequest::submenu_action && std::holds_alternative<ui::StadiumRequest>(answer.submenu_action),
              "periphery deadline produces an ordinary typed answer");
        const auto request=std::get<ui::StadiumRequest>(answer.submenu_action);
        check(request.kind==ui::StadiumRequestKind::decline_periphery && request.object==object && request.price==price &&
              request.option==-1 && !request.accept_tip,"periphery expiry declines the exact owned quote without confirming purchase");
        const auto pending=flow.state_json();
        check(!flow.process_timer_expired().changed && flow.state_json()==pending && save_stream(session->state())==before &&
              session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
              session->game().seed()==g.seed() && session->game().calls()==g.calls(),
              "pending periphery No preserves all Native bytes and complete RNG checkpoints");
        serve(answer);
        check(!flow.stadium()->state().quoted_object && !flow.stadium()->state().periphery_quote_dialog &&
              save_stream(session->state())==before && session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
              session->game().seed()==g.seed() && session->game().calls()==g.calls(),
              "periphery No clears only its modal and pays or builds nothing");
        const auto after=save_stream(session->state());const auto after_revision=revision;std::string error;
        check(!session->dispatch_stadium(request,revision,error) && revision==after_revision && save_stream(session->state())==after,
              "declined periphery quote cannot settle twice");
        if(answer_kind==2) {
            const auto late_up=flow.click(100,170);
            // StartupFlow intentionally repaints the stadium on any UP; the
            // ownership guarantee is no answer action or second settlement.
            check(late_up.request==ui::FlowRequest::none && !flow.stadium()->pending() &&
                  save_stream(session->state())==after && revision==after_revision &&
                  session->rand().seed()==w.seed() && session->rand().calls()==w.calls() &&
                  session->game().seed()==g.seed() && session->game().calls()==g.calls(),
                  "late physical UP cannot accept a second periphery answer");
        }
        if(answer_kind==0) {
            manual_native=after;manual_w_seed=session->rand().seed();manual_g_seed=session->game().seed();
            manual_w_calls=session->rand().calls();manual_g_calls=session->game().calls();
        } else check(after==manual_native && session->rand().seed()==manual_w_seed && session->rand().calls()==manual_w_calls &&
                     session->game().seed()==manual_g_seed && session->game().calls()==manual_g_calls,
                     "both deadline paths match manual periphery No in every Native byte and W/G checkpoint");
        std::printf("PASS: periphery question answer=%d\n",answer_kind);
    }
}

void test_process_deadline_construction_question(const std::filesystem::path& data) {
    for (bool block : {true,false}) {
        Bytes manual_native;
        uint32_t manual_w_seed=0,manual_g_seed=0;uint64_t manual_w_calls=0,manual_g_calls=0;
        for (bool timed : {false,true}) {
            ui::StartupFlow flow(data);std::unique_ptr<app::CareerSession> session;
            uint64_t revision=0;openbmh::audio::Playback playback;std::optional<uint64_t> audio_session;
            start_career(data,flow,session,revision,playback,audio_session,0);
            const auto serve=[&](const ui::FlowResult& request) {
                check(app::serve_flow_request(flow,request,session,data,data,0x51ad10e3,
                    revision,playback,audio_session,false).empty(), "construction question keeps its production dispatcher");
            };
            const auto& items=flow.desktop()->state().items;
            const auto item=std::find_if(items.begin(),items.end(),[](const ui::DeskItem& i){return i.kind==22&&!i.disabled;});
            check(item!=items.end(), "normal career exposes the stadium entry");
            serve(flow.click(item->x+item->width/2,item->y+item->height/2));
            if(block) {
                int slot=-1;
                for(int i=0;i<8;++i) {
                    const auto& row=flow.stadium()->state().blocks[size_t(i)];
                    if(row.capacity_level && !row.construction_weeks && !(row.flags&2)){slot=i;break;}
                }
                check(slot>=0,"normal career supplies a positive-capacity block without roof or fixture setters");
                flow.pointer_down(265,34+slot*24);serve(flow.click(265,34+slot*24));
                flow.pointer_down(165,95);serve(flow.click(165,95));
                check(flow.stadium()->state().block_expansion->quote.base_price>0,"ordinary roof choice has a genuine positive quote");
            } else {
                int choice=-1;
                for(int i=0;i<8;++i)if(flow.stadium()->state().investments[size_t(i)].available &&
                    !flow.stadium()->state().investments[size_t(i)].options.empty()){choice=i;break;}
                check(choice>=0,"normal career supplies an available investment without fixture setters");
                const int x=3+(choice%4)*54+27,y=153+(choice/4)*30+15;
                flow.pointer_down(x,y);serve(flow.click(x,y));
                flow.pointer_down(20,86);serve(flow.click(20,86));
                check(flow.stadium()->state().selected_option==0,"ordinary tier choice selects the first real Source option");
            }
            flow.pointer_down(50,140);serve(flow.click(50,140));
            check(flow.stadium()->state().construction_tip_dialog.has_value(),"confirmed construction enters the real optional-cost question");
            const auto before=save_stream(session->state());const auto before_w=session->rand();const auto before_g=session->game();
            ui::FlowResult answer;
            if(timed) {
                flow.pointer_down(75,170);
                const auto held=flow.state_json();
                check(flow.stadium()->construction_tip_held() && !flow.process_timer_expired().changed && flow.state_json()==held,
                      "deadline retains a physically held construction-answer control");
                // Cancel the physical press outside its hitbox, then deliver
                // raw event8. Main separately verifies the visible-frame and
                // late-UP override with actual SDL captures.
                (void)flow.click(0,0);
                answer=flow.process_timer_expired();
            } else {
                flow.pointer_down(205,170);answer=flow.click(205,170);
            }
            check(answer.request==ui::FlowRequest::submenu_action && std::holds_alternative<ui::StadiumRequest>(answer.submenu_action),
                  "question No uses an ordinary typed stadium action");
            const auto request=std::get<ui::StadiumRequest>(answer.submenu_action);
            check(request.kind==(block?ui::StadiumRequestKind::expansion_answer_tip:ui::StadiumRequestKind::answer_construction_tip) &&
                  !request.accept_tip,"expiry and manual No both decline extra costs rather than cancel the confirmed construction");
            const auto pending=flow.state_json();
            check(!flow.process_timer_expired().changed && flow.state_json()==pending && save_stream(session->state())==before &&
                  session->rand().seed()==before_w.seed() && session->rand().calls()==before_w.calls() &&
                  session->game().seed()==before_g.seed() && session->game().calls()==before_g.calls(),
                  "pending question deadline keeps its exact ACK without saved writes or RNG draws");
            serve(answer);
            check(!flow.stadium()->state().construction_tip_dialog && save_stream(session->state())!=before,
                  "No completes real construction through the production Source settlement");
            const auto after=save_stream(session->state());const auto after_revision=revision;
            std::string error;
            check(!session->dispatch_stadium(request,revision,error) && revision==after_revision && save_stream(session->state())==after,
                  "replayed construction answer is inert after its accepted ACK");
            if(!timed) {
                manual_native=after;manual_w_seed=session->rand().seed();manual_g_seed=session->game().seed();
                manual_w_calls=session->rand().calls();manual_g_calls=session->game().calls();
            } else check(after==manual_native && session->rand().seed()==manual_w_seed && session->rand().calls()==manual_w_calls &&
                         session->game().seed()==manual_g_seed && session->game().calls()==manual_g_calls,
                         "deadline construction equals physical No in every Native byte and both complete RNG checkpoints");
            std::printf("PASS: construction question block=%d deadline=%d\n",int(block),int(timed));
        }
    }
}

void test_process_deadline_handback(const std::filesystem::path& data) {
    for (bool calculator : {false,true}) {
        ui::StartupFlow flow(data);std::unique_ptr<app::CareerSession> session;
        uint64_t revision=0;openbmh::audio::Playback playback;std::optional<uint64_t> audio_session;
        start_career(data,flow,session,revision,playback,audio_session);
        const auto serve=[&](const ui::FlowResult& request) {
            check(app::serve_flow_request(flow,request,session,data,data,0x51ad10e3,
                revision,playback,audio_session,false).empty(),"deadline handback retains its production app dispatcher");
        };
        const auto& items=flow.desktop()->state().items;
        const auto item=std::find_if(items.begin(),items.end(),[](const ui::DeskItem& i){return i.kind==22&&!i.disabled;});
        check(item!=items.end(),"deadline handback opens the genuine club stadium");
        serve(flow.click(item->x+item->width/2,item->y+item->height/2));
        constexpr std::array<int,8> maxima{8,18,8,14,8,18,8,14};int slot=-1;
        for(int i=0;i<8;++i)if(!flow.stadium()->state().blocks[size_t(i)].construction_weeks&&
            flow.stadium()->state().blocks[size_t(i)].capacity<maxima[size_t(i)]){slot=i;break;}
        check(slot>=0,"normal new career supplies an editable Source block without Native fixture writes");
        flow.pointer_down(265,34+slot*24);serve(flow.click(265,34+slot*24));
        check(flow.stadium()->state().block_expansion.has_value(),"production block selection owns its quote before the deadline");
        const auto native=save_stream(session->state());const auto w=session->rand();const auto g=session->game();
        if(calculator){
            flow.pointer_down(50,80);flow.click(50,80);check(flow.text("15333"),"flow owns genuine unconfirmed calculator text");
            const auto cancelled=flow.process_timer_expired();
            check(cancelled.changed&&cancelled.request==ui::FlowRequest::none&&
                !flow.stadium()->calculator_open()&&!flow.stadium()->calculator_source_waiting(),
                "local timer cancellation requests the restored frame without a Core command or N wait");
        }
        const auto cancel=flow.process_timer_expired();
        check(cancel.request==ui::FlowRequest::submenu_action&&
            std::get<ui::StadiumRequest>(cancel.submenu_action).kind==ui::StadiumRequestKind::expansion_cancel,
            "outer block expiry preserves the actual app-owned cancellation request");
        check(flow.process_timer_expired().request==ui::FlowRequest::none,"pending block cancel keeps its ACK");serve(cancel);
        check(!flow.stadium()->state().block_expansion,"app ACK returns to the Source overview");
        const auto close=flow.process_timer_expired();
        check(std::get<ui::StadiumRequest>(close.submenu_action).kind==ui::StadiumRequestKind::close,
            "only the completed block handback may close the stadium parent");serve(close);
        check(flow.screen_name()=="desktop"&&save_stream(session->state())==native&&
            session->rand().seed()==w.seed()&&session->rand().calls()==w.calls()&&
            session->game().seed()==g.seed()&&session->game().calls()==g.calls(),
            "both real app handbacks leave every Native byte and both RNGs unchanged before kickoff");
        check(flow.process_timer_expired().request==ui::FlowRequest::kickoff&&
            flow.process_timer_expired().request==ui::FlowRequest::none,"completed stadium handback emits one normal kickoff");
    }
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=2) throw std::runtime_error("stadium_block_app_tests requires game data directory");
        test_process_deadline_handback(argv[1]);
        test_process_deadline_construction_question(argv[1]);
        test_process_deadline_periphery_question(argv[1]);
        test_original_stadium_data(argv[1]);
        test_periphery_funds_answer_consumed(argv[1]);
        test_panel_setting_projection_uses_real_writer(argv[1]);
        test_preview_cancel_zero_and_cross_family(argv[1]);
        test_literal_eight_block_tables(argv[1]);
        test_no_tip_commit_exact_source_fields(argv[1]);
        test_yes_level_zero_and_funds_refusal(argv[1]);
        test_controller_compensation_ack_rollback(argv[1]);
        std::puts("stadium_block_app_tests: Source A-H App transactions passed");
        return 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr,"stadium_block_app_tests: %s\n",e.what());
        return 1;
    }
}
