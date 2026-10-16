#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "formats/m3d.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/career.hpp"
#include "game/matchday_setup.hpp"
#include "game/national.hpp"
#include "game/month.hpp"
#include "game/pending_offer.hpp"
#include "game/save.hpp"
#include "game/turn.hpp"
#include "game/turn_driver.hpp"
#include "game/week.hpp"
#include "game/weekly.hpp"
#include "game/human_buyer_sale.hpp"
#include "game/transfer_bid.hpp"
#include "game/prediction.hpp"
#include "game/lineup.hpp"
#include "national_coach_desk_pending_fixture.hpp"

using namespace bmh;
namespace {
constexpr uint32_t kCalendar = ::bmh::layout::CompetitionCalendar, kCompetition = ::bmh::layout::SavedCompetition;
constexpr uint32_t kCoachActive = ::bmh::layout::CoachActive, kOriginalCount = ::bmh::layout::OriginalManagerCount;
constexpr uint32_t kManagerCount = ::bmh::layout::ManagerCount, kOrder = ::bmh::layout::NextDeskManager;
constexpr uint32_t kPosition = ::bmh::layout::DeskPosition, kActive = ::bmh::layout::ActiveManager, kOffers = ::bmh::layout::OfferSlots;
constexpr size_t kPoolSlots = 0xb70;

void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

struct Source {
    LeImage exe;
    DbCounts counts;
    Database database;
    TextDb texts;
    explicit Source(const std::filesystem::path& data)
        : exe(bmh::test::load_executable(data / "BMH.exe")),
          counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)},
          database(load_database(data / "M3D.MAN", counts)), texts(load_textd(data / "TEXTD.BLK")) {}
    GameState load(const std::filesystem::path& path) const {
        const auto file = load_savegame(path);
        check(file.checksum_ok && file.version == "V2.24", path.filename().string() + " Native checksum/version");
        auto g = GameState::from_save(SaveState(file.stream, counts), counts);
        competition_pointers(g.memory, exe);
        g.texts = &texts;
        national_coach_after_load(g);
        return g;
    }
};

