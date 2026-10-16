// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <memory>
#include <optional>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <variant>
#include <vector>

#include "app/career_session.hpp"
#include "app/controller.hpp"
#include "app/window_actions.hpp"
#include "formats/fbm.hpp"
#include "game/messages.hpp"
#include "game/month.hpp"
#include "game/save.hpp"
#include "game/weekly.hpp"
#include "ui/pending_offer.hpp"

namespace fs = std::filesystem;
using namespace bmh;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
uint32_t read32(const Bytes& b, size_t at) {
    uint32_t result = 0;
    for (size_t i = 0; i < 4; ++i) result |= uint32_t(b.at(at + i)) << (8 * i);
    return result;
}
bool contains(const GameState& state, int team, int player) {
    const auto range = squad_range(team);
    for (size_t i = range.first; i < range.first + range.count; ++i)
        if ((state.squad_slots.at(i) & 0xfff) == player) return true;
    return false;
}
struct Snapshot {
    Bytes native;
    GameRandom game;
    WatcomRandom rand;
    uint64_t revision;
    Snapshot(app::CareerSession& session, uint64_t rev)
        : native(save_stream(session.state())), game(session.game()), rand(session.rand()), revision(rev) {}
    bool equals(app::CareerSession& session, uint64_t rev) const {
        return native == save_stream(session.state()) && rev == revision &&
            game.seed() == session.game().seed() && game.calls() == session.game().calls() &&
            rand.seed() == session.rand().seed() && rand.calls() == session.rand().calls();
    }
};
int json_integer(std::string_view json, std::string_view key, int fallback = -1) {
    const std::string token = "\"" + std::string(key) + "\":";
    const auto at = json.find(token);
    if (at == std::string_view::npos) return fallback;
    const auto first = json.data() + at + token.size();
    int value = fallback;
    const auto [end, ec] = std::from_chars(first, json.data() + json.size(), value);
    return ec == std::errc{} && end != first ? value : fallback;
}
std::vector<int> visible_market_entries(std::string_view json) {
    constexpr std::string_view key = "\"visible_market_entries\":[";
    const auto at = json.find(key);
    check(at != std::string_view::npos, "actual market state exposes the six visible Source rows");
    const auto first = at + key.size();
    const auto end = json.find(']', first);
    check(end != std::string_view::npos, "visible market row array closes");
    std::vector<int> result;
    auto cursor = first;
    while (cursor < end) {
        while (cursor < end && (json[cursor] == ' ' || json[cursor] == ',')) ++cursor;
        if (cursor == end) break;
        int value = -1;
        const auto [next, ec] = std::from_chars(json.data() + cursor, json.data() + end, value);
        check(ec == std::errc{} && next != json.data() + cursor, "visible Source row is a list-entry integer");
        result.push_back(value);
        cursor = size_t(next - json.data());
    }
    return result;
}
struct OrdinaryInput {
    fs::path directory, file;
    ui::TransferMarketRequest request;
    int seller = -1;
    explicit OrdinaryInput(const fs::path& genuine,bool free_agent=false) {
        const auto native = load_savegame(genuine);
        check(native.checksum_ok, "genuine input checksum");
        auto state = GameState::from_save(SaveState(native.stream));
        check(state.manager_teams == std::vector<int>({1, 54}) && state.memory.u8(::bmh::layout::ActiveManager) == 0,
              "genuine two-manager desk identity");
        check(!state.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2) && !state.memory.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2),
              "direct runtime globals are absent from Native input");
        bool found = false;
        if(free_agent) {
            // Actual unchanged genuine Native free-agent listing. No Source
            // listing generation, seller relabelling, scalar override or BSS.
            for(int entry=0;entry<40&&!found;++entry) {
                const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(entry)*0x58;
                const int player=state.squad_slots.at(0xb70u+size_t(entry))&0xfff;
                if(player==0xfff||state.memory.u16(at+8)!=0x0fff||state.memory.u16(at)!=0xffff) continue;
                const uint32_t fee_base=(state.memory.u32(at+0x2c)/10000u)*12500u;
                const uint32_t salary_base=((100u*state.memory.u32(at+0x30))/10000u)*100u;
                if(!fee_base||!salary_base||fee_base>read32(state.manager_records,0x314)/3u) continue;
                for(int team=0;team<138;++team)
                    check(!contains(state,team,player),"genuine free agent is not an owned player relabelled as free");
                request.kind=ui::TransferMarketRequestKind::submit_bid;
                request.manager_slot=0;request.runtime_club=1;request.revision=1;
                request.bid.manager=0;request.bid.list_entry=entry;request.bid.player=player;
                request.bid.source_slot=0xb70+entry;
                request.bid.fee=int32_t(fee_base*2u);request.bid.annual_salary=int32_t(salary_base*2u);
                request.bid.contract_years=1;seller=0x0fff;found=true;
            }
            check(found&&state.manager_records.at(0x170)==0,"unchanged genuine free-agent offer is affordable and has no subsidy");
        }
        // Generate an ordinary AI-owned offer through Source's listing helper.
        // There is no special-row promotion, OfferSlots rewrite or BSS injection.
        for (int team = 0; team < 90 && !found; ++team) {
            if (state.manager_of(team) >= 0) continue;
            const auto range = squad_range(team);
            for (size_t row = 11; row < range.count && !found; ++row) {
                const int player = state.squad_slots.at(range.first + row) & 0xfff;
                if (player == 0xfff) continue;
                auto candidate = state;
                WatcomRandom listing_rand(37);
                MinuteReport report;
                if (!list_transfer_player(candidate, int(range.first + row), listing_rand, report) || report.unsupported)
                    continue;
                for (int entry = 0; entry < 40; ++entry) {
                    const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(entry) * 0x58;
                    if ((candidate.squad_slots.at(0xb70u + size_t(entry)) & 0xfff) != player ||
                        (candidate.memory.u16(at + 8) & 0x7fff) != team || candidate.memory.u16(at) != 0xffff)
                        continue;
                    const uint32_t fee_base = (candidate.memory.u32(at + 0x2c) / 10000u) * 0x30d4u;
                    const uint32_t salary_base = ((100u * candidate.memory.u32(at + 0x30)) / 10000u) * 100u;
                    const uint32_t cash = read32(candidate.manager_records, 0x314);
                    if (!fee_base || !salary_base || fee_base > cash / 3 || salary_base > 0x3fffffffu) continue;
                    request.kind = ui::TransferMarketRequestKind::submit_bid;
                    request.manager_slot = 0;
                    request.runtime_club = 1;
                    request.revision = 1;
                    request.bid.manager = 0;
                    request.bid.list_entry = entry;
                    request.bid.source_slot = 0xb70 + entry;
                    request.bid.player = player;
                    request.bid.fee = int32_t(fee_base * 2u);
                    request.bid.annual_salary = int32_t(salary_base * 2u);
                    request.bid.contract_years = 1;
                    request.bid.bonuses = {1, 2, 3, 4, 5, 6, 7};
                    candidate.manager_records.at(0x170) = 0; // Declared no-subsidy control.
                    state = std::move(candidate);
                    seller = team;
                    found = true;
                    break;
                }
            }
        }
        check(found, "genuine input has an affordable ordinary AI listing");
        directory = fs::temp_directory_path() / ("openbmh-market-app-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(directory);
        file = directory / "ORDINARY_INPUT.FBM";
        WatcomRandom serialize(1);
        if(free_agent) fs::copy_file(genuine,file);
        else write_savegame(file, make_savegame(state, serialize));
        check(load_savegame(file).stream == save_stream(state), "ordinary Native control roundtrip");
        if(free_agent) check(read_file(file)==read_file(genuine),"actual free-agent App fixture preserves complete genuine FBM bytes");
    }
    ~OrdinaryInput() { std::error_code ignored; fs::remove_all(directory, ignored); }
};
struct ControllerInput {
    fs::path directory;
    explicit ControllerInput(const OrdinaryInput& fixture,const fs::path& native={}) {
        static unsigned serial=0;
        directory=fixture.directory/("controller-"+std::to_string(++serial));
        check(fs::create_directory(directory),"isolated real-controller save directory");
        fs::copy_file(native.empty()?fixture.file:native,directory/"AUTOSAVE.FBM");
    }
    ~ControllerInput() { std::error_code ignored; fs::remove_all(directory,ignored); }
};
struct ControllerContext {
    fs::path data;
    ControllerInput input;
    ui::StartupFlow flow;
    std::unique_ptr<app::CareerSession> session;
    uint64_t revision=0;
    std::optional<Snapshot> before_editor_submit;
    openbmh::audio::Playback playback;
    std::optional<uint64_t> audio_session;
    ControllerContext(const fs::path& d,const OrdinaryInput& fixture,const fs::path& native={})
        :data(d),input(fixture,native),flow(d,input.directory) {
        playback.set_muted(true);
        flow.click(214,215);
        const auto load=flow.send_key("ENTER");
        check(load.request==ui::FlowRequest::file_action&&load.file_action.action==ui::SaveLoadAction::load,
              "physical setup/load controls produce the genuine pending Native-load request");
        check(serve(load).empty()&&session&&revision==1&&flow.screen_name()=="desktop",
              "actual production controller loads the ordinary Native control into its desk");
        check(save_stream(session->state())==load_savegame(native.empty()?fixture.file:native).stream&&
              session->game().calls()==0&&session->rand().calls()==0,
              "actual StartupFlow Native load preserves bytes and consumes no gameplay RNG");
    }
    std::string serve(const ui::FlowResult& result,HumanBuyerSaleHooks hooks={}) {
        return app::serve_flow_request(flow,result,session,data,input.directory,1,revision,
            playback,audio_session,false,{},{},{},{},{},{},{},{},PendingOfferHooks{},
            app::PendingOfferEntryPolicy::legacy_unpresented,std::move(hooks),
            app::HumanBuyerMarketEntryPolicy::normal_interactive);
    }
    ui::FlowResult bid(const OrdinaryInput& fixture,const TransferBidRequest* edited=nullptr,bool existing=false) {
        const auto& wanted=edited?*edited:fixture.request.bid;
        check(session!=nullptr,"actual loaded CareerSession is available");
        before_editor_submit.reset();
        if(flow.screen_name()=="outgoing_transfer") {
            const auto submitted=flow.click(180,219);
            check(submitted.request==ui::FlowRequest::submenu_action&&
                  std::holds_alternative<ui::OutgoingTransferRequest>(submitted.submenu_action)&&
                  flow.pending_transfer_offer(),"physical KB31 Angebot control retries the open Source editor");
            before_editor_submit.emplace(*session,revision);
            return submitted;
        }
        if(flow.screen_name()=="desktop") {
            check(flow.desktop()!=nullptr&&!flow.desktop()->pending(),"actual native desk owns an available menu");
            const auto& items=flow.desktop()->state().items;
            const auto market=std::find_if(items.begin(),items.end(),[](const auto& item){return item.kind==17&&!item.disabled;});
            check(market!=items.end(),"actual desk exposes transfer menu17");
            const auto opened=flow.click(market->x+market->width/2,market->y+market->height/2);
            check(serve(opened).empty()&&flow.screen_name()=="transfer_market","actual menu17 opens production market screen");
        }
        check(flow.screen_name()=="transfer_market","real market retained for an ordinary retry");
        const auto projection=ui::transfer_market_from_game(session->state(),revision);
        const auto item=std::find_if(projection.items.begin(),projection.items.end(),[&](const auto& value){
            return value.list_entry==fixture.request.bid.list_entry&&value.player==fixture.request.bid.player;
        });
        check(item!=projection.items.end()&&item->seller==fixture.seller&&!item->own_listing&&item->own_bid_pending==existing,
              "actual Native market projection includes the ordinary seller/player");
        int visible_row=-1;
        for(int page=0;page<7&&visible_row<0;++page) {
            const auto entries=visible_market_entries(flow.state_json());
            const auto found=std::find(entries.begin(),entries.end(),fixture.request.bid.list_entry);
            if(found!=entries.end()) visible_row=int(found-entries.begin());
            else if(page<6) flow.click(250,40,true); // Source right-side page advance
        }
        check(visible_row>=0,"ordinary listing is reached through the Source six-row page window");
        const int row_y=25+visible_row*15+7;
        const auto preview=flow.move_action(220,row_y);
        check(preview.request==ui::FlowRequest::submenu_action&&
              std::holds_alternative<ui::TransferMarketRequest>(preview.submenu_action)&&
              std::get<ui::TransferMarketRequest>(preview.submenu_action).kind==ui::TransferMarketRequestKind::preview_quote,
              "real market hover produces the Source quote preview request");
        check(serve(preview).empty()&&flow.screen_name()=="transfer_market"&&
              !flow.pending_transfer_market(),"controller acknowledges the actual hover preview before the next input");
        const auto marked=flow.click(220,row_y);
        check(marked.request==ui::FlowRequest::none,"Source row click marks the already previewed player");
        const auto opening=flow.click(185,207);
        check(opening.request==ui::FlowRequest::submenu_action&&
              std::holds_alternative<ui::TransferMarketRequest>(opening.submenu_action)&&
              std::get<ui::TransferMarketRequest>(opening.submenu_action).kind==ui::TransferMarketRequestKind::open_offer,
              "Source Aktion button opens the marked ordinary purchase in KB31");
        const uint32_t opening_row=::bmh::layout::EditorListUnsigned+uint32_t(fixture.request.bid.list_entry)*0x58;
        const auto before_open=Snapshot(*session,revision);
        int opening_bid_slot=-1;
        for(int n=0;n<4;++n) {
            const auto buyer=session->state().memory.u16(opening_row+uint32_t(n)*2);
            if(buyer==0xffff||buyer==1){opening_bid_slot=n;break;}
        }
        check(opening_bid_slot>=0,"Source opening has the first empty or current-buyer raw offer slot");
        const auto expected_bidder=session->state().memory.u16(opening_row+uint32_t(opening_bid_slot)*2);
        std::array<uint8_t,7> opening_bonuses{};
        for(size_t n=0;n<opening_bonuses.size();++n)
            opening_bonuses[n]=session->state().memory.u8(opening_row+0x38+uint32_t(opening_bid_slot)*7+uint32_t(n));
        check(serve(opening).empty()&&flow.screen_name()=="outgoing_transfer"&&
              flow.pending_transfer_offer()==nullptr,"CareerSession opens the production KB31 Source editor");
        check(revision==before_open.revision+1&&session->game().calls()==before_open.game.calls()+1&&
              session->rand().calls()>=before_open.rand.calls()&&
              session->rand().calls()<=before_open.rand.calls()+1,
              "Source legacy routine opening owns one G draw and its quote's zero-or-one conditional W draw");
        if(expected_bidder==0xffff)
            check(session->state().memory.u32(opening_row+0x0c+uint32_t(opening_bid_slot)*4)==0&&
                  session->state().memory.u32(opening_row+0x1c+uint32_t(opening_bid_slot)*4)==0&&
                  session->state().memory.u8(opening_row+0x34+uint32_t(opening_bid_slot))==24,
                  "Source opening initializes only a new row's salary, fee, and 24-month term");
        else check(existing&&expected_bidder==1,
                   "an existing own bid is opened without replacing its Native owner");
        for(size_t n=0;n<opening_bonuses.size();++n)
            check(session->state().memory.u8(opening_row+0x38+uint32_t(opening_bid_slot)*7+uint32_t(n))==opening_bonuses[n],
                  "Source opening preserves each Native premium byte");

        auto editor_value=[&](std::string_view key){return json_integer(flow.state_json(),key);};
        auto commit_field=[&](int x,int y,uint32_t value,const char* what) {
            const auto opened=flow.click(x,y);
            check(opened.request==ui::FlowRequest::none&&flow.state_json().find("\"calculator_open\":true")!=std::string::npos,
                  "KB31 field click opens its Source KB6 calculator");
            check(flow.text(std::to_string(value)),what);
            const auto action=flow.send_key("ENTER");
            check(action.request==ui::FlowRequest::submenu_action&&
                  std::holds_alternative<ui::OutgoingTransferRequest>(action.submenu_action)&&
                  std::get<ui::OutgoingTransferRequest>(action.submenu_action).kind==ui::OutgoingTransferRequestKind::quote_change,
                  "KB6 Enter emits the typed Source editor commit request");
            const Snapshot before_edit(*session,revision);
            check(serve(action).empty()&&flow.screen_name()=="outgoing_transfer"&&
                  !flow.pending_transfer_offer(),"CareerSession acknowledges each committed KB31 edit immediately");
            check(revision==before_edit.revision+1&&
                  session->game().seed()==before_edit.game.seed()&&
                  session->game().calls()==before_edit.game.calls()&&
                  session->rand().calls()>=before_edit.rand.calls()&&
                  session->rand().calls()<=before_edit.rand.calls()+1,
                  "Source field commit writes Native and uses the quote's zero-or-one conditional W draw without another G draw");
        };
        if(uint32_t(editor_value("fee"))!=uint32_t(wanted.fee))
            commit_field(40,123,uint32_t(wanted.fee),"actual KB31 fee calculator accepts the offered amount");
        if(uint32_t(editor_value("monthly_salary"))!=uint32_t(wanted.annual_salary))
            commit_field(200,123,uint32_t(wanted.annual_salary),"actual KB31 salary calculator accepts the offered amount");

        const int source_mode=editor_value("source_mode");
        uint8_t duration=uint8_t(std::max(1,editor_value("duration_years")));
        std::array<uint8_t,7> bonuses{};
        const auto state_json=flow.state_json();
        const auto bonuses_at=state_json.find("\"bonuses\":[");
        check(bonuses_at!=std::string::npos,"KB31 projection exposes all seven source bonus bytes");
        auto bonus_cursor=bonuses_at+11;
        const auto bonuses_end=state_json.find(']',bonus_cursor);
        check(bonuses_end!=std::string::npos,"KB31 bonus projection closes");
        for(size_t n=0;n<bonuses.size();++n) {
            while(bonus_cursor<bonuses_end&&(state_json[bonus_cursor]==' '||state_json[bonus_cursor]==','))++bonus_cursor;
            int value=-1;const auto [next,ec]=std::from_chars(state_json.data()+bonus_cursor,state_json.data()+bonuses_end,value);
            check(ec==std::errc{}&&next!=state_json.data()+bonus_cursor&&value>=0&&value<=255,
                  "KB31 native bonus value is a byte");
            bonuses[n]=uint8_t(value);bonus_cursor=size_t(next-state_json.data());
        }
        if(source_mode==3) {
            if(duration!=wanted.contract_years) {
                const uint32_t input=uint32_t(wanted.contract_years);
                commit_field(210,137,input,"Mode-3 term KB6 accepts the Source years value");
                duration=uint8_t(wanted.contract_years);
            }
            for(size_t n=0;n<bonuses.size();++n) if(bonuses[n]!=wanted.bonuses[n]) {
                const uint32_t input=uint32_t(wanted.bonuses[n])*(n>=3?5u:1u);
                commit_field(210,146+int(n)*9,input,"Mode-3 bonus KB6 accepts its Source-scaled value");
                bonuses[n]=uint8_t(wanted.bonuses[n]);
            }
        }
        const auto final_editor=flow.state_json();
        const uint32_t final_fee=uint32_t(json_integer(final_editor,"fee"));
        const uint32_t final_salary=uint32_t(json_integer(final_editor,"monthly_salary"));
        const int bid_slot=json_integer(final_editor,"bid_slot");
        const auto submit_snapshot=Snapshot(*session,revision);
        const auto submitted=flow.click(180,219);
        check(submitted.request==ui::FlowRequest::submenu_action&&
              std::holds_alternative<ui::OutgoingTransferRequest>(submitted.submenu_action)&&flow.pending_transfer_offer(),
              "physical KB31 Angebot footer creates StartupFlow-owned Source submit request");
        const auto& actual=std::get<ui::OutgoingTransferRequest>(submitted.submenu_action);
        check(actual.kind==ui::OutgoingTransferRequestKind::submit&&actual.identity.manager_slot==0&&
              actual.identity.runtime_club==1&&actual.identity.revision==revision&&actual.identity.bid_slot==bid_slot&&
              actual.identity.bid_slot==opening_bid_slot&&
              actual.identity.player==fixture.request.bid.player&&actual.identity.list_entry==fixture.request.bid.list_entry&&
              actual.identity.source_slot==fixture.request.bid.source_slot&&actual.fee==final_fee&&
              actual.monthly_salary==final_salary&&actual.duration_years==duration&&actual.bonuses==bonuses,
              "KB31 typed submit retains Source list, owner, raw bid slot, fields and premiums");
        const uint32_t row_base=::bmh::layout::EditorListUnsigned+uint32_t(fixture.request.bid.list_entry)*0x58;
        check(session->state().memory.u16(row_base+uint32_t(bid_slot)*2)==expected_bidder&&
              session->state().memory.u32(row_base+0x1c+uint32_t(bid_slot)*4)==final_fee&&
              session->state().memory.u32(row_base+0x0c+uint32_t(bid_slot)*4)==final_salary&&
              session->state().memory.u8(row_base+0x34+uint32_t(bid_slot))==duration*12,
              "Source editor commits the displayed fields while retaining its original empty/own bidder word");
        for(size_t n=0;n<bonuses.size();++n)
            check(session->state().memory.u8(row_base+0x38+uint32_t(bid_slot)*7+uint32_t(n))==bonuses[n],
                  "Source KB31 owner commits the displayed native byte for every edited premium");
        before_editor_submit.emplace(submit_snapshot);
        return submitted;
    }
};
void controller_unchanged_capacity_notice(const fs::path& data,const OrdinaryInput& fixture) {
    auto state=GameState::from_save(SaveState(load_savegame(fixture.file).stream));
    const int count=squad_count(state,1,3);
    check(count>=0&&count<=23&&state.memory.has(::bmh::layout::DeskActions_Unsupported_RuntimeField),"Native capacity counter is represented");
    // Declared Source equality-23 control. Editor fields already match the
    // physical offer, so this refusal has no Native or gameplay RNG effect.
    state.memory.set8(::bmh::layout::DeskActions_Unsupported_RuntimeField,uint8_t(23-count));
    const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(fixture.request.bid.list_entry)*0x58;
    check(state.memory.u16(at)==0xffff,"capacity control has a new, unowned bid slot");
    state.memory.set32(at+0x1c,uint32_t(fixture.request.bid.fee));
    state.memory.set32(at+0x0c,uint32_t(fixture.request.bid.annual_salary));
    state.memory.set8(at+0x34,12);
    for(uint32_t i=0;i<7;++i) state.memory.set8(at+0x38+i,0);
    const auto native=fixture.directory/"CAPACITY_INPUT.FBM";
    WatcomRandom serialize(1);
    write_savegame(native,make_savegame(state,serialize));
    ControllerContext c(data,fixture,native);
    std::optional<Snapshot> before_submit;
    unsigned calls=0;
    HumanBuyerSaleHooks hooks;
    hooks.notice=[&](const GameState& staged,const HumanBuyerSaleNotice& notice){
        check(before_submit&&before_submit->equals(*c.session,c.revision)&&
              save_stream(staged)==before_submit->native,
              "Source capacity presenter observes unchanged staged and live Native/W/G/revision");
        check(notice.manager==0&&notice.list_entry==fixture.request.bid.list_entry&&
              notice.source_text_address==::bmh::layout::DeskActions_Unsupported_ExecutableField,"actual controller presents Source equality-23 refusal");
        const auto dialog=ui::pending_offer_notice_dialog(staged,
            {notice.manager,notice.raw_offer_slot,notice.source_text_address,notice.original_message});
        check(dialog.source_manager_flags==0x40&&dialog.body==message_text(staged,::bmh::layout::DeskActions_Unsupported_ExecutableField),
              "capacity refusal uses Original legacy routine manager40 presentation");
        ++calls;
    };
    for(unsigned attempt=1;attempt<=2;++attempt) {
        const auto request=c.bid(fixture);
        check(c.before_editor_submit.has_value(),"KB31 retry retains a confirmed pre-submit checkpoint");
        before_submit=c.before_editor_submit;
        check(c.serve(request,hooks).empty()&&calls==attempt,
              "physical KB31 submit and retry each present exactly one Source refusal");
        check(before_submit->equals(*c.session,c.revision)&&!c.flow.pending_transfer_offer()&&
              c.flow.screen_name()=="outgoing_transfer"&&c.flow.desktop()->state().revision==c.revision&&
              c.flow.state_json().find("\"feedback\":\"\"")!=std::string::npos,
              "unchanged Source capacity refusal keeps KB31 open for another physical offer attempt");
    }
}
void controller_existing_bid_edit(const fs::path& data,const OrdinaryInput& fixture) {
    auto state=GameState::from_save(SaveState(load_savegame(fixture.file).stream));
    const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(fixture.request.bid.list_entry)*0x58;
    state.memory.set16(at,1);
    state.memory.set32(at+0x1c,uint32_t(fixture.request.bid.fee));
    state.memory.set32(at+0x0c,uint32_t(fixture.request.bid.annual_salary));
    state.memory.set8(at+0x34,12);
    for(uint32_t n=0;n<7;++n) state.memory.set8(at+0x38+n,uint8_t(fixture.request.bid.bonuses[n]));
    const auto native=fixture.directory/"EXISTING_BID_INPUT.FBM";
    WatcomRandom serialize(1); write_savegame(native,make_savegame(state,serialize));
    ControllerContext c(data,fixture,native);
    auto edited=fixture.request.bid;
    check(edited.fee>10000&&edited.annual_salary>100,"existing-bid edit control is positive");
    edited.fee-=10000; edited.annual_salary-=100;
    state.memory.set32(at+0x1c,uint32_t(edited.fee));
    state.memory.set32(at+0x0c,uint32_t(edited.annual_salary));
    HumanBuyerSaleHooks hooks;
    hooks.notice=[](const GameState&,const HumanBuyerSaleNotice&){
        throw std::runtime_error("existing-bid edit unexpectedly requalified or presented an ACK");
    };
    const auto request=c.bid(fixture,&edited,true);
    const Snapshot before_submit=*c.before_editor_submit;
    check(c.serve(request,hooks).empty()&&
          save_stream(c.session->state())==before_submit.native&&
          c.session->game().seed()==before_submit.game.seed()&&
          c.session->game().calls()==before_submit.game.calls()&&
          c.session->rand().seed()==before_submit.rand.seed()&&
          c.session->rand().calls()==before_submit.rand.calls()&&c.revision==before_submit.revision&&
          !c.flow.pending_transfer_offer()&&c.flow.screen_name()=="outgoing_transfer"&&
          c.flow.desktop()->state().revision==c.revision&&
          c.flow.state_json().find("\"feedback\":\"\"")!=std::string::npos,
          "affordable existing-bid submit is Source's early return: no Native, RNG, revision, qualification or ACK");
    const uint32_t edited_row=::bmh::layout::EditorListUnsigned+uint32_t(fixture.request.bid.list_entry)*0x58;
    check(c.session->state().memory.u16(edited_row)==1&&
          c.session->state().memory.u32(edited_row+0x1c)==uint32_t(edited.fee)&&
          c.session->state().memory.u32(edited_row+0x0c)==uint32_t(edited.annual_salary)&&
          c.session->state().memory.u8(edited_row+0x34)==12,
          "existing bidder's selected Native row keeps the Source term and commits only the edited prices");
    for(uint32_t n=0;n<7;++n)
        check(c.session->state().memory.u8(edited_row+0x38+n)==uint8_t(fixture.request.bid.bonuses[n]),
              "existing bid retains each of its seven Native premiums through KB31 editing");
    const auto repeated=c.bid(fixture,&edited,true);
    const Snapshot before_repeated=*c.before_editor_submit;
    check(c.serve(repeated,hooks).empty()&&save_stream(c.session->state())==before_repeated.native&&
          c.session->game().seed()==before_repeated.game.seed()&&
          c.session->game().calls()==before_repeated.game.calls()&&
          c.session->rand().seed()==before_repeated.rand.seed()&&
          c.session->rand().calls()==before_repeated.rand.calls()&&c.revision==before_repeated.revision&&
          !c.flow.pending_transfer_offer()&&
          c.flow.state_json().find("\"feedback\":\"\"")!=std::string::npos,
          "reopened existing-bid submit leaves the already committed owner fields unchanged");
}
void controller_purchase(const fs::path& data,const OrdinaryInput& fixture,bool throw_first) {
    ControllerContext c(data,fixture);
    const auto request=c.bid(fixture);
    check(c.before_editor_submit.has_value(),"KB31 has an acknowledged Native state before buyer decision");
    const Snapshot before=*c.before_editor_submit;
    // A heap-backed callback is borrowed through the actual controller. Its
    // Source ACK happens after controller hook construction has returned from
    // its local if-scope, exposing the production dangling std::function bug.
    const auto calls=std::make_shared<unsigned>(0);
    HumanBuyerSaleHooks hooks;
    hooks.notice=[&,calls](const GameState& staged,const HumanBuyerSaleNotice& notice){
        check(before.equals(*c.session,c.revision),"controller Source presenter sees stable live Native/W/G/revision");
        check(notice.manager==0&&notice.list_entry==fixture.request.bid.list_entry&&notice.raw_offer_slot==-1&&
              notice.source_text_address==::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField&&staged.memory.u8(::bmh::layout::ActiveManager)==0,
              "real controller invokes the ordinary buyer agreement with source identity");
        const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(notice.list_entry)*0x58;
        check(staged.memory.u16(at)==1&&
              (fixture.seller==0x0fff?(staged.squad_slots.at(0xb70u+size_t(notice.list_entry))&0xfff)==fixture.request.bid.player
                                    :contains(staged,fixture.seller,fixture.request.bid.player))&&
              !contains(staged,1,fixture.request.bid.player),"af304 runs on staged bid before account/allocation/move");
        const auto dialog=ui::pending_offer_notice_dialog(staged,
            {notice.manager,notice.raw_offer_slot,notice.source_text_address,notice.original_message});
        check(dialog.source_manager_flags==0x80&&dialog.body==message_text(staged,::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField),
              "actual controller notice keeps Source ACK presentation");
        ++*calls;
        if(throw_first) throw std::runtime_error("controlled actual-controller presenter exception");
    };
    check(c.serve(request,hooks).empty(),"actual controller serves the UI-owned request synchronously");
    check(*calls==1,"borrowed controller presenter remains alive for exactly one Source agreement");
    if(throw_first) {
        check(before.equals(*c.session,c.revision)&&c.flow.screen_name()=="outgoing_transfer"&&
              !c.flow.pending_transfer_offer()&&c.flow.desktop()->state().revision==c.revision&&
              contains(c.session->state(),fixture.seller,fixture.request.bid.player)&&
              !contains(c.session->state(),1,fixture.request.bid.player),
              "throwing real controller presenter rolls back only the submit stage and leaves edited KB31 retryable");
        const auto retry=c.bid(fixture); // actual UI retry, never a hand-made FlowResult
        throw_first=false;
        check(c.serve(retry,hooks).empty()&&*calls==2,"actual market controls retry after complete presenter rollback");
    }
    check(c.revision==before.revision+1&&c.flow.desktop()->state().revision==c.revision&&c.flow.screen_name()=="transfer_market"&&
          !c.flow.pending_transfer_market()&&contains(c.session->state(),1,fixture.request.bid.player)&&
          !contains(c.session->state(),fixture.seller,fixture.request.bid.player),
          "actual Controller/StartupFlow commits ownership and refreshed projection with exactly one revision");
    const uint32_t at=::bmh::layout::EditorListUnsigned+uint32_t(fixture.request.bid.list_entry)*0x58;
    check(c.session->state().squad_slots.at(0xb70u+size_t(fixture.request.bid.list_entry))==0xfff&&
          c.session->state().memory.u16(at)==1,"real controller retains ordinary bid while Source removes sold pool player");
    const Snapshot committed(*c.session,c.revision);
    const auto closed=c.flow.send_key("ESC");
    check(c.serve(closed).empty()&&c.flow.screen_name()=="desktop"&&c.flow.desktop()->state().revision==committed.revision&&
          committed.equals(*c.session,c.revision)&&c.playback.queued_bytes()==0,
          "physical market close restores refreshed native desk without another engine revision or audio");
    if(fixture.seller==0x0fff) {
        const auto& state=c.session->state();
        check(read32(state.manager_records,0x7d8)==uint32_t(state.player_name_offsets.at(size_t(fixture.request.bid.player)))+0xffff0000u,
              "actual free-agent controller purchase records format field history counterpart");
        for(size_t slot=0xb70;slot<3072;++slot)
            check((state.squad_slots.at(slot)&0xfff)!=fixture.request.bid.player,
                  "actual unchanged free-agent input loses all matching free-pool aliases");
        const auto& items=c.flow.desktop()->state().items;
        const auto save_item=std::find_if(items.begin(),items.end(),[](const auto& item){return item.kind==11&&!item.disabled;});
        check(save_item!=items.end(),"actual free-agent desk exposes Source Save menu11");
        const auto opened=c.flow.click(save_item->x+save_item->width/2,save_item->y+save_item->height/2);
        check(c.serve(opened).empty()&&c.flow.screen_name()=="save_load","actual free-agent Save UI opens");
        auto save=c.flow.send_key("ENTER");
        if(save.request!=ui::FlowRequest::file_action) save=c.flow.send_key("ENTER");
        check(save.request==ui::FlowRequest::file_action&&save.file_action.action==ui::SaveLoadAction::save&&
              save.file_action.path==c.input.directory/"AUTOSAVE.FBM","physical Save confirmation owns actual overwrite request");
        check(c.serve(save).empty()&&c.revision==committed.revision+1&&c.flow.screen_name()=="desktop",
              "actual free-agent Save writes Native and publishes one separate Save revision");
        const auto saved=load_savegame(c.input.directory/"AUTOSAVE.FBM");
        check(saved.checksum_ok&&saved.stream==committed.native&&save_stream(c.session->state())==committed.native&&
              c.session->game().seed()==committed.game.seed()&&c.session->game().calls()==committed.game.calls()&&
              c.session->rand().calls()==committed.rand.calls()+4,
              "real free-agent Save preserves gameplay Native/G and consumes only four encryption W draws");
        auto expected_w=committed.rand;(void)make_savegame(c.session->state(),expected_w);
        check(expected_w.seed()==c.session->rand().seed(),"actual free-agent Save has exact encryption W endpoint");
        ControllerContext reloaded(data,fixture,c.input.directory/"AUTOSAVE.FBM");
        check(save_stream(reloaded.session->state())==committed.native&&
              contains(reloaded.session->state(),1,fixture.request.bid.player),
              "actual new StartupFlow/controller Native load preserves free-agent buyer ownership and history");
    }
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "usage: human_buyer_market_app_tests ORIGINAL_DATA GENUINE_2PNWK001");
        if (!*argv[1]) { std::puts("human buyer market App tests: skipped (Original data not configured)"); return 77; }
        const fs::path data(argv[1]);
        {
            // Genuine, unchanged two-manager input: both Source own-player buttons
            // list the same player; the buyer owns the eventual loan decision.
            app::CareerSession sale(data, 37, argv[2]), loan(data, 37, argv[2]);
            uint64_t sale_revision = 1, loan_revision = 1;
            const auto observed = sale.preparation_projection(sale_revision).transfer_market;
            check(observed.has_value(), "genuine two-manager desk exposes the market");
            const auto player = std::find_if(observed->squad.begin(), observed->squad.end(),
                [](const auto& p) { return p.player == 118 && !p.already_listed; });
            check(player != observed->squad.end(), "genuine BURKARDT is available for own-player listing");
            const auto sell_request = app::TransferActions::listing(*observed, *player, false);
            const auto loan_request = app::TransferActions::listing(*observed, *player, true);
            HumanBuyerSaleHooks hooks; std::string error;
            const auto sold = app::TransferActions(sale).market(sell_request, hooks, sale_revision, error);
            const auto listed = app::TransferActions(loan).market(loan_request, hooks, loan_revision, error);
            check(sold && listed && sold->action.status == TransferActionStatus::accepted &&
                  listed->action.status == TransferActionStatus::accepted,
                  "VER-LEIHEN reaches the same owned Source listing caller as VER-KAUFEN");
            check(save_stream(sale.state()) == save_stream(loan.state()) &&
                  sale.rand().seed() == loan.rand().seed() && sale.rand().calls() == loan.rand().calls() &&
                  sale.game().seed() == loan.game().seed() && sale.game().calls() == loan.game().calls() &&
                  sale_revision == loan_revision && loan_revision == 2,
                  "listing loan selection preserves the complete sale-list Native/W/G/revision result");
            const Snapshot before(loan, loan_revision);
            auto stale = loan_request;
            check(!app::TransferActions(loan).market(stale, hooks, loan_revision, error) &&
                  before.equals(loan, loan_revision), "consumed loan listing cannot replay");
            stale.revision = loan_revision;
            stale.selected_players.push_back(stale.selected_players.front());
            check(!app::TransferActions(loan).market(stale, hooks, loan_revision, error) &&
                  before.equals(loan, loan_revision), "unowned marked loan batch remains inert");
        }
        const OrdinaryInput fixture(argv[2]);
        controller_unchanged_capacity_notice(data,fixture);
        controller_existing_bid_edit(data,fixture);
        controller_purchase(data,fixture,false);
        controller_purchase(data,fixture,true);
        const OrdinaryInput free_agent(argv[2],true);
        controller_purchase(data,free_agent,false);
        {
            app::CareerSession session(data, 1, fixture.file);
            uint64_t revision = 1;
            std::string error;
            const Snapshot before(session, revision);
            int notices = 0;
            HumanBuyerSaleHooks hooks;
            hooks.allocator_gate = HumanBuyerSaleAllocatorGate::blocked;
            hooks.direct_mode = HumanBuyerSaleDirectMode::unsupported;
            hooks.before_accounts = [](const auto&, const auto&) { throw std::runtime_error("presenter cannot own caller work"); };
            hooks.after_paid_accounts = [](auto&, const auto&) { throw std::runtime_error("presenter cannot inject pending caller work"); };
            hooks.notice = [&](const GameState& staged, const HumanBuyerSaleNotice& notice) {
                check(before.equals(session, revision), "live Native/W/G/revision unchanged during Source notice");
                check(notice.source_text_address == ::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField && notice.manager == 0 &&
                      notice.list_entry == fixture.request.bid.list_entry && notice.raw_offer_slot == -1,
                      "ordinary agreement notice has actual buyer and list provenance");
                const auto view = ui::pending_offer_notice_dialog(staged,
                    {notice.manager, notice.raw_offer_slot, notice.source_text_address, notice.original_message});
                check(view.source_manager_flags == 0x80 && view.source_flags == 0 &&
                      view.body == message_text(staged, ::bmh::layout::DeskActions_SubmitTransferBidImpl_ExecutableField), "Original legacy routine agreement projection");
                std::string nested_error;
                check(!session.save_game(fixture.directory / "NESTED.FBM", 0, false, nested_error),
                      "nested Save is rejected during staged market notice");
                check(before.equals(session, revision), "nested Save does not mutate live state or RNG");
                ++notices;
            };
            const auto result = session.dispatch_transfer_market(fixture.request, hooks, revision, error);
            check(result && result->status == TransferActionStatus::accepted && revision == 2 && notices == 1 && error.empty(),
                  "actual EXE gate and owned ordinary mode override presenter input and commit a purchase");
            check(contains(session.state(), 1, fixture.request.bid.player) &&
                  !contains(session.state(), fixture.seller, fixture.request.bid.player), "ordinary player ownership moved");
            const uint32_t at = ::bmh::layout::EditorListUnsigned + uint32_t(fixture.request.bid.list_entry) * 0x58;
            check(session.state().squad_slots.at(0xb70u + size_t(fixture.request.bid.list_entry)) == 0xfff &&
                  session.state().memory.u16(at) == 1, "Source removal clears pool without blanket bid-row cleanup");
            check(!session.state().memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2) && !session.state().memory.has(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2),
                  "owned direct runtime never materializes globals into Native memory");
            const auto after = save_stream(session.state());
            const auto draws = session.rand().calls();
            const auto saved = fixture.directory / "PURCHASE.FBM";
            check(session.save_game(saved, 0, false, error), "actual session Save succeeds after direct purchase");
            check(session.rand().calls() == draws + 4 && save_stream(session.state()) == after,
                  "Native Save consumes only four encryption draws and preserves gameplay bytes");
            app::CareerSession reloaded(data, 1, saved);
            check(save_stream(reloaded.state()) == after && contains(reloaded.state(), 1, fixture.request.bid.player),
                  "fresh process-style session reload preserves complete ordinary purchase state");
            const auto desk = reloaded.desktop_projection(1);
            ui::DeskRequest next;
            next.kind = ui::DeskRequestKind::advance_turn;
            next.manager_slot = desk.manager_slot;
            next.revision = 1;
            uint64_t reload_revision = 1;
            const auto advance = reloaded.dispatch_kickoff(next, reload_revision, error);
            check(advance.kind == TurnDeskKind::NextDesk && advance.manager_slot == 1 && reload_revision == 2 && error.empty(),
                  "reloaded ordinary purchase continues to remaining manager's desk");
        }
        for (bool throw_notice : {false, true}) {
            app::CareerSession session(data, 1, fixture.file);
            uint64_t revision = 1;
            std::string error;
            const Snapshot before(session, revision);
            HumanBuyerSaleHooks hooks;
            if (throw_notice) hooks.notice = [&](const GameState&, const HumanBuyerSaleNotice&) {
                check(before.equals(session, revision), "live state remains isolated at throwing callback");
                throw std::runtime_error("stopped Original agreement presenter");
            };
            const auto result = session.dispatch_transfer_market(fixture.request, hooks, revision, error);
            check(!result && !error.empty() && before.equals(session, revision),
                  "missing or stopped Source presenter discards complete staged Native/W/G/revision");
        }
        {
            app::CareerSession session(data, 1, fixture.file);
            uint64_t revision = 1;
            std::string error;
            const Snapshot before(session, revision);
            auto stale = fixture.request;
            stale.revision = 2;
            HumanBuyerSaleHooks hooks;
            hooks.notice = [](const auto&, const auto&) { throw std::runtime_error("stale request reached presenter"); };
            check(!session.dispatch_transfer_market(stale, hooks, revision, error) && before.equals(session, revision),
                  "stale ordinary request is rejected before staging or notices");
        }
        std::puts("human buyer market App tests: passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "human buyer market App tests: %s\n", e.what());
        return 1;
    }
}
