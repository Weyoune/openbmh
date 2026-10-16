// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/desk_actions.hpp"
#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/month.hpp"
#include "game/national.hpp"
#include "game/pending_offer.hpp"
#include "game/prediction.hpp"
#include "game/save.hpp"
#include "game/season.hpp"

// Private QA composition of format field -> IncomingTransferScreen/ProjectTransferOfferList -> Void2 ->
// SettleHumanBuyerSale -> PendingOfferAnswer -> 42707. No production pending adapter is called.
// Contract: ordinary two-human-club/count24 SALE, validated Native control,
// explicit EXE gate and scripted answers. Caller supplies independent copies
// of input/state/W/G and compares complete snapshots, not just selected fields.
// This shares only previously reviewed small primitives. It is not an Original
// runtime oracle for those primitives or the renderer/input loop.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "game/lineup.hpp"
#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/month.hpp"
#include "game/national.hpp"
#include "game/prediction.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"

namespace human_buyer_source_oracle {
using namespace bmh;
constexpr uint32_t offers = ::bmh::layout::OfferSlots, list_first = ::bmh::layout::EditorList;
constexpr size_t pool = 0xb70, record_size = 0xb0c, board_size = 0x840, row_size = 0x58;
enum class Gate { unset, zero, blocked };
enum class Reply { unset, yes, no };
enum class Choice { unset, bid0, decline };
struct Script {
    int clicked_manager = 0;
    int clicked_row = 0;
    Gate gate = Gate::unset; // MUST be selected from actual EXE observation by test caller.
    std::array<Reply, 4> salary{};
    std::array<Reply, 4> severance{};
    std::array<Choice, 4> choice{};
};
struct Snapshot {
    std::string point;
    int raw = -1, manager = -1;
    uint32_t text_address = 0;
    uint16_t question_flags = 0;
    uint8_t manager_flags = 0;
    std::string text;
    Bytes native;
    uint32_t w_seed = 0;
    int32_t g_seed = 0;
    uint64_t w_calls = 0, g_calls = 0;
    uint8_t active = 0;
    int bdb7e = -1, bdb8c = -1, bdb93 = -1; // -1 means absent, never inferred zero.
};
struct Result {
    int processed = 0, moved = 0, salary_accepted = 0;
    int funds_refused = 0, allocator_blocked = 0, no_room = 0;
    bool sticky_salary_no = false;
    std::vector<Snapshot> snapshots;
};
inline void require(bool ok, const char* why) {
    if (!ok) throw std::runtime_error(std::string("Source human-buyer oracle: ") + why);
}
inline uint32_t read32(const std::vector<uint8_t>& bytes, size_t at) {
    uint32_t v = 0;
    for (size_t k = 0; k < 4; ++k) v |= uint32_t(bytes.at(at + k)) << (k * 8);
    return v;
}
inline void write32(std::vector<uint8_t>& bytes, size_t at, uint32_t v) {
    for (size_t k = 0; k < 4; ++k) bytes.at(at + k) = uint8_t(v >> (k * 8));
}
inline void optional_byte(GameState& g, uint32_t at, uint8_t value) {
    if (g.memory.has(at)) g.memory.set8(at, value);
}
inline size_t find_player(const GameState& g, int team, int player) {
    const auto range = squad_range(team);
    for (size_t at = range.first; at < range.first + range.count; ++at)
        if ((g.squad_slots.at(at) & 0xfff) == player) return at;
    throw std::runtime_error("Source human-buyer oracle: missing seller player/error32");
}
inline void snapshot(Result& result, const GameState& g, const GameRandom& game,
                     const WatcomRandom& rand, std::string point, int raw = -1,
                     int manager = -1, uint32_t address = 0, std::string text = {},
                     uint16_t flags = 0, uint8_t manager_flags = 0) {
    Snapshot out;
    out.point = std::move(point); out.raw = raw; out.manager = manager;
    out.text_address = address; out.text = std::move(text);
    out.question_flags = flags; out.manager_flags = manager_flags;
    out.w_seed = rand.seed(); out.g_seed = game.seed();
    out.w_calls = rand.calls(); out.g_calls = game.calls();
    out.active = g.memory.u8(::bmh::layout::ActiveManager);
    const auto transient = [&](uint32_t at) { return g.memory.has(at) ? int(g.memory.u8(at)) : -1; };
    out.bdb7e = transient(::bmh::layout::FinanceStatisticsEnabled); out.bdb8c = transient(::bmh::layout::DeskActions_EditorFail_RuntimeField2); out.bdb93 = transient(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField);
    out.native = save_stream(g); // no make_savegame, therefore no encryption W draws.
    require(rand.seed() == out.w_seed && rand.calls() == out.w_calls &&
            game.seed() == out.g_seed && game.calls() == out.g_calls, "snapshot consumed RNG");
    result.snapshots.push_back(std::move(out));
}
inline void notice(Result& result, const GameState& g, const GameRandom& game,
                   const WatcomRandom& rand, int raw, int manager, uint32_t at,
                   bool timed, std::string composed = {}) {
    snapshot(result, g, game, rand, timed ? "timed_notice" : "ack", raw, manager,
             at, composed.empty() ? message_text(g, at) : std::move(composed), 0,
             uint8_t(manager) | uint8_t(timed ? 0x40 : 0x80));
}
inline void history(GameState& g, int manager, int other, int player, uint32_t fee, bool buyer) {
    const size_t base = size_t(manager) * record_size;
    // format field shifts exactly 0x74 bytes: 29 old entries into rows1..29.
    for (size_t n = 29; n > 0; --n) {
        write32(g.manager_records, base + 0x760 + n * 4,
                read32(g.manager_records, base + 0x760 + (n - 1) * 4));
        write32(g.manager_records, base + 0x7d8 + n * 4,
                read32(g.manager_records, base + 0x7d8 + (n - 1) * 4));
    }
    write32(g.manager_records, base + 0x760,
            g.date * 4u + uint32_t(buyer) + (fee / 10000u) * 0x80000u);
    write32(g.manager_records, base + 0x7d8,
            uint32_t(g.player_name_offsets.at(size_t(player))) +
            uint32_t(g.club_ids.at(size_t(other))) * 0x10000u);
    auto& count = g.manager_records.at(base + 0x21d);
    if (count < 25) ++count;
}
inline void remove_seller(GameState& g, int seller, int manager, int player) {
    // Independent composition of ReleasePlayer, not production remove_from_seller.
    const auto range = squad_range(seller);
    const size_t found = find_player(g, seller, player);
    for (size_t at = pool; at < 0xb9c; ++at)
        if ((g.squad_slots.at(at) & 0xfff) == player) g.squad_slots.at(at) = 0xfff;
    for (size_t at = found; at + 1 < range.first + range.count; ++at)
        g.squad_slots.at(at) = g.squad_slots.at(at + 1);
    g.squad_slots.at(range.first + range.count - 1) = 0xfff;
    const size_t row = found - range.first;
    if (row != 23) {
        const size_t start = size_t(manager) * board_size + row * row_size;
        std::memmove(g.tactics.data() + start, g.tactics.data() + start + row_size,
                     (23 - row) * row_size);
    }
    // ReleasePlayer embedded call; 42707 calls seller again afterwards.
    auto_lineup(g, seller, 100);
}
inline uint32_t interested(const GameState& g, int seller, int buyer) {
    // format field, with real Native option bytes required rather than defaulted.
    if (g.memory.u8(::bmh::layout::Weekly_InterestedManagers_RuntimeField) != 0) return 0;
    uint32_t mask = 0;
    for (size_t m = 0; m < g.manager_teams.size(); ++m) {
        const int league = g.manager_records.at(m * record_size + 0x210);
        if (team_league(seller) != league && team_league(buyer) != league) continue;
        const int own = int8_t(table_position(g, g.manager_teams.at(m)));
        if (std::abs(own - int16_t(table_position(g, seller))) <= 5 ||
            std::abs(own - int16_t(table_position(g, buyer))) <= 5) mask |= 1u << m;
    }
    return mask;
}
inline uint32_t stack_board_bonus(GameState& g, int buyer_manager, int player,
                                  uint32_t salary, uint32_t list) {
    // ProjectTransferOfferList's local board has +50 salary and +12 seven bidder bonuses. The
    // reviewed primitive indexes GameState boards, so temporarily lend row0,
    // restore that row exactly, and KEEP any native table-prediction side effects.
    const size_t at = size_t(buyer_manager) * board_size;
    std::array<uint8_t, row_size> previous{};
    std::copy_n(g.tactics.begin() + std::ptrdiff_t(at), row_size, previous.begin());
    struct Restore {
        GameState& g; size_t at; const std::array<uint8_t, row_size>& previous;
        ~Restore() { std::copy(previous.begin(), previous.end(), g.tactics.begin() + std::ptrdiff_t(at)); }
    } restore{g, at, previous};
    std::fill_n(g.tactics.begin() + std::ptrdiff_t(at), row_size, uint8_t(0));
    write32(g.tactics, at + 0x50, salary);
    for (size_t n = 0; n < 7; ++n) g.tactics.at(at + 0x12 + n) = g.memory.u8(list + 0x38 + uint32_t(n));
    return expected_bonus(g, buyer_manager, 0xff, 0, player, true);
}
inline Result run(GameState& g, GameRandom& game, WatcomRandom& rand, const Script& script) {
    require(script.gate != Gate::unset, "actual EXE allocator gate must be supplied");
    require(g.manager_teams.size() == 2 && script.clicked_manager >= 0 && script.clicked_manager < 2,
            "requires genuine two-manager ordinary control");
    require(g.memory.u8(::bmh::layout::ActiveManager) == script.clicked_manager, "clicked manager must be active");
    for (int team : g.manager_teams) require(team >= 0 && team < 90 && squad_range(team).count == 24,
                                          "only ordinary24-slot human clubs are supported");
    require(script.clicked_row >= 0 && script.clicked_row < 24 && g.texts, "fixed clicked board/texts");
    const size_t clicked_slot = squad_range(g.manager_teams.at(size_t(script.clicked_manager))).first + size_t(script.clicked_row);
    const size_t clicked_board = size_t(script.clicked_manager) * board_size + size_t(script.clicked_row) * row_size;
    Result result;
    snapshot(result, g, game, rand, "entry");
    for (int raw = 0; raw < 4; ++raw) {
        const uint32_t offer_at = offers + uint32_t(raw) * 4;
        const uint32_t word = g.memory.u32(offer_at);
        const int owner = g.memory.u8(::bmh::layout::ActiveManager);
        if (word == 0xffffffffu || int(word & 31) != owner) continue;
        const int player = int((word >> 10) & 0xfff);
        const int seller = g.manager_teams.at(size_t(owner));
        const uint32_t list = list_first + uint32_t(raw + 40) * row_size;
        const size_t seller_slot = find_player(g, seller, player);
        const size_t seller_board = size_t(owner) * board_size +
                                    (seller_slot - squad_range(seller).first) * row_size;
        const int buyer = int16_t(g.memory.u16(list));
        const int buyer_manager = g.manager_of(buyer);
        require(buyer_manager >= 0 && buyer_manager < 2 && buyer_manager != owner && buyer != seller,
                "distinct human buyer");
        require(g.memory.u16(list + 2) == 0xffff && g.memory.u16(list + 4) == 0xffff &&
                g.memory.u16(list + 6) == 0xffff && (g.memory.u16(list + 8) & 0x7fff) == seller,
                "one genuine control bid and matching seller");
        const uint8_t term = g.memory.u8(list + 0x34);
        require(term <= 127, "loans excluded");
        for (uint32_t n = 0; n < 200; ++n) {
            const uint32_t loan = g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + n * 4);
            require(loan == 0 || int(loan & 0xfff) != player, "error30 boundary excluded");
        }
        bool negotiate = true;
        if (word & 0x80000000u) {
            require(script.salary[size_t(raw)] != Reply::unset, "salary answer must be scripted");
            const uint32_t demanded = g.memory.u32(list + 0x0c);
            const auto name = g.player_name(g.squad_slots.at(clicked_slot) & 0xfff);
            snapshot(result, g, game, rand, "salary_question", raw, owner, ::bmh::layout::Weekly_ProcessPendingBatch_TextPointer,
                     name + message_text(g, ::bmh::layout::Weekly_ProcessPendingBatch_TextPointer) + format_amount(g, int32_t(demanded)) +
                     message_text(g, ::bmh::layout::Weekly_ProcessPendingBatch_TextPointer2), 1, uint8_t(owner));
            if (script.salary[size_t(raw)] == Reply::yes) {
                if (!(word & 0x20u)) {
                    uint32_t fee = 0, reference = 0;
                    market_value(g, int(clicked_slot), 0, 98, fee, reference,
                                 (int(g.tactics.at(clicked_board + 0x0c)) + 6) / 12, 0, rand);
                    require(reference != 0, "salary-JA divide-by-zero excluded");
                    write32(g.tactics, clicked_board + 0x50, demanded);
                    g.tactics.at(clicked_board + 0x0d) = uint8_t((demanded * 100u) / reference);
                }
                ++result.salary_accepted; negotiate = false;
            } else result.sticky_salary_no = true;
        }
        bool settle = negotiate;
        if (negotiate && !result.sticky_salary_no) {
            require(script.choice[size_t(raw)] != Choice::unset, "normal bid answer must be scripted");
            // OpenTransferEditor redraw: reset G to CareerSession_Invalid_RuntimeField2, ONE G92..108, optional market W.
            const int previous_suppression = g.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2) ? int(g.memory.u8(::bmh::layout::DeskActions_EditorFail_RuntimeField2)) : -1;
            optional_byte(g, ::bmh::layout::DeskActions_EditorFail_RuntimeField2, 0);
            game.set_seed(int32_t(g.memory.u32(::bmh::layout::CareerSession_Invalid_RuntimeField2)));
            const int percent = game.range(92, 108);
            uint32_t quote_fee = 0, quote_salary = 0;
            const int years = (int(g.tactics.at(seller_board + 0x0c)) - 1) / 12 + 1;
            market_value(g, int(pool + size_t(raw + 40)), 0, percent,
                         quote_fee, quote_salary, years, 0, rand);
            quote_fee = ((quote_fee + (quote_fee >> 2)) / 10000u) * 10000u;
            quote_salary = (quote_salary / 100u) * 100u;
            (void)quote_fee; (void)quote_salary;
            g.memory.set32(list + 0x10, 0); g.memory.set32(list + 0x20, 0);
            g.memory.set8(list + 0x35, 24);
            if (previous_suppression >= 0) optional_byte(g, ::bmh::layout::DeskActions_EditorFail_RuntimeField2, uint8_t(previous_suppression));
            snapshot(result, g, game, rand, "chooser", raw, owner);
            if (script.choice[size_t(raw)] == Choice::decline) {
                if (previous_suppression <= 0 && !g.transfer_notice_suppressed)
                    notice(result, g, game, rand, raw, owner, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField, false);
                settle = false;
            } else {
                // IncomingTransferScreen/ProjectTransferOfferList: temporary salary inflation; exact buyer fitness,
                // predicted season bonus and its Native side effects; W90..100.
                const uint32_t original_salary = g.memory.u32(list + 0x0c);
                const uint32_t inflated_salary = (original_salary * 10u) >> 3;
                g.memory.set32(list + 0x0c, inflated_salary);
                const uint8_t extra_years = uint8_t((int(term) - 1) / 12);
                const uint32_t fee_base = (g.memory.u32(list + 0x2c) / 10000u) * 12500u;
                const uint32_t salary_base = (((uint32_t(extra_years) * 20u + 100u) *
                                               g.memory.u32(list + 0x30)) / 10000u) * 100u;
                require(fee_base != 0 && salary_base != 0, "425ee denominator zero excluded");
                uint32_t fee_percent = (g.memory.u32(list + 0x1c) * 100u) / fee_base;
                const uint32_t bonus = stack_board_bonus(g, buyer_manager, player, inflated_salary, list);
                uint32_t salary_percent = ((inflated_salary + bonus) * 100u) / salary_base;
                if (fee_percent < uint32_t(rand.range(90, 100))) fee_percent = 0;
                if (salary_percent < g.memory.u8(list + 0x55)) salary_percent = 0;
                (void)fee_percent; // IncomingTransferScreen manual branch tests salary percentage only.
                g.memory.set32(list + 0x0c, original_salary);
                snapshot(result, g, game, rand, "selected_bid_evaluated", raw, owner);
                if (salary_percent == 0) {
                    notice(result, g, game, rand, raw, owner, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3, true);
                    g.memory.set16(list, 0xffff); settle = false;
                }
            }
        }
        uint32_t severance = 0;
        if (settle) {
            // Void2 human seller always draws this W, even without a question.
            const uint32_t months = g.tactics.at(seller_board + 0x0c);
            const uint32_t old_total = read32(g.tactics, seller_board + 0x50) * months;
            const uint32_t new_total = g.memory.u32(list + 0x0c) * months;
            const uint32_t threshold = old_total * uint32_t(rand.range(90, 100));
            if (threshold >= new_total * 100u && threshold - new_total * 100u != 0) {
                severance = ((old_total - new_total) / 1000u) * 1000u;
                if (severance < 1000) severance = 1000;
                snapshot(result, g, game, rand, "severance_question", raw, owner, ::bmh::layout::Weekly_PendingSettle_TextPointer,
                         message_text(g, ::bmh::layout::Weekly_PendingSettle_TextPointer) + format_amount(g, int32_t(severance)) + ".",
                         0x305, uint8_t(owner));
                require(script.severance[size_t(raw)] != Reply::unset, "severance answer must be scripted");
                if (script.severance[size_t(raw)] == Reply::no) {
                    g.memory.set8(::bmh::layout::ActiveManager, uint8_t(owner));
                    notice(result, g, game, rand, raw, owner, ::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField4, true); settle = false;
                }
            }
        }
        if (settle) {
            const uint32_t raw_fee = g.memory.u32(list + 0x1c);
            uint32_t adjusted_fee = raw_fee;
            const size_t buyer_record = size_t(buyer_manager) * record_size;
            const size_t seller_record = size_t(owner) * record_size;
            const uint8_t subsidy = g.manager_records.at(buyer_record + 0x170);
            if (raw_fee <= read32(g.manager_records, buyer_record + 0x314) && subsidy != 0 &&
                (subsidy & 3) == (g.players.at(size_t(player)).w364 & 3)) {
                uint32_t league = g.manager_records.at(buyer_record + 0x210);
                if (league > 2) --league;
                const uint32_t limit = (((uint32_t(subsidy) >> 2) & 7u) * 50000u + 150000u) * (4u - league);
                if (raw_fee < limit) {
                    const uint32_t discount = (((uint32_t(subsidy) >> 5) + 3u) * raw_fee * 10u) / 100u;
                    notice(result, g, game, rand, raw, buyer_manager, ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer, false,
                           message_text(g, ::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer) + format_amount(g, int32_t(discount)) +
                           message_text(g, ::bmh::layout::Weekly_Fail_TextPointer) + format_amount(g, int32_t(raw_fee)) + ").");
                    adjusted_fee -= discount;
                } else notice(result, g, game, rand, raw, buyer_manager, ::bmh::layout::PendingOffer_FormatError_ExecutableField3, false);
                g.manager_records.at(buyer_record + 0x170) = 0;
            }
            optional_byte(g, ::bmh::layout::FinanceStatisticsEnabled, 1);
            const uint8_t active_before = g.memory.u8(::bmh::layout::ActiveManager);
            if (!book_money(g, buyer_manager, int32_t(adjusted_fee), false, true)) {
                g.memory.set8(::bmh::layout::ActiveManager, uint8_t(buyer_manager));
                notice(result, g, game, rand, raw, buyer_manager, ::bmh::layout::Advertising_FormatError_TextPointer18, true);
                optional_byte(g, ::bmh::layout::FinanceStatisticsEnabled, 0); g.memory.set8(::bmh::layout::ActiveManager, active_before);
                ++result.funds_refused; settle = false;
            } else {
                if (g.level != 0) optional_byte(g, ::bmh::layout::FinanceStatisticsEnabled, 0);
                write32(g.manager_records, buyer_record + 0x330,
                        read32(g.manager_records, buyer_record + 0x330) + adjusted_fee);
                optional_byte(g, ::bmh::layout::FinanceStatisticsEnabled, 1);
                book_money(g, owner, int32_t(0u - adjusted_fee), true, true);
                if (g.level != 0) optional_byte(g, ::bmh::layout::FinanceStatisticsEnabled, 0);
                write32(g.manager_records, seller_record + 0x330,
                        read32(g.manager_records, seller_record + 0x330) - adjusted_fee);
                if (severance != 0) {
                    optional_byte(g, ::bmh::layout::FinanceStatisticsEnabled, 1);
                    book_money(g, owner, int32_t(severance), true, true);
                    if (g.level != 0) optional_byte(g, ::bmh::layout::FinanceStatisticsEnabled, 0);
                }
                if (!result.sticky_salary_no) notice(result, g, game, rand, raw, buyer_manager, ::bmh::layout::PendingOffer_FormatError_ExecutableField2, false);
                g.transfer_notice_suppressed = false;
                optional_byte(g, ::bmh::layout::DeskActions_EditorFail_RuntimeField2, 0);
                uint32_t mask = 0x80000000u | (1u << owner) | (1u << buyer_manager);
                if (g.memory.u8(::bmh::layout::Weekly_ExecuteTransfer_RuntimeField) == 0) mask = 0xffffffffu;
                mask |= interested(g, seller, buyer);
                if (mask != 0x80000000u)
                    post_composed(g, int64_t(mask), message_text(g, ::bmh::layout::Month_SearchProfileNews_TextPointer) + g.player_name(player) +
                        message_text(g, ::bmh::layout::Weekly_ExecuteTransfer_TextPointer) + g.club_name(seller) + message_text(g, ::bmh::layout::Weekly_ExecuteTransfer_TextPointer2) +
                        format_amount(g, int32_t(raw_fee)) + message_text(g, ::bmh::layout::ClubPlayerDetail_RenderCard_TextPointer7) +
                        g.club_name(buyer) + message_text(g, ::bmh::layout::Weekly_ExecuteTransfer_TextPointer4));
                snapshot(result, g, game, rand, "accounting_and_news_complete", raw, owner);
                if (script.gate == Gate::blocked) {
                    ++result.allocator_blocked;
                } else {
                    const auto buyer_range = squad_range(buyer);
                    size_t destination = buyer_range.first;
                    // PendingOfferAnswer unconditionally scans 24 FULL words; scope validated above.
                    while (destination < buyer_range.first + 24 && g.squad_slots.at(destination) != 0x0fff) ++destination;
                    if (destination == buyer_range.first + 24) {
                        notice(result, g, game, rand, raw, int(g.memory.u8(::bmh::layout::ActiveManager)), ::bmh::layout::PendingOffer_FormatError_ExecutableField, true);
                        ++result.no_room;
                    } else {
                        const size_t dest_row = destination - buyer_range.first;
                        const size_t dest_board = size_t(buyer_manager) * board_size + dest_row * row_size;
                        g.squad_slots.at(destination) = g.squad_slots.at(pool + size_t(raw + 40));
                        // Exact initializer draws remain observable despite copy-over.
                        new_board_entry(g, buyer_manager, int(dest_row), int(destination), rand);
                        snapshot(result, g, game, rand, "allocator_initialized", raw, buyer_manager);
                        std::copy_n(g.tactics.begin() + std::ptrdiff_t(seller_board), row_size,
                                    g.tactics.begin() + std::ptrdiff_t(dest_board));
                        const uint32_t salary = g.memory.u32(list + 0x0c);
                        write32(g.tactics, dest_board + 0x50, salary);
                        uint32_t reference_fee = 0, reference_salary = 0;
                        market_value(g, int(pool + size_t(raw + 40)), 0, 100,
                                     reference_fee, reference_salary, term & 127, 0, rand);
                        require(reference_salary != 0, "42707 divide-by-zero excluded");
                        for (size_t n = 0; n < 7; ++n)
                            g.tactics.at(dest_board + 0x12 + n) = g.memory.u8(list + 0x38 + uint32_t(n));
                        const uint32_t bonus = expected_bonus(g, buyer_manager, 0xff, int(dest_row), player, true);
                        // EXE VA4292c..42949: signed bonus IDIV, unsigned final DIV.
                        const int32_t bonus_part = int32_t(bonus * 80u) / 100;
                        const uint32_t numerator = (salary + uint32_t(bonus_part)) * 100u;
                        const uint32_t quotient = numerator / reference_salary;
                        g.tactics.at(dest_board + 0x0d) = uint8_t(std::clamp(int32_t(quotient), 0, 255));
                        write32(g.tactics, dest_board + 0x54, raw_fee);
                        g.tactics.at(dest_board + 0x0c) = term;
                        history(g, owner, buyer, player, raw_fee, false);
                        history(g, buyer_manager, seller, player, raw_fee, true);
                        remove_seller(g, seller, owner, player);
                        auto_lineup(g, seller, 100); auto_lineup(g, buyer, 100);
                        ++result.moved;
                    }
                }
                optional_byte(g, ::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField, 1);
            }
        }
        // DispatchClubPlayerPendingOffers consumes owned row/pool for every expected outcome; list bytes stay.
        g.squad_slots.at(pool + size_t(raw + 40)) = 0x0fff;
        g.memory.set32(offer_at, 0xffffffffu);
        ++result.processed;
        snapshot(result, g, game, rand, "owned_cleanup", raw, owner);
    }
    snapshot(result, g, game, rand, "batch_complete");
    return result;
}
} // namespace human_buyer_source_oracle