void verify_case(const Source& source, const std::filesystem::path& path, bool run_departure) {
    GameState input = source.load(path);
    const auto& mem = input.memory;
    const uint16_t entry = mem.u16(kCalendar + uint32_t(input.season_day()) * 2);
    check(entry == 0x4006 && day_competitions(input, entry) == 0x800000 &&
          mem.u32(kCompetition) == 0 && mem.u8(kCoachActive) != 0 &&
          input.manager_teams.size() == 1 && input.manager_teams[0] == kNoTeam &&
          mem.u8(kManagerCount) == 1,
          path.filename().string() + " is a canonical no-fixture national coach desk");
    const int original_count = mem.u8(kOriginalCount);
    check(original_count >= 1 && original_count <= 4 && mem.u16(kOrder) < original_count &&
          mem.u8(kPosition) == 0 && mem.u8(kActive) == 0,
          path.filename().string() + " preserves a valid original manager prefix and open desk cursor");

    std::array<int, 2> prefixes{0, int(mem.u16(kOrder))};
    if (original_count > 1) prefixes[1] = prefixes[0] == 0 ? 1 : 0;
    else prefixes[1] = -1;
    const auto set_valid_prefix = [original_count](GameState& state, int prefix) {
        int source_position = -1;
        for (int i = 0; i < original_count; ++i)
            if (state.memory.u16(kOrder + uint32_t(i) * 2) == prefix) {
                source_position = i;
                break;
            }
        if (source_position < 0) return false;
        const uint16_t old_prefix = state.memory.u16(kOrder);
        state.memory.set16(kOrder, uint16_t(prefix));
        state.memory.set16(kOrder + uint32_t(source_position) * 2, old_prefix);
        return true;
    };
    int exercised_prefix_one = 0;
    bool departed = false;
    for (int prefix : prefixes) {
        if (prefix < 0 || prefix >= original_count) continue;
        GameState g = input;
        // Controlled saved-order prefix transform swaps two existing entries,
        // preserving the native permutation and original-count provenance.
        check(set_valid_prefix(g, prefix), "requested prefix is present in the saved permutation");
        TurnDriver driver;
        const auto refs = source.database.raw_referee_properties;
        std::string error;
        check(begin_turn_driver(driver, g, {}, entry, refs, 700 + uint64_t(prefix), 1, error),
              path.filename().string() + " national saved desk binds: " + error);
        check(!driver.enable_national_nomination_desks && driver.national_nomination_desk,
              "classification is independent of the default-off GUI policy");
        {
            // The UI may dispatch the driver only after the staged Source helper
            // has written the completed cursor. A premature request stays
            // retryable and cannot claim the cursor or touch state/RNG.
            TurnDriver premature = driver;
            premature.enable_national_nomination_desks = true;
            const auto native_before = save_stream(g);
            GameRandom premature_game(0x5123u + uint32_t(prefix));
            WatcomRandom premature_rand(0x8345u + uint32_t(prefix));
            const auto game_seed = premature_game.seed();
            const auto game_calls = premature_game.calls();
            const auto rand_seed = premature_rand.seed();
            const auto rand_calls = premature_rand.calls();
            MinuteReport premature_report;
            const auto rejected = advance_to_next_desk(
                premature, g, TurnAdvanceRequest{0, 1}, premature_game,
                premature_rand, premature_report);
            check(rejected.kind == TurnDeskKind::Failed &&
                  rejected.reason == "national_nomination_pending_not_completed" &&
                  premature.stage == TurnDriver::Stage::MatchdayDesks &&
                  premature.revision == 1 && save_stream(g) == native_before &&
                  premature_game.seed() == game_seed && premature_game.calls() == game_calls &&
                  premature_rand.seed() == rand_seed && premature_rand.calls() == rand_calls,
                  "incomplete Source cursor is rejected before claim and remains retryable");
        }
        const auto order_before = std::array<uint16_t, 4>{
            g.memory.u16(kOrder), g.memory.u16(kOrder + 2),
            g.memory.u16(kOrder + 4), g.memory.u16(kOrder + 6)};
        GameRandom game(0x8a71u + uint32_t(prefix));
        WatcomRandom rand(0x334455u + uint32_t(prefix));
        const auto game_seed = game.seed();
        const auto game_calls = game.calls();
        const auto rand_seed = rand.seed();
        const auto rand_calls = rand.calls();
        int hook_calls = 0;
        PendingOfferHooks hooks;
        hooks.answer = [&](const auto&, const auto&) { ++hook_calls; return PendingOfferAnswer::unsupported; };
        hooks.select_transfer = [&](const auto&, const auto&) {
            ++hook_calls; return PendingOfferTransferChoice{};
        };
        hooks.notice = [&](const auto&, const auto&) { ++hook_calls; };
        MinuteReport report;
        const auto cleanup = process_national_nomination_pending_after_desk(
            g, PendingOfferDeskRequest{PendingOfferDeskRuntimeMode::normal_interactive, 1, 0},
            game, rand, hooks, report);
        check(cleanup.completed && cleanup.processed == 0 && !report.unsupported && hook_calls == 0,
              "unowned national pending batch performs Source cleanup without UI callbacks");
        check(game.seed() == game_seed && game.calls() == game_calls &&
              rand.seed() == rand_seed && rand.calls() == rand_calls,
              "national pending cleanup is RNG-free");
        check(g.memory.u8(kActive) == prefix && g.memory.u8(kPosition) == 1,
              "Source restores active owner from bd6cc[0] and writes completed count one");
        for (int i = 0; i < 4; ++i) {
            check(g.squad_slots.at(kPoolSlots + size_t(40 + i)) == 0x0fff &&
                  g.memory.u32(kOffers + uint32_t(i) * 4) == 0xffffffffu,
                  "Source clears each special pool/offer slot");
        }
        for (int i = 0; i < 4; ++i)
            check(g.memory.u16(kOrder + uint32_t(i) * 2) == order_before[size_t(i)],
                  "national cleanup preserves the entire saved manager-order prefix");
        GameState expected = input;
        check(set_valid_prefix(expected, prefix), "expected prefix exists in saved permutation");
        expected.memory.set8(kActive, uint8_t(prefix));
        expected.memory.set8(kPosition, 1);
        for (int i = 0; i < 4; ++i) {
            expected.squad_slots.at(kPoolSlots + size_t(40 + i)) = 0x0fff;
            expected.memory.set32(kOffers + uint32_t(i) * 4, 0xffffffffu);
        }
        check(save_stream(g) == save_stream(expected),
              "cleanup changes only Source active/cursor and four global special entries");

        if (original_count > 1 && prefix == 1) ++exercised_prefix_one;
        if (run_departure && !departed) {
            driver.enable_national_nomination_desks = true;
            MinuteReport departure_report;
            const uint32_t old_date = g.date;
            const auto result = advance_to_next_desk(driver, g, TurnAdvanceRequest{0, 1},
                                                     game, rand, departure_report);
            check(result.kind != TurnDeskKind::Failed && !departure_report.unsupported &&
                  result.date > old_date && result.audio_events.empty() &&
                  !driver.last_results.valid,
                  "national empty desk advances without fake results or match audio");
            departed = true;
        }

        // The no-owner native fixture starts without an offered manager word;
        // the exact byte comparison above is the mutation oracle.
    }
    if (original_count > 1)
        check(exercised_prefix_one != 0,
              path.filename().string() + " exercises Source's valid nonzero restored owner prefix");
    if (run_departure) check(departed, "one real Native national desk exercised its empty-event departure");
}

