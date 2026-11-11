// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Internal header of the career session: the includes and the helper functions that more than one of the
// career_session*.cpp files uses. Included only by those files (the helpers have internal linkage).
#include "formats/native_save_version.hpp"
#include "formats/badge_policy.hpp"
#include "game/calendar.hpp"
#include "formats/executable_layout.hpp"
#include "app/career_session.hpp"
#include "formats/league_visibility.hpp"

#include <stdexcept>
#include <system_error>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <bit>
#include <fstream>
#include <limits>
#include <utility>

#include "formats/binary.hpp"
#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/career.hpp"
#include "game/cup.hpp"
#include "game/driver.hpp"
#include "game/desk.hpp"
#include "game/desk_actions.hpp"
#include "game/match.hpp"
#include "game/messages.hpp"
#include "game/prediction.hpp"
#include "game/schedule.hpp"
#include "game/bank_credit.hpp"
#include "game/matchday_setup.hpp"
#include "game/matchday.hpp"
#include "game/lineup.hpp"
#include "game/month.hpp"
#include "game/national.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include "game/table_forecast.hpp"
#include "game/save.hpp"
#include "game/season.hpp"
#include "game/turn.hpp"
#include "game/week.hpp"
#include "game/weekly.hpp"
#include "ui/desk_state.hpp"
#include "ui/league_table.hpp"
#include "ui/other_games_state.hpp"
#include "ui/manager.hpp"
#include "ui/pending_offer.hpp"
#include "ui/training.hpp"

namespace bmh::app {
namespace {

// Reads one byte of the executable's data at 'address': from the game's memory image when the
// address is mapped there, else from the runtime override map, else from the loaded EXE.
uint8_t source_runtime_byte(const GameState& state,const std::map<uint32_t,uint8_t>& runtime,
                            const LeImage& exe,uint32_t address) {
    if(state.memory.has(address))return state.memory.u8(address);
    const auto found=runtime.find(address);return found==runtime.end()?exe.u8(address):found->second;
}

// Writes one byte to the memory image when the address is mapped there, otherwise into the
// runtime override map (the EXE image itself is never modified).
void source_runtime_set_byte(GameState& state,std::map<uint32_t,uint8_t>& runtime,
                             uint32_t address,uint8_t value) {
    if(state.memory.has(address))state.memory.set8(address,value);
    else runtime[address]=value;
}

// Little-endian 32-bit read built from four source_runtime_byte reads.
uint32_t source_runtime_u32(const GameState& state,const std::map<uint32_t,uint8_t>& runtime,
                             const LeImage& exe,uint32_t address) {
    return uint32_t(source_runtime_byte(state,runtime,exe,address)) |
        uint32_t(source_runtime_byte(state,runtime,exe,address+1))<<8 |
        uint32_t(source_runtime_byte(state,runtime,exe,address+2))<<16 |
        uint32_t(source_runtime_byte(state,runtime,exe,address+3))<<24;
}

// Writes the process scratch bytes (league, group, date pieces, strength sums) that the
// original leaves behind after drawing the pairings page, so later reads see the same values.
// Bytes go to the memory image when mapped there, else to the runtime map.
void pairings_runtime_draw(GameState& g,std::map<uint32_t,uint8_t>& runtime,
    const ui::LeaguePairingsState& page,const LeImage& exe) {
    const auto put=[&](uint32_t at,uint8_t v) {
        if(g.memory.has(at))g.memory.set8(at,v);else runtime[at]=v;
    };
    const uint32_t tip_preference=::bmh::layout::CareerSession_CloseLeagueFamily_ExecutableField+uint32_t(page.identity.manager);
    const bool computer_tip=page.source.owner.computer_tip_active;
    if(computer_tip)put(tip_preference,uint8_t(g.memory.u8(tip_preference)|0x10));
    // Actual5147b entry/input globals; these are process bytes, not invented FBM blocks.
    put(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_RuntimeField2,uint8_t(page.identity.league));put(::bmh::layout::CareerSession_AdvertisingSessionUnchanged_ExecutableField,page.source.owner.league_group);
    put(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField,0);put(::bmh::layout::CareerSession_BeginFriendlyOpponentTable_ExecutableField2,0);
    const auto date=[&](uint32_t counter) {
        if(counter==0xffff)return; //21258's Wettbewerb-gesperrt branch changes no date scratch.
        const auto d=ui::desk_date(counter,exe);
        int cycle=3;uint32_t n=counter;
        while(n>uint32_t(364+(cycle==4))) {if(cycle==4){cycle=0;--n;}n-=365;++cycle;}
        put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField,uint8_t(28+(cycle==4)));
        put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField4,uint8_t(d.day));put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField5,uint8_t(d.month));put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField6,uint8_t(d.weekday));
        put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField2,uint8_t(d.year));put(::bmh::layout::CareerSession_PairingsRuntimeDraw_ExecutableField3,uint8_t(d.year>>8));
    };
    const bool selected_filter=page.source.owner.filter_active&&page.source.owner.filter_team>=0;
    if(!selected_filter&&!page.source.owner.postponed_active)
        date(page.source.match_date); //333f3 is absent in selected Mode4 and Mode8.
    for(const auto& row:page.source.rows) {
        if(row.scheduled_date)date(*row.scheduled_date);
        else if(row.postponed_date)date(*row.postponed_date);
        const uint8_t displayed=row.computer_tip_result.value_or(row.raw_result);
        // In local28's referee phase, exact CC rows skip both 68164 calls.
        // Preserve prior scratch when every row takes that branch; a mixed
        // page leaves the last ordinary away-row value, in Source draw order.
        if(page.source.owner.active_phase&&displayed==0xcc)continue;
        //68164's final17295 call on the away team leaves Source CareerSession_FormatError_ExecutableField3.
        const auto strength=team_average(g,row.away.runtime_team,0x1ff);
        put(::bmh::layout::CareerSession_FormatError_ExecutableField3,uint8_t(strength.best_half_sum));put(::bmh::layout::CareerSession_FormatError_ExecutableField4,uint8_t(strength.best_half_sum>>8));
    }
    // Move clears its one-redraw local38 bit after the forecast has
    // been projected. The returned page retains the pressed Original button.
    if(computer_tip)put(tip_preference,uint8_t(g.memory.u8(tip_preference)&0xef));
}

