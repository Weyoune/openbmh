// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/other_games_state.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

#include "formats/binary.hpp"
#include "game/cup.hpp"
#include "game/matchday_setup.hpp"
#include "ui/desk_state.hpp"
#include "game/messages.hpp"
#include "game/season.hpp"

namespace bmh::ui {
namespace {

[[noreturn]] void invalid(const char* why) { throw FormatError(std::string("other games: ") + why); }

uint8_t source_byte(const GameState& game, const std::map<uint32_t,uint8_t>& runtime,
                    const LeImage& exe, uint32_t address) {
    if (game.memory.has(address)) return game.memory.u8(address);
    const auto it = runtime.find(address);
    return it == runtime.end() ? exe.u8(address) : it->second;
}

uint32_t source_u32(const GameState& game, const std::map<uint32_t,uint8_t>& runtime,
                    const LeImage& exe, uint32_t address) {
    return uint32_t(source_byte(game,runtime,exe,address)) |
        uint32_t(source_byte(game,runtime,exe,address+1)) << 8 |
        uint32_t(source_byte(game,runtime,exe,address+2)) << 16 |
        uint32_t(source_byte(game,runtime,exe,address+3)) << 24;
}

uint16_t source_u16(const GameState& game, const std::map<uint32_t,uint8_t>& runtime,
                    const LeImage& exe, uint32_t address) {
    return uint16_t(source_byte(game,runtime,exe,address)) |
        uint16_t(source_byte(game,runtime,exe,address+1)) << 8;
}

std::string text_pointer(const TextDb& texts, uint32_t address) {
    if (address < ::bmh::layout::TextPointerTable || (address - ::bmh::layout::TextPointerTable) % 4 != 0) return {};
    const size_t index = (address - ::bmh::layout::TextPointerTable) / 4;
    return index < texts.strings.size() ? texts.strings[index] : std::string{};
}

std::string original_literal(const LeImage& exe, uint32_t address);

void source_zero_padding(std::string& text) {
    std::replace(text.begin(),text.end(),'%','0');
}

uint32_t source_round_day(const GameState& game, uint32_t entry_address, int date_index) {
    if (entry_address < kCupTable || (entry_address-kCupTable)%kCupEntrySize != 0 ||
        !game.memory.has(entry_address+0x1b) || date_index < 0 || date_index >= 366)
        invalid("unsupported Source round-date descriptor");
    const int target=cup_entry_index(entry_address);
    int first=target;
    for(int steps=0;steps<kCupEntries;++steps) {
        const int previous=int8_t(game.memory.u8(cup_entry_address(first)+0x15));
        if(previous<0) break;
        if(previous>=kCupEntries) invalid("malformed Source round-date previous link");
        first=previous;
        if(steps+1==kCupEntries) invalid("cyclic Source round-date previous links");
    }
    const int first_competition=int16_t(game.memory.u16(cup_entry_address(first)+0x18));
    if(first_competition<0||first_competition+5>=32)
        invalid("unsupported Source round-date competition mask");
    const uint32_t mask=uint32_t(1)<<(first_competition+5);

    const uint32_t target_address=cup_entry_address(target);
    const int target_previous=int8_t(game.memory.u8(target_address+0x15));
    if(target_previous>=kCupEntries)
        invalid("malformed Source round-date target predecessor");
    const bool target_indoor_special_flags=game.memory.u8(target_address+0x14)==0;
    int consumed=0;
    int stage=first;
    bool target_stage=false;
    for(int stages=0;stages<kCupEntries;++stages) {
        int cursor=stage;
        bool reached_target=cursor==target;
        for(int groups=0;!reached_target&&groups<kCupEntries;++groups) {
            if(int8_t(game.memory.u8(cup_entry_address(cursor)+0x17))==0) break;
            if(cursor+1>=kCupEntries) invalid("malformed Source parallel-group link");
            cursor++;
            reached_target=cursor==target;
            if(groups+1==kCupEntries) invalid("cyclic Source parallel-group links");
        }
        const uint32_t cursor_address=cup_entry_address(cursor);
        const int source_competition=int16_t(game.memory.u16(cursor_address+0x18));
        if(reached_target) {
            target_stage=true;
            if(source_competition==11) date_index=0;
            else {
                const uint8_t flags=game.memory.u8(cursor_address+0x14);
                const int legs=(flags&5)==5?2:1;
                if(date_index>=int(game.memory.u8(cursor_address+0x1b))*legs)
                    return (game.season_start+0xffffu)&0xffff;
            }
            consumed+=date_index;
            break;
        }

        if(source_competition==11) {
            if(cursor==target|| (target_previous==cursor&&target_indoor_special_flags)) {
                // Source's indoor special consumes only the first matching calendar day.
                date_index=0;
                target_stage=true;
                break;
            }
            // LeaguePairingsPlayedSource sets this stage's remaining date count to zero for other targets.
            ++consumed; // The Source loop finds the stage's first calendar occurrence before zeroing it.
        } else {
            const uint8_t flags=game.memory.u8(cursor_address+0x14);
            const int legs=(flags&5)==5?2:1;
            consumed+=int(game.memory.u8(cursor_address+0x1b))*legs;
        }
        const int next=int8_t(game.memory.u8(cursor_address+0x16));
        if(next<0) break;
        if(next>=kCupEntries) invalid("malformed Source round-date next link");
        stage=next;
        if(stages+1==kCupEntries) invalid("cyclic Source round-date next links");
    }
    if(!target_stage) invalid("Source round-date target is not in its linked stage chain");
    int matching_day=0;
    for(int day=0;day<366;++day) {
        if(!game.memory.has(::bmh::layout::CompetitionCalendar+uint32_t(day)*2+1))
            invalid("truncated Source calendar for round date");
        const uint16_t calendar=game.memory.u16(::bmh::layout::CompetitionCalendar+uint32_t(day)*2);
        if(!(day_competitions(game,calendar)&mask)) continue;
        if(matching_day++==consumed) return (game.season_start+uint32_t(day))&0xffff;
    }
    return (game.season_start+0xffffu)&0xffff;
}

std::string source_date_heading(const GameState& game, const TextDb& texts, const LeImage& exe,
                                uint32_t source_day) {
    if(source_day==0xffff) return original_literal(exe,::bmh::layout::Fltd_Layout_TextPointer5);
    const DeskDate date = desk_date(source_day,exe);
    std::string out = text_pointer(texts,::bmh::layout::LeaguePairings_DateString_TextPointer2 + uint32_t(date.weekday) * 4) +
        original_literal(exe,::bmh::layout::LeaguePairings_DateString_TextPointer) + format_number(game,date.day,2) +
        original_literal(exe,::bmh::layout::OtherGamesState_ResultText_ExecutableField) + format_number(game,date.month,2) +
        original_literal(exe,::bmh::layout::OtherGamesState_ResultText_ExecutableField) + format_number(game,date.year,0x44);
    source_zero_padding(out);
    return out;
}

std::string original_literal(const LeImage& exe, uint32_t address) {
    if (!exe.contains(address)) return {};
    return exe.cstring(address);
}

int source_league(const LeImage& exe, int team) {
    if (team > 0x164) return 0;
    if (team < 0) invalid("negative team for Source league lookup");
    int cumulative = 0;
    for (int league = 0; league < 9; ++league) {
        cumulative += exe.u8(::bmh::layout::LeagueSizes + uint32_t(league));
        if (team < cumulative) return league;
    }
    return 9;
}

uint8_t source_country(const LeImage& exe, int team) {
    if (team == kNoTeam) return 0;
    if (team < 0) invalid("negative team for Source country lookup");
    int remaining = team;
    const int domestic_cutoff = exe.u8(::bmh::layout::LeaguePairings_LeaguePairingsConstants_RuntimeField);
    for (int league = 0; league < 9; ++league) {
        remaining -= exe.u8(::bmh::layout::LeagueSizes + uint32_t(league));
        if (remaining < 0) {
            if (league >= domestic_cutoff) return uint8_t(league - domestic_cutoff + 1);
            return 0;
        }
    }
    const int remainder = remaining - int16_t(exe.u16(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField6));
    if (remainder >= 0) return uint8_t(remainder / 7 - domestic_cutoff + 10);
    return 0;
}

uint8_t source_name_color(const GameState& game, const std::map<uint32_t,uint8_t>& runtime,
                          const LeImage& exe, int team, uint8_t fallback) {
    const int count = int(game.manager_teams.size());
    const int active = source_byte(game,runtime,exe,::bmh::layout::ActiveManager);
    const int current_manager = source_byte(game,runtime,exe,::bmh::layout::DeskPosition);
    for (int slot = 0; slot < count && slot < 4; ++slot) {
        const size_t record = size_t(slot) * 0xb0c;
        if (record + 0xa8 < game.manager_records.size() && game.manager_records[record + 0xa8] != 0 &&
            game.manager_teams[size_t(slot)] == team)
            return slot == active && current_manager < count ? 0x7a : 0x75;
    }
    if (source_u16(game,runtime,exe,::bmh::layout::AlltimeTable_TeamColor_RuntimeField) == uint16_t(team)) return 0xf7;
    return fallback;
}

std::string phase_annotation(const TextDb& texts, uint16_t result) {
    if (int16_t(result) >> 10 == 0) return {};
    std::string suffix = texts.strings.size() > 160 ? texts.strings[160] : std::string{};
    if (suffix.size() > 2) {
        const int8_t high = int8_t(result >> 8);
        suffix[2] = char((int(high >> 2) - 1) * -0x11 + 'V');
    }
    return suffix;
}

std::string result_text(const GameState& game, const TextDb& texts, const LeImage& exe,
                        uint16_t value, bool scheduled, uint32_t scheduled_day = 0) {
    if (scheduled) {
        if (scheduled_day == 0xffff) return original_literal(exe,::bmh::layout::Fltd_Layout_TextPointer5); // "Wettbewerb gesperrt"
        const DeskDate date = desk_date(scheduled_day,exe);
        std::string out = format_number(game,date.day,2) + original_literal(exe,::bmh::layout::OtherGamesState_ResultText_ExecutableField) +
            format_number(game,date.month,2) + original_literal(exe,::bmh::layout::OtherGamesState_ResultText_ExecutableField) +
            format_number(game,date.year,0x44);
        source_zero_padding(out);
        return out;
    }
    if (value == 0xcccc) return {};
    return format_number(game,(value >> 5) & 0x1f,1) + original_literal(exe,::bmh::layout::OtherGamesState_FormatNumber_ExecutableField) +
        format_number(game,value & 0x1f,1) + phase_annotation(texts,value);
}

std::string foreign_detail(const TextDb& texts, const LeImage& exe, int strength) {
    const int tier = (strength >> 5) & 7;
    std::string out = texts.strings.size() > size_t(500 + tier)
        ? texts.strings[size_t(500 + tier)] : std::string{};
    out += original_literal(exe,::bmh::layout::CupDraw_FormatError_TextPointer);
    for (int bit = 3; bit >= 1; --bit)
        out += original_literal(exe, (strength & (1 << bit)) ? ::bmh::layout::OtherGamesState_ForeignDetail_ExecutableField : ::bmh::layout::CupDraw_FormatError_ExecutableField3);
    out += original_literal(exe,::bmh::layout::CupDraw_FormatError_ExecutableField);
    return out;
}

std::string squad_detail(const GameState& game, const TextDb& texts, const LeImage& exe,
                        const OtherGamesClubMetadata& metadata) {
    if (!metadata.has_squad_values) return {};
    std::string out = original_literal(exe,::bmh::layout::CupDraw_FormatError_ExecutableField2);
    const std::array<uint32_t,3> labels{::bmh::layout::CupDraw_FormatError_TextPointer5,::bmh::layout::LeaguePairings_FormatError_TextPointer8,::bmh::layout::LeaguePairings_FormatError_TextPointer9};
    for (size_t i = 0; i < labels.size(); ++i) {
        out += format_number(game,metadata.averages[i],2) + text_pointer(texts,labels[i]);
    }
    out += format_number(game,metadata.averages[3],2) + original_literal(exe,::bmh::layout::CupDraw_FormatError_TextPointer2);
    out += format_number(game,metadata.averages[4],2) + original_literal(exe,::bmh::layout::CupDraw_FormatError_TextPointer3);
    return out;
}

std::string score_line(const GameState& game, const TextDb& texts, const LeImage& exe,
                       uint16_t value, bool second_leg = false) {
    if (value == 0xcccc && !second_leg) return {};
    const int first = second_leg ? (value & 0x1f) : ((value >> 5) & 0x1f);
    const int second = second_leg ? ((value >> 5) & 0x1f) : (value & 0x1f);
    std::string out = format_number(game,first,1) + original_literal(exe,::bmh::layout::OtherGamesState_FormatNumber_ExecutableField) +
        format_number(game,second,1) + phase_annotation(texts,value);
    if (second_leg && out.size() < 6 && texts.strings.size() > 936) out += texts.strings[936];
    return out;
}

} // namespace

// Kept out of the public UI header; the focused adapter test exercises LeaguePairingsPlayedSource's
// calendar traversal independently from fixture-table formatting.
uint32_t other_games_source_round_day_for_test(const GameState& game, uint32_t entry_address,
                                               int date_index) {
    return source_round_day(game,entry_address,date_index);
}

OtherGamesState other_games_state_from_game(
    const GameState& game, const OtherGamesIdentity& identity, const TextDb& texts,
    const LeImage& exe, int competition, int linked_page, int round,
    OtherGamesDisplayMode mode, int participant_offset, bool source_new_selection,
    const Image& underlay, const std::map<uint32_t,uint8_t>& runtime) {
    if (identity.manager_slot < 0 || identity.manager_slot > 3 || !identity.scope ||
        identity.runtime_team < 0 || identity.club_identity < 0)
        invalid("incomplete modal identity");
    OtherGamesState state;
    state.identity = identity;
    state.underlay = underlay;
    state.decorated_background = source_byte(game,runtime,exe,::bmh::layout::PlainBackground) == 0;
    state.original_panel = source_byte(game,runtime,exe,::bmh::layout::SolidPanelBackground) == 0;
    state.gradient_text = source_byte(game,runtime,exe,::bmh::layout::TextGradientDisabled) == 0 &&
        source_byte(game,runtime,exe,::bmh::layout::TextGradientMode) == 2;
    state.cached_backdrop = source_u32(game,runtime,exe,::bmh::layout::CareerSession_ApplyBalanceSourceStyle_ExecutableField) != 0;

    state.projection = other_games_from_game(game,texts,competition,linked_page,round,mode,
                                              participant_offset,source_new_selection);
    state.source_participant_offset = participant_offset;
    for (const auto& selector : state.projection.selectors) {
        if (selector.slot < 0 || selector.slot >= int(state.selector_labels.size()))
            invalid("projection returned an invalid selector slot");
        if (selector.active) state.selector_labels[size_t(selector.slot)] = selector.title;
    }
    if (state.projection.source_participant_count > 16) {
        const std::string suffix = original_literal(exe,::bmh::layout::OtherGamesState_OtherGamesStateFromGame_ExecutableField) +
            format_number(game,state.source_participant_offset / 16 + 1,1) +
            original_literal(exe,::bmh::layout::OtherGamesState_OtherGamesStateFromGame_ExecutableField2);
        int last_match = -1;
        for (const auto& selector : state.projection.selectors)
            if (selector.active && selector.competition == competition) last_match = selector.slot;
        if (last_match >= 0) state.selector_labels[size_t(last_match)] += suffix;
    }

    state.rows.reserve(state.projection.rows.size());
    for (const auto& source : state.projection.rows) {
        OtherGamesDisplayRow row;
        const std::array<int,2> teams{source.home_team,source.away_team};
        const std::array<const OtherGamesClubMetadata*,2> metadata{&source.home_metadata,&source.away_metadata};
        for (size_t side = 0; side < 2; ++side) {
            const int team = teams[side];
            const uint8_t base_color = state.projection.knockout
                ? (source_country(exe,team) ? 0x67 : 0x5f) : 0x6a;
            const uint8_t winner_color = state.projection.knockout && source.source_winner == int(side)
                ? 0x27 : base_color;
            row.name_colors[side] = source_name_color(game,runtime,exe,team,winner_color);
            row.league_tiers[side] = uint8_t(source_league(exe,team));
            row.countries[side] = source_country(exe,team);
            row.details[side] = metadata[side]->has_squad_values
                ? squad_detail(game,texts,exe,*metadata[side])
                : foreign_detail(texts,exe,metadata[side]->foreign_strength);
        }
        row.primary_result = source.kind == OtherGamesRowKind::scheduled_competition
            ? result_text(game,texts,exe,source.source_result,true,source.scheduled_source_day)
            : score_line(game,texts,exe,source.source_result);
        if (source.second_leg_visible)
            row.second_leg_result = score_line(game,texts,exe,source.source_second_leg,true);
        state.rows.push_back(std::move(row));
    }

    if (state.projection.descriptor_competition != 12 &&
        state.projection.display_mode == OtherGamesDisplayMode::detail &&
        state.projection.round >= 0 && state.projection.entry_index >= 0) {
        const int round_multiplier = state.projection.two_legs ? 2 : 1;
        const uint32_t day = source_round_day(game,state.projection.entry_address,
            round_multiplier*state.projection.round);
        state.date_heading = source_date_heading(game,texts,exe,day);
        if (state.projection.two_legs) {
            state.date_heading += original_literal(exe,::bmh::layout::LiveMatchday_FormatError_ExecutableField) +
                source_date_heading(game,texts,exe,source_round_day(game,state.projection.entry_address,
                    round_multiplier*state.projection.round+1)) +
                original_literal(exe,::bmh::layout::LiveMatchday_FormatError_ExecutableField2);
        }
    }
    if (!state.projection.knockout && state.projection.entry_address &&
        game.memory.u8(state.projection.entry_address+0x1a)>2) {
        state.matchday_heading = format_number(game,state.projection.round + 1,1) +
            (935 < texts.strings.size() ? texts.strings[935] : std::string{});
    }

    if (!state.projection.group_tables.empty()) {
        const uint32_t team_list = game.memory.pointer(state.projection.entry_address);
        const bool has_previous_rank=std::any_of(state.projection.group_tables.begin(),
            state.projection.group_tables.end(),[](const LeagueTable& table) {
                for(int i=0;i<table.size;++i) if(table.previous_rank(i)>0) return true;
                return false;
            });
        state.previous_rank_visible = has_previous_rank &&
            game.memory.u8(state.projection.entry_address + 0x1d) > 1;
        const int groups = game.memory.u8(state.projection.entry_address + 0x1e) & 0x0f;
        const int qualifiers = game.memory.u8(state.projection.entry_address + 0x1f);
        for (const LeagueTable& table : state.projection.group_tables) {
            const int count = std::min(table.size,20);
            for (int i = 0; i < count; ++i) {
                if (!game.memory.has(team_list + uint32_t(i) * 2 + 1))
                    invalid("truncated group table team list");
                const int team = game.memory.u16(team_list + uint32_t(i) * 2);
                if (team < 0 || size_t(team) >= game.club_ids.size()) invalid("invalid group-table club");
                OtherGamesTableRow row;
                row.runtime_team = team;
                const int rank_zero = table.rank(i);
                row.rank = rank_zero + 1;
                row.previous_rank = table.previous_rank(i) + 1;
                row.name = game.club_name(team);
            row.name_color = source_name_color(game,runtime,exe,team,0x5f);
                row.rank_color = groups > 0 && row.rank * groups <= qualifiers ? 0x7a :
                    groups > 0 && groups * rank_zero + 1 <= qualifiers &&
                        qualifiers < row.rank * groups ? 0x27 : 0xf7;
                row.form = table.form[size_t(i)];
                row.games = table.games(i);
                row.wins = table.wins[size_t(i)];
                row.defeats = table.defeats[size_t(i)];
                row.draws = row.games - row.wins - row.defeats;
                row.goals_for = table.goals_for[size_t(i)];
                row.goals_against = table.goals_against[size_t(i)];
                row.points = table.points[size_t(i)];
                state.table_rows.push_back(std::move(row));
            }
        }
        std::stable_sort(state.table_rows.begin(),state.table_rows.end(),[](const auto& a,const auto& b) {
            return a.rank < b.rank;
        });
    }
    return state;
}

} // namespace bmh::ui