void verify_owned_offers(const Source& source, const std::filesystem::path& saves) {
    namespace control = national_pending_control;
    for(int owner:{0,1}) for(bool nomination:{false,true}) for(bool yes:{false,true}) {
        auto g=source.load(saves/(owner==1?"2PNWK060.FBM":"NATWK001.FBM"));
        const auto o=control::prepare(g,owner);
        auto expected=g;const auto before=g;
        GameRandom game(73);WatcomRandom rand(1),reference(1);MinuteReport report;
        int questions=0;PendingOfferHooks hooks;
        hooks.answer=[&](const GameState& state,const PendingOfferQuestion& q){
            ++questions;
            check(q.kind==PendingOfferQuestionKind::salary_demand && q.manager==owner &&
                q.offered_player==o.player && q.clicked_player==o.player && q.clicked_source_slot==o.anchor &&
                q.current_clicked_name==state.player_name(o.player) && state.memory.u8(kActive)==owner,
                "raw Source owner and fixed native anchor reach salary presenter");
            return yes?PendingOfferAnswer::yes:PendingOfferAnswer::no;
        };
        hooks.select_transfer=[](const auto&,const auto&)->PendingOfferTransferChoice {
            throw std::runtime_error("salary NO is sticky and bypasses transfer chooser");
        };
        hooks.notice=[](const auto&,const auto&){throw std::runtime_error("forced sale has no ACK");};
        if(yes) {
            const size_t board=size_t(owner)*0x840+size_t(o.row)*0x58;
            uint32_t fee=0,salary=0;
            market_value(expected,o.anchor,0,98,fee,salary,(expected.tactics.at(board+12)+6)/12,0,reference);
            check(salary!=0,"controlled actual player's Source quote is defined");
            control::write(expected.tactics,board+0x50,o.salary);
            expected.tactics.at(board+13)=uint8_t(o.salary*100u/salary);
            control::cleanup(expected,owner);
        }
        const auto result=nomination
            ? process_national_nomination_pending_after_desk(g,control::request(),game,rand,hooks,report)
            : process_natural_pending_offers_after_desk(g,control::request(),game,rand,hooks,report);
        check(result.completed && !report.unsupported && result.processed==1 && questions==1,
            std::string("owned national batch completes: ")+report.reason);
        check(g.manager_teams==std::vector<int>{kNoTeam} && g.memory.u8(kActive)==owner &&
            g.memory.u8(kPosition)==1 && game.seed()==73 && game.calls()==0,
            "raw owner restoration preserves reduced runtime managers and consumes no G");
        if(yes) {
            check(result.salary_accepted==1 && result.transferred==0 &&
                save_stream(g)==save_stream(expected) && rand.seed()==reference.seed() && rand.calls()==reference.calls(),
                "salary JA matches entire independent Source Native/market-W composition");
        } else {
            check(result.transferred==1 && result.forced_acceptance && rand.calls()==1 && rand.seed()==1103527590 &&
                !control::contains(g,o.seller,o.player) && control::contains(g,o.buyer,o.player),
                "salary NO performs Source AI sale with one exact W draw");
            check(control::read(g.manager_records,size_t(owner)*0xb0c+0x314)==
                control::read(before.manager_records,size_t(owner)*0xb0c+0x314)+o.fee,
                "Source param8 receives fee even when its raw club is inactive");
            for(int record:{0,1,4}) {
                if(size_t(record+1)*0xb0c>g.manager_records.size())continue;
                const auto at=size_t(record)*0xb0c+0x760;
                check(std::equal(before.manager_records.begin()+long(at),before.manager_records.begin()+long(at+240),
                    g.manager_records.begin()+long(at)),
                    "retained AI owner and format field's excluded parked owner4 receive no human history");
            }
            if(owner==1)check(g.tactics==before.tactics,
                "inactive format field seller has no human board compaction or parked-board writes");
            else check(std::equal(before.tactics.begin(),before.tactics.begin()+0x840,g.tactics.begin()),
                "owner0 AI sale does not rewrite national salary board");
        }
    }
    // A national seller is not constructed by format field. Distinguish damaged
    // provenance from a valid old-club player that also occurs in the national squad.
    auto invalid=source.load(saves/"NATWK001.FBM");control::prepare(invalid,0);
    invalid.memory.set16(control::list+8,kNoTeam|0x8000);
    GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
    const auto result=process_national_nomination_pending_after_desk(invalid,control::request(),game,rand,{},report);
    check(!result.completed && report.unsupported && report.reason.find("invalid special offer")!=std::string::npos &&
        game.calls()==0 && rand.calls()==0,"synthetic national seller is diagnosed as invalid Source provenance");
}