using namespace bmh;
namespace {
constexpr uint32_t kOffers=::bmh::layout::OfferSlots, kList=::bmh::layout::EditorList+40*0x58;
constexpr size_t kPool=0xb70+40, kRecord=0xb0c, kBoard=0x840, kBoardSlot=0x58;
void check(bool v,const char* what){if(!v)throw std::runtime_error(what);}
uint32_t read32(const std::vector<uint8_t>& b,size_t at){uint32_t v=0;for(int i=0;i<4;++i)v|=uint32_t(b.at(at+size_t(i)))<<(8*i);return v;}
void write32(std::vector<uint8_t>& b,size_t at,uint32_t v){for(int i=0;i<4;++i)b.at(at+size_t(i))=uint8_t(v>>(i*8));}
bool contains(const GameState& g,int team,int player){const auto r=squad_range(team);for(size_t i=r.first;i<r.first+r.count;++i)if((g.squad_slots.at(i)&0x0fff)==player)return true;return false;}
int find_player(const GameState& g,int team){const auto r=squad_range(team);for(size_t i=r.first;i<r.first+r.count;++i){const int p=g.squad_slots.at(i)&0x0fff;if(p!=0x0fff)return p;}return -1;}
int player_row(const GameState& g,int team,int player){const auto r=squad_range(team);for(size_t i=0;i<r.count;++i)if((g.squad_slots.at(r.first+i)&0x0fff)==player)return int(i);return -1;}
struct Control { int seller=0,buyer=0,player=-1,seller_row=-1,buyer_row=-1; uint32_t fee=0,salary=0,term=0; };
Control make_control(GameState& g,int free_row=-1,bool buyer_full=false,bool salary_question=true,
                     uint32_t fee=0,uint32_t salary=0){
 check(g.manager_teams.size()==2,"fixture must be genuine two-manager 2PNWK001");
 Control c;c.seller=g.manager_teams[0];c.buyer=g.manager_teams[1];c.fee=fee;c.salary=salary;
 check(c.seller>=0&&c.seller<138&&c.buyer>=0&&c.buyer<138&&c.seller!=c.buyer,"distinct ordinary clubs");
 check(g.manager_of(c.seller)==0&&g.manager_of(c.buyer)==1,"fixture manager mapping");
 const auto sr=squad_range(c.seller),br=squad_range(c.buyer);
 check(sr.count==24&&br.count==24,"ordinary 24-slot manager squads");
 // Use the actual source-backed listing and bid helpers on the unchanged Native
 // two-manager input. The special-row copy below is a declared TEST ONLY adapter,
 // not an Original QA_INPUT offer.
 WatcomRandom helper_rand(37);MinuteReport helper_report;
 const auto listing=list_manager_player(g,0,5,helper_rand,helper_report);
 check(listing.status==TransferActionStatus::accepted&&!helper_report.unsupported&&listing.player>=0,
       "Source human listing helper creates control seller entry");
 c.player=listing.player;c.seller_row=player_row(g,c.seller,c.player);
 check(c.seller_row>=0,"listed player remains in a concrete seller row");
 const uint32_t ordinary=::bmh::layout::EditorList+uint32_t(listing.list_entry)*0x58;
 const uint32_t fee_base=(g.memory.u32(ordinary+0x2c)/10000)*0x30d4;
 const uint32_t salary_base=((100u*g.memory.u32(ordinary+0x30))/10000)*100;
 TransferBidRequest bid;bid.manager=1;bid.list_entry=listing.list_entry;bid.player=listing.player;
 bid.fee=int32_t(fee_base);bid.annual_salary=int32_t(salary_base*2);bid.contract_years=1;
 for(size_t i=0;i<7;++i)bid.bonuses[i]=uint32_t(i+3);
 const auto submitted=submit_transfer_bid(g,bid,helper_rand);
 check(submitted.status==TransferActionStatus::pending,"Source bid helper forms two-human pending bid");
 int raw_bid=-1;for(int i=0;i<4;++i)if(g.memory.u16(ordinary+uint32_t(i)*2)==c.buyer)raw_bid=i;
 check(raw_bid>=0,"human bidder remains in the real ordinary offer row");
 c.fee=g.memory.u32(ordinary+0x1c+uint32_t(raw_bid)*4);
 c.salary=g.memory.u32(ordinary+0x0c+uint32_t(raw_bid)*4);
 c.term=g.memory.u8(ordinary+0x34+uint32_t(raw_bid));
 constexpr uint32_t special=::bmh::layout::EditorList+40*0x58;
 for(uint32_t i=0;i<0x58;++i)g.memory.set8(special+i,g.memory.u8(ordinary+i));
 for(int i=0;i<4;++i)g.memory.set16(special+uint32_t(i)*2,i==0?uint16_t(c.buyer):0xffff);
 g.squad_slots.at(kPool)=g.squad_slots.at(0xb70+size_t(listing.list_entry));
 g.memory.set32(kOffers,uint32_t(c.player)<<10); // explicit TEST ONLY special-row promotion
 if(fee!=0){c.fee=fee;g.memory.set32(special+0x1c,fee);}
 if(salary!=0){c.salary=salary;g.memory.set32(special+0x0c,salary);}
 if(fee!=0)g.memory.set32(special+0x2c,(fee/12500u)*10000u);
 g.memory.set32(special+0x30,10000);
 g.memory.set8(special+0x55,0);
 const size_t seller_board=size_t(c.seller_row)*kBoardSlot;
 g.tactics.at(seller_board+0x0c)=24;
 write32(g.tactics,seller_board+0x50,52300);
 g.tactics.at(seller_board+0x06)=0x6d; // opaque board byte proves full-row inheritance
 const int buyer_player=find_player(g,c.buyer);check(buyer_player>=0,"buyer needs one real player for Source fitness average");
 c.buyer_row=-1;
 for(size_t i=0;i<br.count;++i){
   const bool free=buyer_full?false:(free_row<0 ? g.squad_slots.at(br.first+i)==0x0fff : int(i)==free_row);
   g.squad_slots.at(br.first+i)=free?0x0fff:uint16_t(buyer_player);
   if(free&&c.buyer_row<0)c.buyer_row=int(i);
 }
 if(buyer_full)c.buyer_row=-1;
 check((buyer_full&&c.buyer_row==-1)||(!buyer_full&&c.buyer_row>=0),"controlled buyer capacity");

 for(int i=1;i<4;++i){g.memory.set32(kOffers+uint32_t(i)*4,0xffffffff);g.squad_slots.at(kPool+size_t(i))=0x0fff;}
 g.memory.set32(kOffers,uint32_t(c.player)<<10 | (salary_question?0x80000000u:0));
 g.squad_slots.at(kPool)=g.squad_slots.at(sr.first+size_t(c.seller_row));
 g.memory.set8(::bmh::layout::ActiveManager,0);
 write32(g.manager_records,kRecord+0x314,10000000);
 write32(g.manager_records,kRecord+0x31c,0);
 g.manager_records.at(kRecord+0x170)=0; // scenario applies subsidy bits explicitly
 return c;
}
PendingOfferHooks hooks_for(PendingOfferAllocatorGate gate,std::vector<uint32_t>& notices,bool loan=false){
 PendingOfferHooks h;h.human_buyer_allocator_gate=gate;
 h.answer=[](const GameState&,const PendingOfferQuestion&){return PendingOfferAnswer::no;};
 h.select_transfer=[loan](const GameState&,const TransferOfferListView& v){
   check(v.valid&&v.offer_count==1&&v.offers[0].present&&v.offers[0].buyer_manager==1&&
         v.offers[0].loan==loan,"human-buyer bid is projected as one supported source offer");
   return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};
 };
 h.notice=[&](const GameState&,const PendingOfferNotice& n){notices.push_back(n.text_address);};
 return h;
}
PendingOfferResult run(GameState& g,const Control& c,PendingOfferHooks& hooks,GameRandom& game,
                       WatcomRandom& rand,MinuteReport& report){
 return process_player_card_pending_offers(g,{c.seller,c.player,c.seller_row,c.seller_row,-1},game,rand,hooks,report);
}
struct ObservedBoundary {
 std::string point,text;int raw=-1,manager=-1;uint32_t address=0;uint16_t question_flags=0;
 uint8_t manager_flags=0;Bytes native;uint32_t w_seed=0;int32_t g_seed=0;
 uint64_t w_calls=0,g_calls=0;uint8_t active=0;int bdb7e=-1,bdb8c=-1,bdb93=-1;
};
void observe_boundaries(PendingOfferHooks& h,std::vector<ObservedBoundary>& out,
                        GameRandom& game,WatcomRandom& rand){
 const auto record=[&](const GameState& g,ObservedBoundary e){
  e.native=save_stream(g);e.w_seed=rand.seed();e.g_seed=game.seed();e.w_calls=rand.calls();e.g_calls=game.calls();
  e.active=g.memory.u8(::bmh::layout::ActiveManager);
  e.bdb7e=g.memory.has(::bmh::layout::FinanceStatisticsEnabled)?int(g.memory.u8(::bmh::layout::FinanceStatisticsEnabled)):-1;
  e.bdb8c=g.memory.has(::bmh::layout::DeskActions_EditorFail_RuntimeField2)?int(g.memory.u8(::bmh::layout::DeskActions_EditorFail_RuntimeField2)):-1;
  e.bdb93=g.memory.has(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField)?int(g.memory.u8(::bmh::layout::Weekly_PendingSettleHumanBuyer_RuntimeField)):-1;
  out.push_back(std::move(e));
 };
 const auto old_answer=h.answer;h.answer=[record,old_answer](const GameState& g,const PendingOfferQuestion& q){
  ObservedBoundary e;e.point=q.kind==PendingOfferQuestionKind::salary_demand?"salary_question":"severance_question";
  e.raw=q.raw_offer_slot;e.manager=q.manager;e.address=e.point=="salary_question"?::bmh::layout::Weekly_ProcessPendingBatch_TextPointer : ::bmh::layout::Weekly_PendingSettle_TextPointer;
  e.question_flags=q.source_flags;e.manager_flags=uint8_t(q.manager);e.text=q.original_message;record(g,std::move(e));
  return old_answer?old_answer(g,q):PendingOfferAnswer::unsupported;
 };
 const auto old_notice=h.notice;h.notice=[record,old_notice](const GameState& g,const PendingOfferNotice& n){
  ObservedBoundary e;e.point=(n.text_address==::bmh::layout::Advertising_FormatError_TextPointer18||n.text_address==::bmh::layout::PendingOffer_FormatError_ExecutableField||n.text_address==::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField2||
       n.text_address==::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField3||n.text_address==::bmh::layout::PendingOffer_PendingOfferNoticeDialog_ExecutableField4)?"timed_notice":"ack";
  e.raw=n.raw_offer_slot;e.manager=n.manager;e.address=n.text_address;e.text=n.original_message;
  e.manager_flags=uint8_t(n.manager)|uint8_t(e.point=="timed_notice"?0x40:0x80);record(g,std::move(e));
  if(old_notice)old_notice(g,n);
 };
 const auto old_choice=h.select_transfer;h.select_transfer=[record,old_choice](const GameState& g,const TransferOfferListView& v){
  ObservedBoundary e;e.point="chooser";e.raw=v.list_entry-40;e.manager=v.seller_manager;record(g,std::move(e));
  return old_choice?old_choice(g,v):PendingOfferTransferChoice{};
 };
}
void compare_boundaries(const std::vector<ObservedBoundary>& observed,
                        const human_buyer_source_oracle::Result& expected){
 std::vector<const human_buyer_source_oracle::Snapshot*> points;
 for(const auto& s:expected.snapshots)if(s.point=="salary_question"||s.point=="severance_question"||
      s.point=="chooser"||s.point=="ack"||s.point=="timed_notice")points.push_back(&s);
 check(observed.size()==points.size(),"all Original-facing answer/chooser/notice callbacks are observed in order");
 for(size_t i=0;i<observed.size();++i){const auto& a=observed[i];const auto& b=*points[i];
  check(a.point==b.point&&a.raw==b.raw&&a.manager==b.manager&&a.address==b.text_address&&
        a.question_flags==b.question_flags&&a.manager_flags==b.manager_flags&&a.text==b.text,
        "callback kind/order/recipient/address/Original text/flags match Source oracle");
  check(a.native==b.native&&a.w_seed==b.w_seed&&a.g_seed==b.g_seed&&
        a.w_calls==b.w_calls&&a.g_calls==b.g_calls&&a.active==b.active&&
        a.bdb7e==b.bdb7e&&a.bdb8c==b.bdb8c&&a.bdb93==b.bdb93,
        "full Native, RNG, active-manager and transient flags match at callback boundary");
 }
}
void expect_history(const GameState& g,int manager,int other,int player,uint32_t fee,bool buyer,bool loan=false){
 const size_t r=size_t(manager)*kRecord;
 const uint32_t expected=g.date*4u+uint32_t(buyer)+uint32_t(loan)*2u+(fee/10000u)*0x80000u;
 check(read32(g.manager_records,r+0x760)==expected,"history direction/date/raw-fee word");
 check(read32(g.manager_records,r+0x7d8)==uint32_t(g.player_name_offsets.at(size_t(player)))+
       uint32_t(g.club_ids.at(size_t(other)))*0x10000u,"history counterpart/player offsets");
}
}