// Compares the runtime-only fields of two states (callbacks by target type); used to check the
// strengths view left the game unchanged.
bool same_strengths_runtime_state(const GameState& a,const GameState& b) {
    const auto same_callback=[](const auto& lhs,const auto& rhs) {
        return bool(lhs)==bool(rhs)&&(!lhs||lhs.target_type()==rhs.target_type());
    };
    return a.indoor==b.indoor&&a.currency_suffix==b.currency_suffix&&a.level==b.level&&
        a.chance_rain==b.chance_rain&&a.chance_temperature==b.chance_temperature&&
        a.last_shooter==b.last_shooter&&a.last_pick_quality==b.last_pick_quality&&
        a.live_scene==b.live_scene&&a.reality==b.reality&&a.live_matches==b.live_matches&&a.list_toggle==b.list_toggle&&
        a.lineups_frozen==b.lineups_frozen&&a.texts==b.texts&&a.date==b.date&&
        a.season_start==b.season_start&&a.development_balance==b.development_balance&&
        a.trend_rotation==b.trend_rotation&&a.season==b.season&&a.transfer_notice_suppressed==b.transfer_notice_suppressed&&
        a.offer_answer==b.offer_answer&&a.pools_open==b.pools_open&&a.autosave_months==b.autosave_months&&
        a.autosave_month==b.autosave_month&&same_callback(a.contract_screen,b.contract_screen)&&
        same_callback(a.loan_return_screen,b.loan_return_screen)&&
        same_callback(a.loan_return_offer_screen,b.loan_return_offer_screen)&&
        same_callback(a.transfer_decision,b.transfer_decision)&&
        same_callback(a.transfer_offer_selector,b.transfer_offer_selector)&&
        same_callback(a.transfer_decision_observer,b.transfer_decision_observer);
}

// Reinterprets the low byte of 'value' as a signed byte.
int8_t source_byte(int value) { return static_cast<int8_t>(uint8_t(value)); }

// True when a table request carries no stray fields (no target child, team, row or dialog reply;
// league id only if the selector variant is allowed) and the expected control index.
bool canonical_table_envelope(const ui::LeagueFamilyRequest& request,int control_index=-1,
                              bool allow_selector_league=false) {
    return request.target_child==0&&request.runtime_team==-1&&request.expected_club_identity==-1&&
        request.table_row_index==-1&&(allow_selector_league||request.league_id==-1)&&
        request.control_index==control_index&&
        !request.table_dialog_reply;
}

}  // namespace

}  // namespace bmh::app