void verify_parked_buyer(const Source& source,const std::filesystem::path& saves) {
    namespace control=national_pending_control;
    for(bool loan:{false,true})for(int boundary=0;boundary<4;++boundary) {
        auto g=source.load(saves/"2PNWK060.FBM");
        const auto o=control::prepare_parked_buyer(g,loan);
        if(boundary==1)control::write(g.manager_records,4*0xb0c+0x314,0);
        if(boundary==3)control::fill_with_free_agents(g,o.buyer);
        const auto before=g;GameRandom game(73);WatcomRandom rand(1),reference(1);MinuteReport report;
        int salary_calls=0;std::vector<uint32_t> notices;
        PendingOfferHooks hooks;
        hooks.human_buyer_allocator_gate=boundary==2?PendingOfferAllocatorGate::blocked:PendingOfferAllocatorGate::zero;
        hooks.answer=[&](const auto&,const auto& q){++salary_calls;
            check(q.manager==1 && q.kind==PendingOfferQuestionKind::salary_demand,"retained param8 owns the buyer4 salary question");
            return PendingOfferAnswer::no;};
        hooks.select_transfer=[](const auto&,const auto&)->PendingOfferTransferChoice {
            throw std::runtime_error("sticky buyer4 salary NO bypasses chooser");};
        hooks.notice=[&](const GameState& state,const PendingOfferNotice& notice) {
            notices.push_back(notice.text_address);
            if(boundary==1)check(notice.manager==4 && notice.text_address==::bmh::layout::Advertising_FormatError_TextPointer18 && state.memory.u8(kActive)==4,
                "legacy routine insufficient funds notice temporarily owns native manager4");
            else check(boundary==3 && notice.text_address==::bmh::layout::PendingOffer_FormatError_ExecutableField && notice.manager==1 && state.memory.u8(kActive)==1,
                "full buyer notice observes restored pending owner1");
        };
        const auto result=process_national_nomination_pending_after_desk(g,control::request(),game,rand,hooks,report);
        check(result.completed && !report.unsupported && salary_calls==1 && result.processed==1 &&
            result.buyer_funds_refused==(boundary==1) && result.buyer_allocator_blocked==(boundary==2) &&
            result.buyer_no_room==(boundary==3) && result.transferred==(boundary==0),
            std::string("native buyer4 scoped transaction: ")+report.reason);
        check(g.manager_teams==std::vector<int>{kNoTeam} && g.memory.u8(kActive)==1 && g.memory.u8(kPosition)==1 &&
            game.calls()==0 && game.seed()==73,"buyer4 settlement retains reduced native manager list and Source cursor");
        const bool paid=boundary!=1;
        check(control::read(g.manager_records,0xb0c+0x314)==control::read(before.manager_records,0xb0c+0x314)+(paid?o.fee:0) &&
            control::read(g.manager_records,4*0xb0c+0x314)==control::read(before.manager_records,4*0xb0c+0x314)-(paid?o.fee:0),
            "legacy routine credits retained param8 seller1 and debits actual parked buyer4");
        check(control::read(g.manager_records,0xb0c+0x330)==control::read(before.manager_records,0xb0c+0x330)-(paid?o.fee:0) &&
            control::read(g.manager_records,4*0xb0c+0x330)==control::read(before.manager_records,4*0xb0c+0x330)+(paid?o.fee:0),
            "both exact transfer-account ledgers follow actual payment outcome");
        for(int owner:{0,1,4}) {
            const size_t at=size_t(owner)*0xb0c+0x760;
            check(std::equal(before.manager_records.begin()+long(at),before.manager_records.begin()+long(at+240),
                g.manager_records.begin()+long(at)),"format field skips retained AI seller and parked buyer4 histories");
        }
        check(std::equal(before.tactics.begin(),before.tactics.begin()+4*0x840,g.tactics.begin()),
            "buyer4 cannot copy the retained raw seller1 board into a new buyer board");
        for(uint32_t at=::bmh::layout::Career_CareerManagersEnd_RuntimeField;at<::bmh::layout::FixtureNationalWorkspaceEnd;++at)if(before.memory.has(at))
            check(g.memory.u8(at)==before.memory.u8(at),"retained Source AI seller needs no human loan snapshot or counter");
        if(boundary==0) {
            check(!control::contains(g,o.seller,o.player) && control::contains(g,o.buyer,o.player),
                "valid native buyer4 receives the actual player");
            // Independent legacy routine RNG/board composition, using Source handback
            // for the ordinary bonus oracle instead of a synthetic manager list.
            auto expected=before;const auto range=squad_range(o.buyer);size_t slot=range.first;
            while(expected.squad_slots.at(slot)!=0xfff)++slot;
            const int row=int(slot-range.first);const size_t board=4*0x840+size_t(row)*0x58;
            reference.range(90,100);expected.squad_slots.at(slot)=uint16_t(o.player);
            new_board_entry(expected,4,row,int(slot),reference);
            control::write(expected.tactics,board+0x50,o.salary);
            uint32_t fee=0,salary=0;market_value(expected,int(control::pool),0,100,fee,salary,loan?12:24,0,reference);
            for(int k=0;k<7;++k)expected.tactics.at(board+0x12+size_t(k))=uint8_t(k+2);
            auto ordinary=expected;national_coach_hand_back(ordinary);
            const uint32_t bonus=expected_bonus(ordinary,0,0xff,row,o.player,true);
            check(expected_bonus_parked_club_source(expected,0xff,row,o.player,true)==bonus,
                "native record4 bonus equals real Source handback ordinary-club calculation");
            const uint32_t worth=o.salary+uint32_t(int32_t(bonus*80u)/100);
            expected.tactics.at(board+0x0d)=uint8_t(std::clamp(int32_t(worth*100u/salary),0,255));
            control::write(expected.tactics,board+0x54,o.fee);expected.tactics.at(board+0x0c)=loan?0xff:24;
            MinuteReport removal;check(remove_from_seller(expected,o.seller,o.player,removal) && !removal.unsupported,
                "independent Source AI seller removal is defined");
            auto_lineup_parked_club_source(expected,o.buyer,100);
            check(std::equal(expected.tactics.begin()+4*0x840,expected.tactics.begin()+5*0x840,g.tactics.begin()+4*0x840) &&
                rand.seed()==reference.seed() && rand.calls()==reference.calls(),
                "complete buyer4 board and exact W draws match independent Source helper composition");
            int loans=0;
            for(int k=0;k<200;++k)if((g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(k)*4)&0xfff)==uint32_t(o.player) &&
                g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(k)*4)!=0) {
                ++loans;check(loan && g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(k)*4)==uint32_t(o.player+o.seller*0x1000+o.buyer*0x400000) &&
                    g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField+uint32_t(k))==12,"Source AI-owner loan ledger has exact seller/buyer/term");
            }
            check(loans==int(loan),"only accepted loans create a return ledger entry");
        } else {
            check(g.tactics==before.tactics && control::contains(g,o.seller,o.player) && !control::contains(g,o.buyer,o.player) &&
                rand.calls()==1 && rand.seed()==1103527590,"Source partial payment/refusal boundary moves no player or board and draws one W");
        }
        check(notices.size()==size_t(boundary==1 || boundary==3),"forced buyer4 flow sends only its actual refusal/full notice");
    }
    // Normal fee negotiation must rate the parked buyer's bonuses, not silently
    // classify the club as AI. Compare the primitive against a real handback.
    {
        auto g=source.load(saves/"2PNWK060.FBM");const auto o=control::prepare_parked_buyer(g,false,false);
        g.memory.set8(kActive,1);const auto native=save_stream(g);auto ordinary=g;national_coach_hand_back(ordinary);
        TransferBidRatingRequest request{int(control::pool),40,0,o.buyer,o.player};request.source_pending_owner=SourcePendingOwner{1,o.seller};
        auto legacy=request;legacy.source_pending_owner.reset();WatcomRandom a(1),b(1);MinuteReport ra,rb;
        const auto actual=rate_transfer_bid_source(g,request,a,ra),oracle=rate_transfer_bid_source(ordinary,legacy,b,rb);
        check(actual.supported && oracle.supported && !ra.unsupported && !rb.unsupported && actual.fee_pct==oracle.fee_pct &&
            actual.salary_pct==oracle.salary_pct && a.seed()==b.seed() && a.calls()==b.calls() && save_stream(g)==native,
            "format field native buyer4 rating equals real handback and restores its entire borrowed board");
    }
    {
        auto g=source.load(saves/"2PNWK060.FBM");const auto o=control::prepare_parked_buyer(g,false,false);
        GameRandom game(73);WatcomRandom rand(1);MinuteReport report;PendingOfferHooks hooks;int choices=0,acks=0;
        hooks.human_buyer_allocator_gate=PendingOfferAllocatorGate::zero;
        hooks.select_transfer=[&](const GameState&,const TransferOfferListView& view) {
            ++choices;check(view.seller_manager==1 && view.offers[0].buyer_manager==4 && view.offers[0].transaction_supported,
                "Source fee chooser receives retained seller1 and actual native buyer4");
            return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};
        };
        hooks.notice=[&](const GameState& state,const PendingOfferNotice& notice) {
            ++acks;check(notice.manager==4 && notice.text_address==::bmh::layout::PendingOffer_FormatError_ExecutableField2 && state.memory.u8(kActive)==1,
                "Source normal sale ACK uses buyer4/System identity after active owner1 restoration");
        };
        const auto result=process_national_nomination_pending_after_desk(g,control::request(),game,rand,hooks,report);
        check(result.completed && !report.unsupported && result.transferred==1 && choices==1 && acks==1 &&
            game.calls()==1 && control::contains(g,o.buyer,o.player),"real buyer4 fee chooser/rating/ACK/settlement completes");
    }
    // The public shared opt-in rejects unrelated or damaged coordinates before
    // any native/account/RNG changes; defaults do not acquire hidden new owners.
    for(int damage=0;damage<8;++damage) {
        auto g=source.load(saves/"2PNWK060.FBM");const auto o=control::prepare_parked_buyer(g);
        g.memory.set8(kActive,1);
        HumanBuyerSaleRequest request;request.list_entry=40;request.bid_slot=0;request.list_address=control::list;
        request.source_pool_slot=control::pool;request.expected_seller=o.seller;request.expected_buyer=o.buyer;
        request.expected_active_manager=1;request.raw_offer_slot=0;request.source_pending_owner=SourcePendingOwner{1,o.seller};
        if(damage==0)request.source_pending_owner.reset();
        if(damage==1)request.source_pending_owner->team=o.seller+1;
        if(damage==2)g.memory.set8(::bmh::layout::OriginalManagerCount,1);
        if(damage==3)g.memory.set32(control::offers,uint32_t(o.player)*1024);
        if(damage==4)g.squad_slots.at(control::pool)=uint16_t(o.player+1);
        if(damage==5)request.day_step=true;
        if(damage==6)request.bid_slot=1;
        if(damage==7)g.memory.set16(control::list+2,uint16_t(o.buyer));
        const auto before=save_stream(g);WatcomRandom rand(1);MinuteReport report;HumanBuyerSaleHooks hooks;
        hooks.allocator_gate=HumanBuyerSaleAllocatorGate::zero;hooks.notice=[](const auto&,const auto&){throw std::runtime_error("invalid shared context reached presenter");};
        const auto rejected=settle_human_buyer_sale(g,request,hooks,rand,report);
        check(rejected.status==HumanBuyerSaleStatus::unsupported && report.unsupported && save_stream(g)==before && rand.calls()==0,
            "explicit national shared context is required and strictly validates every raw coordinate");
    }

}