int main(int argc,char** argv){try{
 check(argc==3,"usage: human_buyer_pending_offer_tests DATA 2PNWK001.FBM");
 if(!*argv[1]){std::puts("human buyer pending offer tests: skipped (Original data directory not configured)");return 77;}
 const auto save=load_savegame(argv[2]);check(save.checksum_ok,"genuine Native 2PNWK001 checksum");
 const auto texts=load_textd(std::filesystem::path(argv[1])/"TEXTD.BLK");
 auto original=GameState::from_save(SaveState(save.stream));original.texts=&texts;
 check(original.manager_teams.size()==2&&original.manager_teams[0]==1&&original.manager_teams[1]==54&&
       original.memory.u8(::bmh::layout::ManagerCount)==2,"genuine two-manager native input identity");

 // Independent Source-composition oracle: use the real ordinary listing/bid
 // helpers, then the explicitly declared special-row control transform. The
 // oracle and product receive separate copies of the same Native input and
 // compare full serialized state and both generator streams after a normal
 // human-buyer chooser click.
 {
  auto source=original;const auto c=make_control(source,0,false,false);
  auto product=source;
  human_buyer_source_oracle::Script script;script.clicked_manager=0;script.clicked_row=c.seller_row;
  script.gate=human_buyer_source_oracle::Gate::zero;
  script.choice[0]=human_buyer_source_oracle::Choice::bid0;
  GameRandom source_game(73),product_game(73);WatcomRandom source_w(1),product_w(1);
  const auto oracle=human_buyer_source_oracle::run(source,source_game,source_w,script);
  std::vector<uint32_t> notices;auto h=hooks_for(PendingOfferAllocatorGate::zero,notices);
  std::vector<ObservedBoundary> observed;observe_boundaries(h,observed,product_game,product_w);
  MinuteReport report;const auto result=run(product,c,h,product_game,product_w,report);
  check(!report.unsupported&&result.completed&&result.transferred==oracle.moved&&oracle.moved==1,
        "independent Source composition and product both complete the human sale");
  check(save_stream(product)==save_stream(source),"complete Native state matches independent Source composition");
 check(product_game.seed()==source_game.seed()&&product_game.calls()==source_game.calls()&&
        product_w.seed()==source_w.seed()&&product_w.calls()==source_w.calls(),
        "both RNG streams match independent Source composition at the normal chooser");
  compare_boundaries(observed,oracle);
 }
 // Force wrapped salary arithmetic in both legacy routine and legacy routine while taking
 // the real human-buyer chooser branch. This catches accidental 64-bit
 // widening or unsigned/signed division changes in the EXE's 32-bit math.
 {
  auto source=original;const auto c=make_control(source,0,false,false,2500000,0xf0000000u);
  auto product=source;
  human_buyer_source_oracle::Script script;script.clicked_manager=0;script.clicked_row=c.seller_row;
  script.gate=human_buyer_source_oracle::Gate::zero;script.choice[0]=human_buyer_source_oracle::Choice::bid0;
  script.severance[0]=human_buyer_source_oracle::Reply::yes;
  GameRandom source_game(73),product_game(73);WatcomRandom source_w(1),product_w(1);
  const auto oracle=human_buyer_source_oracle::run(source,source_game,source_w,script);
  std::vector<uint32_t> notices;auto h=hooks_for(PendingOfferAllocatorGate::zero,notices);
  h.answer=[](const GameState&,const PendingOfferQuestion&){return PendingOfferAnswer::yes;};
  std::vector<ObservedBoundary> observed;observe_boundaries(h,observed,product_game,product_w);
  MinuteReport report;const auto result=run(product,c,h,product_game,product_w,report);
  check(!report.unsupported&&result.completed&&result.transferred==oracle.moved&&oracle.moved==1,
        "overflow chooser outcome matches independent Source composition");
  check(save_stream(product)==save_stream(source)&&product_game.seed()==source_game.seed()&&
        product_game.calls()==source_game.calls()&&product_w.seed()==source_w.seed()&&
        product_w.calls()==source_w.calls(),
        "wrapped salary chooser and post-move board ratio match full Native/G/W oracle");
  compare_boundaries(observed,oracle);
 }
 // Successful subsidy settlement uses the adjusted amount for cash/ledger,
 // while board, transfer history and composed news retain the raw fee.
 {
  auto source=original;const auto c=make_control(source,-1,false,true,250000,70000);
  const size_t buyer_record=kRecord;
  const uint8_t pos=uint8_t(source.players.at(size_t(c.player)).w364&3);
  source.manager_records.at(buyer_record+0x170)=uint8_t(0x1c|pos);
  const uint32_t before_cash=read32(source.manager_records,buyer_record+0x314);
  auto product=source;
  human_buyer_source_oracle::Script script;script.clicked_manager=0;script.clicked_row=c.seller_row;
  script.gate=human_buyer_source_oracle::Gate::zero;script.salary[0]=human_buyer_source_oracle::Reply::no;
  script.choice[0]=human_buyer_source_oracle::Choice::bid0;
  GameRandom source_game(73),product_game(73);WatcomRandom source_w(1),product_w(1);
  const auto oracle=human_buyer_source_oracle::run(source,source_game,source_w,script);
  std::vector<uint32_t> notices;auto h=hooks_for(PendingOfferAllocatorGate::zero,notices);
  std::vector<ObservedBoundary> observed;observe_boundaries(h,observed,product_game,product_w);
  MinuteReport report;const auto result=run(product,c,h,product_game,product_w,report);
  check(!report.unsupported&&result.transferred==1&&oracle.moved==1&&
        read32(product.manager_records,buyer_record+0x314)==before_cash-175000&&
        read32(product.manager_records,buyer_record+0x330)==175000,
        "discounted buyer cash and ledger use exact adjusted fee");
  expect_history(product,0,c.buyer,c.player,c.fee,false);
  expect_history(product,1,c.seller,c.player,c.fee,true);
  check(save_stream(product)==save_stream(source)&&product_game.seed()==source_game.seed()&&
        product_game.calls()==source_game.calls()&&product_w.seed()==source_w.seed()&&
        product_w.calls()==source_w.calls(),
        "successful subsidy notices and raw/adjusted accounting match independent oracle");
  compare_boundaries(observed,oracle);
 }

// Ordinary bid path: explicit control row, buyer success ACK, exact Source
 // history direction bits/raw fee, seller-board inheritance and only real moves
 // increment the public transferred counter.
 {
 auto g=original;const auto c=make_control(g,0,false,false);const auto seller_before=g;
  check(seller_before.tactics.at(size_t(c.seller_row)*kBoardSlot+0x06)==0x6d,"seller row marker installed");
  const size_t br=size_t(1)*kRecord,sr=0;
  const int32_t buyer_balance=int32_t(read32(g.manager_records,br+0x314));
  std::vector<uint32_t> notices;auto h=hooks_for(PendingOfferAllocatorGate::zero,notices);
  bool selected=false;h.select_transfer=[&](const GameState&,const TransferOfferListView& v){
   selected=true;check(v.offer_count==1&&v.offers[0].buyer==c.buyer&&v.offers[0].fee==c.fee,
                         "real raw bidder/fee reaches the chooser");
   return PendingOfferTransferChoice{PendingOfferTransferChoiceKind::bid,0};};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto r=run(g,c,h,game,rand,report);
  check(r.completed&&r.processed==1&&r.transferred==1&&r.buyer_funds_refused==0&&
        r.buyer_no_room==0&&!report.unsupported&&selected,"supported human-to-human sale completes");
  check(contains(g,c.buyer,c.player)&&!contains(g,c.seller,c.player),"seller removal and buyer insertion");
  const int buyer_row=player_row(g,c.buyer,c.player);
  check(buyer_row>=0,"transferred player retains a buyer board row after lineup");
  check(g.memory.u32(kOffers)==0xffffffffu&&g.squad_slots.at(kPool)==0x0fff,"owned pending word/pool cleanup");
  check((read32(g.manager_records,br+0x314)==uint32_t(buyer_balance)-c.fee)||g.level==0,
        "buyer pays adjusted fee exactly once");
  check(read32(g.manager_records,br+0x330)==read32(seller_before.manager_records,br+0x330)+c.fee,
        "buyer transfer ledger uses adjusted transaction fee");
  check(read32(g.manager_records,sr+0x330)==read32(seller_before.manager_records,sr+0x330)-c.fee,
        "seller transfer ledger receives same amount in Source direction");
  check(g.tactics.at(size_t(1)*kBoard+size_t(buyer_row)*kBoardSlot+0x06)==0x6d,
        "seller 0x58-byte board is inherited before buyer offer fields");
  check(read32(g.tactics,size_t(1)*kBoard+size_t(buyer_row)*kBoardSlot+0x50)==c.salary&&
        read32(g.tactics,size_t(1)*kBoard+size_t(buyer_row)*kBoardSlot+0x54)==c.fee&&
        g.tactics.at(size_t(1)*kBoard+size_t(buyer_row)*kBoardSlot+0x0c)==c.term,
        "raw salary/fee and sale term override inherited board fields");
  for(int i=0;i<7;++i)check(g.tactics.at(size_t(1)*kBoard+size_t(buyer_row)*kBoardSlot+0x12+size_t(i))==uint8_t(i+3),
        "all seven raw bidder bonus bytes copied");
  expect_history(g,0,c.buyer,c.player,c.fee,false);expect_history(g,1,c.seller,c.player,c.fee,true);
  check(std::find(notices.begin(),notices.end(),::bmh::layout::PendingOffer_FormatError_ExecutableField2)!=notices.end(),"normal accepted-sale ACK is shown to human buyer");
  check(!report.unsupported&&game.calls()>0&&rand.calls()>0,"quote and Source allocator consume their owning RNG streams");
 }

 // Subsidy eligibility/threshold and raw-vs-adjusted fields. Buyer has enough
 // cash for the raw fee but a negative credit makes the discounted fee refuse;
 // subsidy ACK precedes timed funds notice and the refusal retains only that flag
 // clear/ACK, not ledgers, seller credit, news, allocator, histories or move.
 {
  auto g=original;const auto c=make_control(g,-1,false,true,250000,70000);
  const size_t br=kRecord;const uint8_t position=uint8_t(g.players.at(size_t(c.player)).w364&3);
  g.manager_records.at(br+0x170)=uint8_t(0x1c|position); // full tax tier, matching position
  write32(g.manager_records,br+0x314,c.fee);
  write32(g.manager_records,br+0x31c,uint32_t(-200000));
  const auto seller_balance=read32(g.manager_records,0x314);
  const auto before=save_stream(g);const auto buyer_ledger=read32(g.manager_records,br+0x330);
  const auto seller_ledger=read32(g.manager_records,0x330);std::vector<uint32_t> order;
  PendingOfferHooks h;h.human_buyer_allocator_gate=PendingOfferAllocatorGate::zero;
  h.answer=[](const GameState&,const PendingOfferQuestion&){return PendingOfferAnswer::no;};
  h.notice=[&](const GameState& state,const PendingOfferNotice& n){
   order.push_back(n.text_address);
   if(n.text_address==::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer)check(state.manager_records.at(br+0x170)!=0&&
       n.original_message==message_text(state,::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer)+format_amount(state,75000)+message_text(state,::bmh::layout::Weekly_Fail_TextPointer)+
       format_amount(state,int32_t(c.fee))+").","Source subsidy message amounts/order");
   if(n.text_address==::bmh::layout::Advertising_FormatError_TextPointer18)check(state.memory.u8(::bmh::layout::ActiveManager)==1&&
       (!state.memory.has(::bmh::layout::FinanceStatisticsEnabled)||state.memory.u8(::bmh::layout::FinanceStatisticsEnabled)==1),
       "funds refusal notice runs with buyer active and bdb7e set");
  };
  GameRandom game(73);WatcomRandom rand(1);std::vector<ObservedBoundary> observed;
  observe_boundaries(h,observed,game,rand);MinuteReport report;const auto r=run(g,c,h,game,rand,report);
  check(r.completed&&r.buyer_funds_refused==1&&r.transferred==0&&!report.unsupported,
        "expected Source buyer funds refusal still completes row");
  check(order==std::vector<uint32_t>({::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer,::bmh::layout::Advertising_FormatError_TextPointer18}),"subsidy ACK then insufficient-funds timed notice");
  check(g.manager_records.at(br+0x170)==0&&read32(g.manager_records,br+0x330)==buyer_ledger&&
        read32(g.manager_records,0x330)==seller_ledger,"refusal clears eligible subsidy but writes neither transfer ledger");
  check(g.memory.u8(::bmh::layout::ActiveManager)==0&&(!g.memory.has(::bmh::layout::FinanceStatisticsEnabled)||g.memory.u8(::bmh::layout::FinanceStatisticsEnabled)==0),
        "buyer active manager and transient bdb7e restored after refusal notice");
  check(!contains(g,c.buyer,c.player)&&contains(g,c.seller,c.player)&&g.memory.u32(kOffers)==0xffffffffu&&
        g.squad_slots.at(kPool)==0x0fff,"funds refusal preserves squads and completes pending cleanup");
  check(read32(g.manager_records,0x314)==seller_balance,"refusal does not credit seller");
  check(before!=save_stream(g),"qualifying subsidy byte and pending cleanup are retained on expected refusal");
  auto source=original;const auto source_control=make_control(source,-1,false,true,250000,70000);
  source.manager_records.at(br+0x170)=uint8_t(0x1c|(source.players.at(size_t(source_control.player)).w364&3));
  write32(source.manager_records,br+0x314,source_control.fee);write32(source.manager_records,br+0x31c,uint32_t(-200000));
  human_buyer_source_oracle::Script script;script.clicked_manager=0;script.clicked_row=source_control.seller_row;
  script.gate=human_buyer_source_oracle::Gate::zero;script.salary[0]=human_buyer_source_oracle::Reply::no;
  script.choice[0]=human_buyer_source_oracle::Choice::bid0;
  GameRandom sg(73);WatcomRandom sw(1);const auto expected=human_buyer_source_oracle::run(source,sg,sw,script);
  check(expected.funds_refused==1&&save_stream(g)==save_stream(source)&&game.seed()==sg.seed()&&
        game.calls()==sg.calls()&&rand.seed()==sw.seed()&&rand.calls()==sw.calls(),
        "subsidy/refusal notice boundary matches independent full Native and RNG oracle");
  compare_boundaries(observed,expected);
 }

 // Threshold equality is the no-discount branch; a position mismatch bypasses
 // both subsidy notices and preserves the subsidy byte. Both cases still use raw
 // fee for history/news and adjusted fee only for settlement.
 {
  auto g=original;auto c=make_control(g,-1,false,true,2000000,70000);
  const size_t br=kRecord;const uint8_t pos=uint8_t(g.players.at(size_t(c.player)).w364&3);
  g.manager_records.at(br+0x170)=uint8_t(0x1c|pos);
  g.manager_records.at(br+0x210)=0; // explicitly fix Source threshold league at zero
  const uint32_t exact_threshold=(((uint32_t(g.manager_records.at(br+0x170))>>2)&7u)*50000u+150000u)*4u;
  check(exact_threshold==c.fee,"test vector is at strict Source subsidy threshold");
  write32(g.manager_records,br+0x314,exact_threshold+100000u);
  std::vector<uint32_t> notices;auto h=hooks_for(PendingOfferAllocatorGate::zero,notices);
  h.answer=[](const auto&,const auto&){return PendingOfferAnswer::no;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;const auto r=run(g,c,h,game,rand,report);
  check(r.completed&&r.transferred==1&&!report.unsupported,"threshold-equality full-fee sale succeeds");
  check(std::find(notices.begin(),notices.end(),::bmh::layout::PendingOffer_FormatError_ExecutableField3)!=notices.end()&&
        std::find(notices.begin(),notices.end(),::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer)==notices.end(),"threshold branch shows no-discount ACK");
  check(g.manager_records.at(br+0x170)==0,"qualifying above-threshold offer still clears subsidy");
  expect_history(g,0,c.buyer,c.player,c.fee,false);expect_history(g,1,c.seller,c.player,c.fee,true);
 }
 {
  auto g=original;auto c=make_control(g,-1,false,true,1000000,70000);
  const size_t br=kRecord;const uint8_t pos=uint8_t(g.players.at(size_t(c.player)).w364&3);
  g.manager_records.at(br+0x170)=uint8_t(0x1c|((pos+1)&3));write32(g.manager_records,br+0x314,3000000);
  std::vector<uint32_t> notices;auto h=hooks_for(PendingOfferAllocatorGate::zero,notices);
  h.answer=[](const auto&,const auto&){return PendingOfferAnswer::no;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;const auto r=run(g,c,h,game,rand,report);
  check(r.completed&&r.transferred==1&&!report.unsupported&&g.manager_records.at(br+0x170)!=0,
        "position mismatch leaves subsidy eligible for a later transfer");
  check(std::find(notices.begin(),notices.end(),::bmh::layout::PendingOffer_FormatError_ExecutableField3)==notices.end()&&
        std::find(notices.begin(),notices.end(),::bmh::layout::PendingOffer_ConstructionNoticeDialog_TextPointer)==notices.end(),"position mismatch shows no subsidy ACK");
 }

 // Source's no-room and EXE-gate suppression are expected partial successes:
 // settlement/news/cleanup commit, but no allocator RNG, history or movement.
 for(bool blocked:{false,true}){
  auto g=original;const auto c=make_control(g,-1,true,true,2500000,70000);
  const auto buyer_before=read32(g.manager_records,kRecord+0x314);
  const auto seller_ledger_before=read32(g.manager_records,0x330);
  std::vector<uint32_t> notices;auto h=hooks_for(blocked?PendingOfferAllocatorGate::blocked:PendingOfferAllocatorGate::zero,notices);
  h.answer=[](const auto&,const auto&){return PendingOfferAnswer::no;};
  GameRandom game(73);WatcomRandom rand(1);std::vector<ObservedBoundary> observed;
  observe_boundaries(h,observed,game,rand);MinuteReport report;const auto r=run(g,c,h,game,rand,report);
  check(r.completed&&!report.unsupported&&r.transferred==0&&
        (blocked?r.buyer_allocator_blocked==1:r.buyer_no_room==1),"expected no-move partial result is typed");
  check((read32(g.manager_records,kRecord+0x314)==buyer_before-c.fee)||g.level==0,
        "partial result keeps buyer settlement");
  check(!contains(g,c.buyer,c.player)&&contains(g,c.seller,c.player)&&
        read32(g.manager_records,0x330)==seller_ledger_before-c.fee,"partial result keeps seller credit without move");
  check(read32(g.manager_records,0x760)==read32(original.manager_records,0x760)&&
        read32(g.manager_records,kRecord+0x760)==read32(original.manager_records,kRecord+0x760),
        "partial result writes no transfer histories");
  check(g.memory.u32(kOffers)==0xffffffffu&&g.squad_slots.at(kPool)==0x0fff,
        "partial result still reaches legacy routine row cleanup");
  if(blocked)check(std::find(notices.begin(),notices.end(),::bmh::layout::PendingOffer_FormatError_ExecutableField)==notices.end(),"match_events_disabled suppression has no invented no-room notice");
  else check(std::find(notices.begin(),notices.end(),::bmh::layout::PendingOffer_FormatError_ExecutableField)!=notices.end(),"full buyer shows Source no-room timed notice");
  auto source=original;const auto source_control=make_control(source,-1,true,true,2500000,70000);
  human_buyer_source_oracle::Script script;script.clicked_manager=0;script.clicked_row=source_control.seller_row;
  script.gate=blocked?human_buyer_source_oracle::Gate::blocked:human_buyer_source_oracle::Gate::zero;
  script.salary[0]=human_buyer_source_oracle::Reply::no;script.choice[0]=human_buyer_source_oracle::Choice::bid0;
  GameRandom sg(73);WatcomRandom sw(1);const auto expected=human_buyer_source_oracle::run(source,sg,sw,script);
  check((blocked?expected.allocator_blocked==1:expected.no_room==1)&&save_stream(g)==save_stream(source)&&
        game.seed()==sg.seed()&&game.calls()==sg.calls()&&rand.seed()==sw.seed()&&rand.calls()==sw.calls(),
        "blocked/no-room partial result and both RNGs match independent full Source composition");
  compare_boundaries(observed,expected);
 }

 // Missing gate provenance, human-buyer loan, same-club and error30 boundaries
 // fail the staged transaction; the direct API caller must discard state and RNG.
 for(int variant: {0,2,3}){
  auto g=original;auto c=make_control(g,-1,false,true);
  if(variant==1)g.memory.set8(kList+0x34,0x98);
  if(variant==2)g.memory.set16(kList,uint16_t(c.seller)); // actual self-bid, not an AI buyer
  if(variant==3)g.memory.set32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField,uint32_t(c.player)|(uint32_t(c.seller)<<12));
  const auto before=save_stream(g);GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  std::vector<uint32_t> notices;auto h=hooks_for(variant==0?PendingOfferAllocatorGate::unsupported:PendingOfferAllocatorGate::zero,notices);
  const auto r=run(g,c,h,game,rand,report);
  check(!r.completed&&report.unsupported&&save_stream(g)==before&&game.calls()==0&&rand.calls()==0,
        "unsupported human-buyer boundary is mutation-free before the caller discards staged state");
 }

 // A human seller's accepted loan to the other human manager performs the
 // complete Source fee, buyer board, owner snapshot, history and return-ledger
 // transaction, then survives the Native save/load boundary and month expiry.
 {
  auto g=original;const auto c=make_control(g,-1,false,true,2500000,70000);
  g.memory.set8(kList+0x34,0x8c); // Source loan flag with a 12-month term.
  std::array<uint8_t,kBoardSlot> owner_board{};
  const size_t seller_board=size_t(c.seller_row)*kBoardSlot;
  std::copy_n(g.tactics.begin()+std::ptrdiff_t(seller_board),kBoardSlot,owner_board.begin());
  int ledger=-1,snapshot=-1;
  for(int n=0;n<200&&ledger<0;++n)if(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(n)*4)==0)ledger=n;
  for(int n=0;n<12&&snapshot<0;++n)if(g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(n)*2)==0xfff)snapshot=n;
  check(ledger>=0&&snapshot>=0,"complete loan ledger/snapshot capacity fixture");
  const uint8_t prior_count=g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField);
  std::vector<uint32_t> notices;auto h=hooks_for(PendingOfferAllocatorGate::zero,notices,true);
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;
  const auto result=run(g,c,h,game,rand,report);
  check(result.completed&&result.transferred==1&&!report.unsupported&&contains(g,c.buyer,c.player)&&
        !contains(g,c.seller,c.player),"accepted human-to-human loan moves both club rosters");
  check(g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4)==uint32_t(c.player+c.seller*0x1000+c.buyer*0x400000)&&
        g.memory.u8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField+uint32_t(ledger))==12,"accepted loan writes packed return owner/borrower and duration");
  check(g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(snapshot)*2)==uint16_t(c.player)&&
        g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2+uint32_t(snapshot))==0&&g.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==uint8_t(prior_count+1),
        "human owner snapshot index, manager and count are recorded");
  for(size_t n=0;n<kBoardSlot;++n)check(g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField+uint32_t(snapshot)*kBoardSlot+uint32_t(n))==owner_board[n],
        "original human owner board row is preserved before compaction");
  const int borrower_row=player_row(g,c.buyer,c.player);
  check(borrower_row>=0&&g.tactics.at(kBoard+size_t(borrower_row)*kBoardSlot+0x0c)==0xff,
        "human borrower board uses Source permanent-loan marker");
  expect_history(g,0,c.buyer,c.player,c.fee,false,true);
  expect_history(g,1,c.seller,c.player,c.fee,true,true);
  auto loaded=GameState::from_save(SaveState(save_stream(g)));loaded.texts=&texts;
  check(save_stream(loaded)==save_stream(g)&&loaded.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4)==
        g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4),"loan ledger and snapshot round-trip through native save state");

  loaded.memory.set8(::bmh::layout::ClubPlayerDetail_InvalidArgument_RuntimeField+uint32_t(ledger),1);
  const auto expired_ledger=loaded.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4);
  GameRandom ownerless_game(73);WatcomRandom ownerless_rand(1);MinuteReport ownerless_report;
  month_change(loaded,ownerless_rand,ownerless_report,&ownerless_game);
  check(ownerless_report.unsupported&&loaded.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4)==expired_ledger&&
        contains(loaded,c.buyer,c.player),"missing Source loan-return dialog owner aborts before loan commit");
  auto declined=loaded;
  int declined_manager=-1;
  declined.loan_return_screen=[&](GameState&,int manager,const std::string&){declined_manager=manager;return 0;};
  GameRandom declined_game(73);WatcomRandom declined_rand(1);MinuteReport declined_report;
  month_change(declined,declined_rand,declined_report,&declined_game);
  check(!declined_report.unsupported&&declined_manager==declined.manager_of(c.buyer)&&
        contains(declined,c.seller,c.player)&&!contains(declined,c.buyer,c.player)&&
        declined.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4)==0,
        "Source NO declines re-offer but still completes the expiring return");
  auto editor_aborted=loaded;
  editor_aborted.loan_return_screen=[](GameState&,int,const std::string&){return 1;};
  editor_aborted.loan_return_offer_screen=[](GameState&,GameRandom&,WatcomRandom&,int,int,int,
      const HumanBuyerSaleHooks&){return -1;};
  const auto editor_abort_ledger=editor_aborted.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4);
  const auto editor_abort_owner=editor_aborted.squad_slots.at(squad_range(c.seller).first);
  const auto editor_abort_borrower=editor_aborted.squad_slots.at(squad_range(c.buyer).first);
  const auto editor_abort_snapshot=editor_aborted.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(snapshot)*2);
  GameRandom abort_game(73);WatcomRandom abort_rand(1);MinuteReport abort_report;
  month_change(editor_aborted,abort_rand,abort_report,&abort_game);
  check(abort_report.unsupported&&
        editor_aborted.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4)==editor_abort_ledger&&
        editor_aborted.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(snapshot)*2)==editor_abort_snapshot&&
        editor_aborted.squad_slots.at(squad_range(c.seller).first)==editor_abort_owner&&
        editor_aborted.squad_slots.at(squad_range(c.buyer).first)==editor_abort_borrower&&
        contains(editor_aborted,c.buyer,c.player)&&!contains(editor_aborted,c.seller,c.player),
        "legacy routine runtime-owner abort discards the staged loan return; preceding month work remains committed");
  int dialog_manager=-1;std::string dialog_text;
  loaded.memory.set32(::bmh::layout::OfferSlots,0x12345678u); // legacy routine must choose the next free row.
  loaded.squad_slots.at(0xb70+41)=uint16_t(c.player);
  loaded.loan_return_screen=[&](GameState&,int manager,const std::string& question){
    dialog_manager=manager;dialog_text=question;return 1;
  };
  bool actual_return_editor_opened=false;
  loaded.loan_return_offer_screen=[&](GameState& staged,GameRandom& game,WatcomRandom& random,
      int manager,int source_slot,int list_entry,const HumanBuyerSaleHooks&)->int {
    const uint32_t row=::bmh::layout::EditorList+uint32_t(list_entry)*0x58;
    int bid_slot=-1;for(int i=0;i<4;++i)if(staged.memory.u16(row+uint32_t(i)*2)==0xffff){bid_slot=i;break;}
    TransferOfferEditorRequest request{manager,staged.manager_teams.at(size_t(manager)),list_entry,
      source_slot,int(staged.squad_slots.at(size_t(source_slot))&0x0fff),staged.memory.u16(row+8),bid_slot};
    request.loan_return_entry = true;
    const auto editor=open_transfer_offer_editor_source(staged,request,game,random);
    if(!editor.supported)return -1;
    actual_return_editor_opened=true;
    (void)cancel_transfer_offer_editor_source(staged,editor); // User closes legacy routine without submitting.
    return 0;
  };
  GameRandom return_game(73);WatcomRandom return_rand(1);MinuteReport return_report;
  month_change(loaded,return_rand,return_report,&return_game);
  check(!return_report.unsupported&&contains(loaded,c.seller,c.player)&&!contains(loaded,c.buyer,c.player),
        "month expiry returns human-borrowed player to human owner");
  check(dialog_manager==loaded.manager_of(c.buyer)&&dialog_text.ends_with(message_text(loaded,::bmh::layout::Month_ReturnLoans_TextPointer6)),
        "human borrower receives exact format field yes/no return question");
  check(actual_return_editor_opened,"Source YES opens real legacy routine before user declines the re-offer");
  check(loaded.memory.u32(::bmh::layout::OfferSlots+4)==0xffffffffu&&loaded.squad_slots.at(0xb70+41)==0x0fff&&
        loaded.memory.u32(::bmh::layout::OfferSlots)==0x12345678u,
        "Source yes path selects, then clears, its temporary return-offer pool row/marker");
  check(loaded.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(ledger)*4)==0&&
        loaded.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(snapshot)*2)==0xfff&&
        loaded.memory.u8(::bmh::layout::DeskActions_Unsupported_RuntimeField)==prior_count,"expiry clears loan ledger/snapshot ownership and decrements count");
 }

 // Normal manager rows 0, 1 and >=2 exercise first-FULL-word allocation rather
 // than masked-ID or synthetic-row selection. The control board positions are
 // intentionally varied while every other destination is occupied by a valid ID.
 for(int row:{0,1,4}){
  auto g=original;const auto c=make_control(g,row,false,true,2500000,70000);
  std::vector<uint32_t> notices;auto h=hooks_for(PendingOfferAllocatorGate::zero,notices);
  h.answer=[](const auto&,const auto&){return PendingOfferAnswer::no;};
  GameRandom game(73);WatcomRandom rand(1);MinuteReport report;const auto r=run(g,c,h,game,rand,report);
  const int actual_row=player_row(g,c.buyer,c.player);
  check(c.buyer_row==row&&actual_row>=0&&r.completed&&r.transferred==1&&!report.unsupported,
        "one requested first-full-word row is allocated and follows player through lineup");
  check(g.tactics.at(kBoard+size_t(actual_row)*kBoardSlot+0x06)==0x6d,
        "selected row inherits seller board before offer fields and lineup");
 }
 std::puts("human buyer pending offer tests: passed");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"human_buyer_pending_offer_tests: %s\n",e.what());return 1;}}