void verify_parked_seller_loan(const Source& source,const std::filesystem::path& saves) {
    namespace c=national_pending_control;
    auto input=source.load(saves/"NATLOAN.FBM");
    constexpr int player=118,seller=1,buyer=12;
    check(input.manager_teams==std::vector<int>{kNoTeam} && c::record_team(input,4)==seller &&
        (input.memory.u32(c::offers)>>10 & 0xfff)==player && input.memory.u32(c::offers)==120832 &&
        input.memory.u8(c::list+0x34)==0x8c && c::contains(input,kNoTeam,player) &&
        c::contains(input,seller,player),"declared Source-generated BURKARDT loan has real national/club ownership");
    const auto range=squad_range(seller);size_t row=0;
    while(row<range.count && (input.squad_slots.at(range.first+row)&0xfff)!=player)++row;
    check(row<range.count,"source loan player belongs to parked club");
    const auto board=4*0x840+row*0x58;
    const std::vector<uint8_t> contract(input.tactics.begin()+long(board),input.tactics.begin()+long(board+0x58));
    for(bool nomination:{false,true})for(uint8_t prior_count:{0,2})for(bool transient:{false,true}) {
        auto g=input;g.memory.set8(::bmh::layout::DeskActions_Unsupported_RuntimeField,prior_count);
        if(transient){g.memory.add_block(::bmh::layout::FixtureNationalWorkspaceEnd,4);g.memory.set32(::bmh::layout::FixtureNationalWorkspaceEnd,0x263ff0);}
        GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
        int choices=0,notices=0;PendingOfferHooks hooks;
        hooks.select_transfer=[&](const auto&,const TransferOfferListView& view){
            ++choices;check(view.seller_manager==0 && view.seller==seller && view.offers[0].loan,
                "national transfer chooser preserves explicit owner0 and parked seller");
            return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};};
        hooks.answer=[](const auto&,const auto&){return PendingOfferAnswer::yes;};
        hooks.notice=[&](const auto&,const auto& n){++notices;check(n.manager==0 && n.text_address==::bmh::layout::PendingOffer_FormatError_ExecutableField2,"Source paid ACK belongs to owner0");};
        const auto result=nomination?process_national_nomination_pending_after_desk(g,c::request(),game,rand,hooks,report):
            process_natural_pending_offers_after_desk(g,c::request(),game,rand,hooks,report);
        check(result.completed && !report.unsupported && result.transferred==1 && choices==1 && notices==1,
            std::string("parked seller loan completes instead of requiring a graphics pointer: ")+report.reason);
        check(!c::contains(g,seller,player) && c::contains(g,buyer,player) &&
            g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField)==uint32_t(player+seller*0x1000+buyer*0x400000) &&
            g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField)==12 && g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField)==player &&
            g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2)==0 && g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==prior_count+1 &&
            (transient ? g.memory.u32(::bmh::layout::FixtureNationalWorkspaceEnd)==0x263ff0 : !g.memory.has(::bmh::layout::FixtureNationalWorkspaceEnd)),"real owner0 saved snapshot/count/12-month ledger replace the Original pointer-write bug");
        for(size_t b=0;b<contract.size();++b)check(g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField+uint32_t(b))==contract[b],
            "loan preserves exact parked club board, not the current national board");
        check(std::equal(input.tactics.begin(),input.tactics.begin()+0x840,g.tactics.begin()),
            "national tactics board remains untouched by parked club movement");
        check(game.calls()==1 && rand.calls()==2,"fee chooser and settlement keep actual Source G1/W2 draws");
        // Normal FBM save/load while still national must preserve owner0's saved snapshot.
        auto save_rand=rand;const auto file=make_savegame(g,save_rand);
        auto loaded=GameState::from_save(SaveState(file.stream,source.counts),source.counts);
        loaded.texts=&source.texts;competition_pointers(loaded.memory,source.exe);national_coach_after_load(loaded);
        // format field resumes via31ad9: rebind previous-active and re-run
        // the existing national optimizer (also resets shared marking bytes).
        auto reloaded_expected=g;reloaded_expected.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField3,0);
        auto_lineup(reloaded_expected,kNoTeam,0xe4);
        check(save_stream(loaded)==save_stream(reloaded_expected) && loaded.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2)==0 &&
            loaded.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==prior_count+1,"native national save/reload preserves persistent loan owner without a fifth counter");
        // Declared expiry control: next month tick; production day_step_rest must
        // hand back before loan returns, then restore the national takeover.
        loaded.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField,1);loaded.date=loaded.season_start+31;
        GameRandom expiry_game(73);WatcomRandom expiry_rand(1);MinuteReport expiry;
        check(day_step_rest(loaded,expiry_game,expiry_rand,expiry,uint16_t(loaded.season_day())),
            std::string("monthly return after source handback: ")+expiry.reason);
        check(!expiry.unsupported && loaded.manager_teams==std::vector<int>{kNoTeam} &&
            c::contains(loaded,seller,player) && !c::contains(loaded,buyer,player) &&
            loaded.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField)==0 && loaded.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField)==0xfff &&
            loaded.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==prior_count,"monthly return restores club ownership and frees persistent owner0 snapshot/count");
    }
}

void verify_national_day_source_counter(const Source& source,
                                       const std::filesystem::path& saves) {
    GameState base;
    bool found_multi_manager = false;
    for (const char* name : {"2PNWK060.FBM", "NATWK001.FBM", "NATWK008.FBM", "NATWK012.FBM"}) {
        const auto path = saves / name;
        if (!std::filesystem::exists(path)) continue;
        GameState candidate = source.load(path);
        if (national_coach_active(candidate)) national_coach_hand_back(candidate);
        if (candidate.manager_teams.size() >= 2) {
            base = std::move(candidate);
            found_multi_manager = true;
            break;
        }
    }
    check(found_multi_manager, "counter regression finds a genuine multi-manager Native save");

    // Make this a Source no-eligible multi-manager control. legacy routine clears
    // national morale bit 7 at day one before checking eligibility, and only restores
    // it after an eligible coach takes over.
    base.memory.set32(::bmh::layout::CompetitionSelection, 0);  // not the world-only value 2
    for (size_t manager = 0; manager < base.manager_teams.size(); ++manager)
        base.manager_records.at(manager * 0xb0c + 0x21c) = 0;
    base.team_morale.at(size_t(kNoTeam)) |= 0x80;

    struct CounterCase { uint16_t source_day; uint16_t stale_a; uint16_t stale_b; };
    const CounterCase cases[] = {
        {1, 0, 2},  // live day one, both caches miss the first-day branch
        {0, 1, 0},  // live day zero, stale cache falsely takes the first-day branch
        {2, 1, 2},  // live day two, stale cache falsely takes the first-day branch
    };
    for (const CounterCase& test : cases) {
        GameState a = base, b = base;
        a.date = a.season_start + uint32_t(test.source_day);
        b.date = a.date;
        a.memory.set16(::bmh::layout::DaysIntoSeason, test.stale_a);
        b.memory.set16(::bmh::layout::DaysIntoSeason, test.stale_b);
        check(save_stream(a) == save_stream(b),
              "different stale bb468 caches encode the same live Native input");

        WatcomRandom rand_a(0x23451), rand_b(0x23451);
        MinuteReport report_a, report_b;
        const bool coach_a = national_team_day(a, rand_a, report_a);
        const bool coach_b = national_team_day(b, rand_b, report_b);
        check(!report_a.unsupported && !report_b.unsupported && coach_a == coach_b,
              "actual national nomination path is supported with identical coach outcome");
        const uint8_t expected_flag = test.source_day == 1 ? 0 : 0x80;
        check((a.team_morale.at(size_t(kNoTeam)) & 0x80) == expected_flag &&
              (b.team_morale.at(size_t(kNoTeam)) & 0x80) == expected_flag,
              "first-day morale handling follows the Source day, including no eligible coach");
        check(save_stream(a) == save_stream(b),
              "actual national nomination has identical full Native output for equivalent live states");
        check(rand_a.seed() == rand_b.seed() && rand_a.calls() == rand_b.calls(),
              "actual national nomination consumes identical Watcom RNG for equivalent live states");
    }

    // Exercise the nested legacy routine re-takeover after a completed opening event
    // (Source day 0, typed date already day 1) and on the quiet-day path (Source
    // day 1). Both use a genuine loaded career and a training day so day_step_rest
    // performs the full coach hand-back/training/re-takeover sequence.
    GameState active = base;
    active.manager_records.at(0x21c) = 1;
    national_coach_take_over(active, int(active.manager_teams.size()), 2);
    check(national_coach_active(active) && active.manager_teams.size() == 1,
          "nested-rest control starts from a genuine Source coach takeover");
    active.manager_records.at(4 * 0xb0c + 0x21c) = 0;  // no eligible coach after hand-back
    uint32_t training_date = active.date + 1;
    while (weekday_code(training_date - 1) != 2 && weekday_code(training_date - 1) != 6)
        ++training_date;
    active.date = training_date;
    active.season_start = training_date - 1;  // typed day 1; event origin is day 0
    const CounterCase nested[] = {
        {0, 1, 0},  // completed event: live typed day 1, Source still sees day 0
        {1, 0, 2},  // quiet day: Source has already advanced to day 1
    };
    for (const CounterCase& test : nested) {
        GameState a = active, b = active;
        a.memory.set16(::bmh::layout::DaysIntoSeason, test.stale_a);
        b.memory.set16(::bmh::layout::DaysIntoSeason, test.stale_b);
        check(save_stream(a) == save_stream(b),
              "nested rest cache variants have identical pre-call Native bytes");
        GameRandom game_a(0x13579), game_b(0x13579);
        WatcomRandom rand_a(0x24680), rand_b(0x24680);
        MinuteReport report_a, report_b;
        check(day_step_rest(a, game_a, rand_a, report_a, test.source_day) &&
              day_step_rest(b, game_b, rand_b, report_b, test.source_day) &&
              !report_a.unsupported && !report_b.unsupported,
              "nested day rest completes with explicit Source-day context");
        const uint8_t expected_flag = test.source_day == 1 ? 0 : 0x80;
        check((a.team_morale.at(size_t(kNoTeam)) & 0x80) == expected_flag &&
              (b.team_morale.at(size_t(kNoTeam)) & 0x80) == expected_flag,
              "nested coach restoration follows completed-event versus quiet-day Source ordering");
        check(save_stream(a) == save_stream(b),
              "nested rest produces identical full Native outputs from equivalent live states");
        check(game_a.seed() == game_b.seed() && game_a.calls() == game_b.calls() &&
              rand_a.seed() == rand_b.seed() && rand_a.calls() == rand_b.calls(),
              "nested rest preserves identical Game and Watcom RNG streams");
    }
}

void verify_matchday_context_day(const Source& source,
                                const std::filesystem::path& saves) {
    GameState base;
    bool found = false;
    for (const char* name : {"2PNWK060.FBM", "NATWK001.FBM", "NATWK008.FBM", "NATWK012.FBM"}) {
        const auto path = saves / name;
        if (!std::filesystem::exists(path)) continue;
        GameState candidate = source.load(path);
        if (national_coach_active(candidate)) national_coach_hand_back(candidate);
        if (candidate.manager_teams.size() >= 2) {
            base = std::move(candidate);
            found = true;
            break;
        }
    }
    check(found, "context-day regression finds a genuine multi-manager Native save");

    // Exercise setup_matchday's production caller with a Source national event.
    // Deliberately keep the typed date at day four while supplying day one in
    // MatchdayContext: legacy routine passes its context day to legacy routine.
    const uint32_t typed_day = 4;
    base.date = base.season_start + typed_day;
    base.memory.set8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4, 0);
    for (size_t manager = 0; manager < base.manager_teams.size(); ++manager)
        base.manager_records.at(manager * 0xb0c + 0x21c) = 0;
    base.team_morale.at(size_t(kNoTeam)) |= 0x80;

    auto run = [&](uint16_t context_day) {
        GameState state = base;
        GameRandom game(0x1379a);
        WatcomRandom rand(0x2468b);
        MinuteReport report;
        MatchdayContext context;
        context.month = month_code(state.date);
        context.weekday = weekday_code(state.date);
        context.season_day = context_day;
        std::vector<uint8_t> refs = source.database.raw_referee_properties;
        context.referees = &refs;
        const auto groups = setup_matchday(state, 0x200, context, game, rand, report);
        check(!report.unsupported && !groups.empty(),
              "actual Source national matchday setup completes with its existing fixture");
        return std::tuple{std::move(state), game, rand};
    };
    auto [source_day_one, game_one, rand_one] = run(1);
    auto [source_day_two, game_two, rand_two] = run(2);
    check(source_day_one.season_day() == typed_day && source_day_two.season_day() == typed_day,
          "context-day controls preserve the deliberately different typed day");
    check((source_day_one.team_morale.at(size_t(kNoTeam)) & 0x80) == 0 &&
          (source_day_two.team_morale.at(size_t(kNoTeam)) & 0x80) == 0x80,
          "setup_matchday takeover uses MatchdayContext.season_day, not the typed date");
    check(game_one.seed() == game_two.seed() && game_one.calls() == game_two.calls() &&
          rand_one.seed() == rand_two.seed() && rand_one.calls() == rand_two.calls(),
          "context-day takeover control preserves identical RNG streams");
}
}

int main(int argc, char** argv) {
    try {
        check(argc == 3, "usage: national_coach_desk_tests ORIGINAL_DATA REGRESSION_SAVEGAMES_DIR");
        if (!*argv[1]) {
            std::cout << "national coach desk tests skipped (Original data directory not configured)\n";
            return 0;
        }
        const Source source(argv[1]);
        const auto saves = std::filesystem::path(argv[2]);
        int cases = 0;
        for (const char* name : {"NATWK001.FBM", "NATWK008.FBM", "NATWK012.FBM", "2PNWK060.FBM"}) {
            const auto path = saves / name;
            if (!std::filesystem::exists(path)) continue;
            verify_case(source, path, cases == 0);
            ++cases;
        }
        check(cases != 0, "regression directory contains a supported Native national no-fixture save");
        verify_owned_offers(source, saves);
        verify_parked_buyer(source,saves);
        verify_parked_seller_loan(source,saves);
        verify_national_day_source_counter(source, saves);
        verify_matchday_context_day(source, saves);
        std::cout << "national coach desk tests passed across " << cases
                  << " genuine Native inputs; raw owners, parked buyer4, Source accounts/boards/RNG and guarded contexts passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "national_coach_desk_tests: " << e.what() << '\n';
        return 1;
    }
}
