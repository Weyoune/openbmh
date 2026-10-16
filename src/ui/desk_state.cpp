// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/desk_state.hpp"

#include <algorithm>
#include <set>
#include <sstream>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/match_engine.hpp"
#include "game/matchday.hpp"
#include "game/schedule.hpp"
#include "game/season.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {

Bytes block_copy(const SaveState& s, uint32_t a) {
    const auto& b = s.block(a);
    return Bytes(s.stream().begin() + b.offset, s.stream().begin() + b.offset + b.size);
}
std::string bounded_string(const Bytes& b, size_t offset, size_t limit) {
    if (offset >= b.size()) throw FormatError("desk: name offset outside pool/record");
    const auto end = std::min(b.size(), offset + limit);
    const auto nul = std::find(b.begin() + offset, b.begin() + end, 0);
    if (nul == b.begin() + end) throw FormatError("desk: unterminated name");
    return std::string(b.begin() + offset, nul);
}
uint16_t word(const Bytes& b, size_t at) { return le16(b, at); }
int32_t signed_word(const Bytes& b, size_t at) { return int32_t(le32(b, at)); }

std::string pool_name(const Bytes& names, const std::vector<uint16_t>& offsets, int team) {
    if (team < 0 || size_t(team) >= offsets.size() || offsets[size_t(team)] >= names.size()) return {};
    return bounded_string(names, offsets[size_t(team)], names.size());
}

int executable_league(const LeImage& exe, int team) {
    if (team < 0 || team >= 138 || !exe.contains(::bmh::layout::LeagueSizes + 8)) return -1;
    int first = 0;
    for (int league = 0; league < 9; ++league) {
        if (!exe.contains(::bmh::layout::LeagueSizes + uint32_t(league))) return -1;
        first += exe.u8(::bmh::layout::LeagueSizes + uint32_t(league));
        if (team < first) return league;
    }
    return -1;
}

void project_drawer_rows(DeskState& d, const LeImage& exe, const Bytes& managers,
                         const Bytes& club_names, const std::vector<uint16_t>& club_ids,
                         const std::vector<uint8_t>& position_history,
                         const std::vector<uint8_t>& manager_results,
                         const std::vector<uint8_t>& league_rounds,
                         std::optional<int> authenticated_rival,
                         const GameState* game = nullptr) {
    if (d.manager_slot < 0) return;
    const size_t manager = size_t(d.manager_slot);
    const size_t record = manager * 0xb0c;
    if (record + 0x2aa <= managers.size()) {
        for (int match = 0; match < 6; ++match) {
            const size_t at = record + 0x292 + size_t(match) * 4;
            const int home = int16_t(le16(managers, at));
            const int away = int16_t(le16(managers, at + 2));
            if (home == away) break; // SourceDrawerTeam uses equal adjacent IDs as its terminator.
            if (home < 0 || away < 0 || size_t(home) >= club_ids.size() || size_t(away) >= club_ids.size())
                break;
            DeskRecentMatch row;
            row.home_team = home; row.away_team = away;
            row.home_name = pool_name(club_names, club_ids, home);
            row.away_name = pool_name(club_names, club_ids, away);
            row.source_score = managers.at(record + 0x212 + size_t(match));
            d.recent_matches.push_back(std::move(row));
        }
    }

    auto rank_curve = [&](int team, DeskRankCurve& curve) {
        curve.runtime_team = team;
        curve.team_name = pool_name(club_names, club_ids, team);
        if (team == 0x165) {
            // TablePosition returns rank 2 for this special national row;
            // QuoteNationalFriendlyMatch uses its native no-team squad (no random rating).
            curve.current_position = 2;
            if (game) curve.strength = team_average(*game, kNoTeam, 0x1ff).value;
            return;
        }
        const int league = executable_league(exe, team);
        if (league < 0 || size_t(league) >= league_rounds.size()) return;
        curve.league = league;
        const int rounds = std::min<int>(league_rounds[size_t(league)], 38);
        const size_t team_base = size_t(team) * 13 * 2;
        if (team_base + size_t((rounds + 2) / 3) * 2 > position_history.size()) return;
        for (int round = 0; round < rounds; ++round) {
            const size_t word_at = team_base + size_t(round / 3) * 2;
            const uint16_t packed = uint16_t(position_history[word_at] |
                (uint16_t(position_history[word_at + 1]) << 8));
            curve.samples.push_back({round + 1, int((packed >> ((round % 3) * 5)) & 0x1f)});
        }
        if (!curve.samples.empty()) curve.current_position = curve.samples.back().source_rank;
        if (game && team < 138) curve.strength = team_average(*game, team, 0x1ff).value;
    };
    rank_curve(d.runtime_club < 0 ? 0x165 : d.runtime_club, d.rank_curves[0]);
    if (authenticated_rival && *authenticated_rival >= 0 && *authenticated_rival != d.runtime_club)
        rank_curve(*authenticated_rival, d.rank_curves[1]);

    // SourceDrawerTeam reads only the prepared opponent's venue history. Other
    // teams' names need not be available to render this manager's current Desk.
    if (!authenticated_rival || *authenticated_rival < 0 || *authenticated_rival >= 168) return;
    const size_t at = manager * 0x690 + size_t(*authenticated_rival) * 10;
    if (at + 10 > manager_results.size()) return;
    const bool present = std::any_of(manager_results.begin() + at,
        manager_results.begin() + at + 10, [](uint8_t value) { return value != 0xff; });
    if (!present) return;
    DeskOpponentHistory row;
    row.runtime_team = *authenticated_rival;
    row.team_name = pool_name(club_names, club_ids, row.runtime_team);
    std::copy_n(manager_results.begin() + at, row.source_results.size(), row.source_results.begin());
    d.opponent_histories.push_back(std::move(row));
}

void project_table_position(DeskState& desk, const GameState& game) {
    // SourceDrawerTeam permits league clubs and CupDraw_FormatError_RuntimeField2 (the Source 0x165
    // national-team slot) through its rank/strength path.
    const int position = table_position(game, desk.runtime_club);
    if (position >= 0) desk.table_position = position;
}

struct Tables {
    const Bytes &managers, &tactics, &options, &stocks, &badges, &names;
    const std::vector<uint16_t>& name_offsets;
    const std::vector<uint16_t>& squads;
    const std::vector<Player>& players;
    const std::vector<uint8_t>& morale;
    const std::vector<int>& teams;
    const std::vector<uint8_t>& rounds;
};

DeskState project(const StaticMemory& memory, const Tables& t, const LeImage& exe,
                  int level, uint32_t date, int season_day, int season, uint64_t revision) {
    DeskState d;
    d.revision = revision;
    d.options.duration = memory.u8(::bmh::layout::CareerDuration); d.options.mode = memory.u8(::bmh::layout::GameMode);
    d.options.managers = memory.u8(::bmh::layout::ManagerCount); d.options.difficulty = level;
    const auto competitions = memory.u32(::bmh::layout::CompetitionSelection);
    d.options.league = (competitions & 1) != 0; d.options.world = (competitions & 2) != 0;
    if (d.options.managers < 1 || d.options.managers > 4 || level < 0 || level > 9 ||
        d.options.duration < 1 || d.options.duration > 5 || d.options.mode < 1 || d.options.mode > 3 ||
        (!d.options.league && !d.options.world)) throw FormatError("desk: invalid saved setup");
    d.manager_slot = memory.u8(::bmh::layout::ActiveManager);
    if (d.manager_slot < 0 || d.manager_slot >= d.options.managers ||
        t.teams.size() != size_t(d.options.managers)) throw FormatError("desk: unsupported/inconsistent active manager");
    const uint32_t layout_base = ::bmh::layout::Desktops + uint32_t(d.manager_slot) * 0x360;
    for (int record = 0; record < 54; ++record) {
        const uint32_t at = layout_base + uint32_t(record) * 16;
        for (int byte = 0; byte < 16; ++byte)
            d.layout_records[size_t(record)][size_t(byte)] = memory.u8(at + uint32_t(byte));
    }
    d.layout_snap = memory.u8(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField2) == 0;
    // SourceDrawerTeam cases 0..2 collect folder children from records 1..30;
    // the item number itself is the KB/3 tile index plus one.
    for (int slot = 0; slot < 30; ++slot) {
        const int item = slot + 1;
        const auto& record = d.layout_records[size_t(item)];
        if ((record[1] & 0x10) != 0 || record[5] != 0xff) continue;
        const int folder = record[13] & 0x7f;
        if (folder >= 0 && folder < 3 && d.folder_icon_slots[size_t(folder)].size() < 12)
            d.folder_icon_slots[size_t(folder)].push_back(uint8_t(item));
    }
    d.manager_teams = t.teams;
    std::set<int> unique_teams;
    d.managers.reserve(d.manager_teams.size());
    for (size_t slot = 0; slot < d.manager_teams.size(); ++slot) {
        const int team = d.manager_teams[slot];
        if (team < 0 || team >= int(t.name_offsets.size()) || !unique_teams.insert(team).second)
            throw FormatError("desk: invalid or duplicate manager runtime club");
        const size_t manager_base = slot * 0xb0c;
        if (t.managers.size() < manager_base + 0xb0c || t.badges.size() < size_t(team + 1) * 2)
            throw FormatError("desk: truncated manager identity/assets");
        DeskManagerIdentity identity;
        identity.runtime_club = team;
        // club_ids maps runtime teams to stable name-pool identities. Season
        // table changes may replace the runtime ID while preserving this key.
        identity.club_identity_key = int(t.name_offsets.at(size_t(team)));
        identity.name = bounded_string(t.managers, manager_base + 0xa8, 26);
        identity.league = t.managers.at(manager_base + 0x210);
        identity.portrait = t.managers.at(manager_base + 0x221);
        identity.badge = word(t.badges, size_t(team) * 2) & 0x1ff;
        if ((team != kNoTeam && identity.league > 8) || identity.portrait > 11)
            throw FormatError("desk: invalid manager identity");
        d.managers.push_back(std::move(identity));
    }
    const size_t base = size_t(d.manager_slot) * 0xb0c;
    if (t.managers.size() < base + 0xb0c || t.options.size() < size_t(d.manager_slot + 1) * 5)
        throw FormatError("desk: truncated manager records/options");
    d.runtime_club = t.teams.at(d.manager_slot);
    d.national_team = d.runtime_club == kNoTeam;
    d.national_coach_takeover = memory.has(::bmh::layout::CoachActive) && memory.u8(::bmh::layout::CoachActive) != 0;
    if (d.national_team && d.national_coach_takeover) {
        const int saved_manager_count = memory.has(::bmh::layout::OriginalManagerCount) ? memory.u8(::bmh::layout::OriginalManagerCount) : 0;
        if (saved_manager_count < 1 || saved_manager_count > 4)
            throw FormatError("desk: invalid saved manager count during national takeover");
        d.national_return_roster.reserve(size_t(saved_manager_count));
        for (int slot = 0; slot < saved_manager_count; ++slot) {
            // SwapManagerSlots2 swaps the original slot 0 through spare record 4;
            // other human records remain in their original slots.
            const size_t source_slot = slot == 0 ? 4 : size_t(slot);
            const size_t manager_base = source_slot * 0xb0c;
            if (t.managers.size() < manager_base + 0x292)
                throw FormatError("desk: truncated saved manager roster during national takeover");
            const int team = word(t.managers, manager_base + 0x290);
            if (team < 0 || team >= int(t.name_offsets.size()) || team == kNoTeam ||
                t.badges.size() < size_t(team + 1) * 2)
                throw FormatError("desk: invalid home club in national return roster");
            DeskManagerIdentity original;
            original.runtime_club = team;
            original.club_identity_key = int(t.name_offsets.at(size_t(team)));
            original.name = bounded_string(t.managers, manager_base + 0xa8, 26);
            original.league = t.managers.at(manager_base + 0x210);
            original.portrait = t.managers.at(manager_base + 0x221);
            original.badge = word(t.badges, size_t(team) * 2) & 0x1ff;
            d.national_return_roster.push_back(std::move(original));
        }
        const auto coach = std::find_if(d.national_return_roster.begin(), d.national_return_roster.end(),
            [&](const DeskManagerIdentity& candidate) {
                return candidate.name == d.managers.at(0).name &&
                       candidate.portrait == d.managers.at(0).portrait;
            });
        if (coach == d.national_return_roster.end())
            throw FormatError("desk: national coach is absent from the saved manager roster");
        d.managers.at(0).club_identity_key = coach->club_identity_key;
    }
    if (d.runtime_club < 0 || d.runtime_club >= int(t.name_offsets.size())) throw FormatError("desk: invalid runtime club");
    if (d.national_team && ((!d.options.world && !d.national_coach_takeover) ||
                            d.manager_slot != 0 || d.options.managers != 1))
        throw FormatError("desk: national team requires one manager and world-mode or source takeover evidence");
    d.manager_name = d.managers[size_t(d.manager_slot)].name;
    d.club_name = bounded_string(t.names, t.name_offsets.at(d.runtime_club), t.names.size());
    d.badge = d.managers[size_t(d.manager_slot)].badge;
    d.league = d.managers[size_t(d.manager_slot)].league; d.portrait = d.managers[size_t(d.manager_slot)].portrait;
    if ((!d.national_team && d.league > 8) || d.portrait > 11) throw FormatError("desk: invalid league/portrait");
    d.day_counter = date; d.season_day = season_day; d.season_index = season;
    if (season < 0 || season_day < 0 || season_day > 366) throw FormatError("desk: invalid season context");
    d.date = desk_date(date, exe);
    if (date == 0 && season == 0 && season_day == 0) { d.next_competition = 33; d.next_opponent = -1; }
    // A completed round count is not proof of the current/next matchday.
    if (!d.national_team && size_t(d.league) >= t.rounds.size()) throw FormatError("desk: missing league round counters");
    d.balance = signed_word(t.managers, base + 0x314); d.savings = signed_word(t.managers, base + 0x318);
    d.ticket_price = t.managers.at(base + 0x26e); d.fans = t.managers.at(base + 0x211);
    // Currency constants are not FBM blocks. Read executable defaults unless the
    // application explicitly supplies their live values in StaticMemory.
    d.money_numerator = int32_t(memory.has(::bmh::layout::CurrencyMultiplier) ? memory.u32(::bmh::layout::CurrencyMultiplier) : exe.u32(::bmh::layout::CurrencyMultiplier));
    d.money_denominator = int32_t(memory.has(::bmh::layout::CurrencyDivisor) ? memory.u32(::bmh::layout::CurrencyDivisor) : exe.u32(::bmh::layout::CurrencyDivisor));
    if (d.money_denominator <= 0) throw FormatError("desk: invalid currency denominator");
    d.decorated_background = memory.u8(::bmh::layout::PlainBackground) == 0;
    d.gradient_text = memory.u8(::bmh::layout::TextGradientDisabled) == 0 && exe.u8(::bmh::layout::TextGradientMode) == 2;
    d.drawer_handles = memory.u8(::bmh::layout::CareerSession_DispatchDesktopLayout_RuntimeField) == 0;
    d.paired_drawers_away_order = memory.u8(::bmh::layout::DeskState_FormatError_RuntimeField2) == 0 && t.managers.at(base+0x28a) != 0;
    d.shortcut_bar_always = memory.u8(::bmh::layout::DeskState_FormatError_RuntimeField3) == 0;
    // CopyN returns the matching record index. The last bar cell (value
    // 72) resolves to native record 53; DeskDate dispatches that index as MEN.
    // Record 34/kind 53 belongs to the curves drawer and is not the MEN handle.
    for (int item = 0; item < 54; ++item) {
        const uint32_t at = ::bmh::layout::Desktops + uint32_t(d.manager_slot) * 0x360 + uint32_t(item) * 16;
        if (item != 53 || memory.u8(at + 12) != 72) continue;
        DeskItem shortcut;
        shortcut.id = item; shortcut.kind = 53;
        shortcut.x = 0x16 + (72 - 50) * 12; shortcut.y = 0xd7;
        shortcut.width = 12; shortcut.height = 11;
        d.menu_layout_shortcut = shortcut;
        break;
    }
    // The mailbox dispatcher additionally requires record index 44.
    for (int item = 0; item < 54; ++item) {
        const uint32_t at = ::bmh::layout::Desktops + uint32_t(d.manager_slot) * 0x360 + uint32_t(item) * 16;
        if (memory.u8(at + 12) != 63) continue;
        // The dispatcher opens mail only for returned item index 44. A changed
        // kind-to-index mapping is not evidence for a different mailbox entry.
        if (item != 44) break;
        DeskItem shortcut;
        shortcut.id = item; shortcut.kind = 63;
        shortcut.x = 0x16 + (63 - 50) * 12; shortcut.y = 0xd7;
        shortcut.width = 12; shortcut.height = 11;
        // Unlike ordinary desk icons, CopyN checks no disabled mask.
        shortcut.disabled = false;
        d.mailbox_shortcut = shortcut;
        break;
    }
    std::copy_n(t.options.begin() + size_t(d.manager_slot) * 5, 5, d.automation.begin());

    // Fill: display predicates only. No game state is changed.
    d.warnings.fill(false);
    for (size_t share = 0; share < 12; ++share) {
        const size_t at = share * 0x5c;
        const auto owned = word(t.stocks, at + 0x50 + size_t(d.manager_slot) * 2);
        if (owned && le32(t.stocks, at + 4 + size_t(d.manager_slot) * 4) / owned < word(t.stocks, at + 0x4e))
            d.warnings[0] = true;
    }
    const int64_t overdraft = signed_word(t.managers, base + 0x31c);
    d.warnings[1] = -int64_t(d.balance) != overdraft && d.balance <= -overdraft;
    for (size_t ad = 0; ad < 16; ++ad)
        if (!t.managers.at(base + 0xa39 + ad * 12)) d.warnings[2] = true;
    uint8_t remaining = 18;
    for (size_t kind = 6; kind < 13; ++kind) remaining = uint8_t(remaining - t.managers.at(base + 0xc5 + kind));
    d.warnings[3] = remaining < 4;
    const int max24 = exe.u16(::bmh::layout::TeamsWithSquads), league_teams = exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField);
    if (!d.national_team && d.runtime_club >= league_teams) throw FormatError("desk: cup-only squad projection not supported");
    const size_t first = d.national_team ? 3072 : size_t(d.runtime_club < max24 ? d.runtime_club * 24 : d.runtime_club * 16 + max24 * 8);
    const size_t count = d.national_team ? 24 : (d.runtime_club < max24 ? 24 : 16);
    if (t.squads.size() < first + count || t.tactics.size() < size_t(d.manager_slot) * 0x840 + count * 0x58)
        throw FormatError("desk: truncated squad/tactics");
    int lineup = 0, reserve = 0, condition = 0, technique = 0, form = 0, morale = 0;
    for (size_t row = 0; row < count; ++row) {
        const auto slot = t.squads.at(first + row);
        if ((slot & 0xfff) == 0xfff) continue;
        const size_t board = size_t(d.manager_slot) * 0x840 + row * 0x58;
        if (slot & 0x8000) {
            ++lineup;
            const auto& p = t.players.at(slot & 0xfff);
            condition += p.condition(); technique += p.technique(); form += p.form();
            morale += (t.morale.at(d.runtime_club) & 0x3f) * 2;
        }
        if (slot & 0x4000) ++reserve;
        if (row < 11 && t.tactics.at(board + 0x0f) > 69) d.warnings[4] = true;
        if (t.tactics.at(board + 0x0c) < 4) d.warnings[8] = true;
    }
    d.warnings[5] = lineup < 11; d.warnings[6] = reserve < 5;
    d.warnings[7] = int8_t(t.managers.at(base + 0xd9)) > 8;
    // QuoteNationalFriendlyMatch(team, 0x1ff): pure display strength; fixed divisor of eleven.
    int strength = (condition + technique) / 2;
    if (form) strength = (100 + (form / 11 - 50) / 8) * strength / 100;
    if (morale) strength = (100 + (morale / 11 - 50) / 8) * strength / 100;
    d.strength = std::max(1, std::clamp(strength / 11, 0, 99));
    // Int64T -> HighscoreScore(0x0f/0x10,0,0xff): readonly assets/debt sum.
    int64_t assets = int64_t(d.balance) + d.savings, debt = 0;
    for (size_t property = 0; property < 6; ++property) {
        const auto at = ::bmh::layout::FundBlockUnsigned + uint32_t(property) * 0x1c;
        assets += int64_t(memory.u16(at + 0x14 + uint32_t(d.manager_slot) * 2)) * memory.u32(at);
    }
    for (size_t share = 0; share < 12; ++share)
        assets += int64_t(word(t.stocks, share * 0x5c + 0x50 + size_t(d.manager_slot) * 2)) * word(t.stocks, share * 0x5c + 0x4e);
    for (size_t loan = 0; loan < 5; ++loan) {
        const auto at = ::bmh::layout::LoansUnsigned + uint32_t(d.manager_slot) * 0x78 + uint32_t(loan) * 0x18;
        debt += int64_t(int32_t(memory.u32(at + 4))) - int32_t(memory.u32(at + 0x14));
    }
    const auto licence = t.managers.at(base + 0x220);
    d.warnings[9] = assets - debt + int64_t(t.managers.at(base + 0x26f)) * 80000 < -6000000 ||
        ((licence >> 5) == 2 && int64_t(licence & 31) * 100000 <= signed_word(t.managers, base + 0x330));

    std::set<int> visited;
    const auto layout = ::bmh::layout::DesktopsUnsigned + uint32_t(d.manager_slot) * 0x360;
    int current = memory.u8(layout + 5);
    while (current != 0) {
        if (current < 1 || current >= 54 || !visited.insert(current).second)
            throw FormatError("desk: invalid/cyclic layout list");
        const auto at = layout + uint32_t(current) * 16;
        const auto xflags = memory.u16(at);
        if (!(xflags & 0x1000)) {
            DeskItem item;
            item.id = current; item.x = int16_t(xflags & ~0x1000); item.y = memory.u8(at + 2);
            item.width = memory.u8(at + 3); item.height = memory.u8(at + 4);
            item.kind = memory.u8(at + 12); item.flags = memory.u8(at + 13);
            item.folder = item.flags & 0x7f; item.open = (item.flags & 0x80) != 0;
            item.disabled = (memory.u8(at + 1) & 0x10) != 0;
            // SourceDrawerTeam derives the three folder drawer dimensions from
            // child count; stored layout dimensions are not authoritative.
            const int drawer_kind = item.kind - 50;
            if (drawer_kind >= 0 && drawer_kind < 3) {
                const size_t count = d.folder_icon_slots[size_t(drawer_kind)].size();
                if (count > 12 || !exe.contains(::bmh::layout::DeskState_FormatError_ExecutableFieldUnsigned + uint32_t(count)) ||
                    !exe.contains(::bmh::layout::DeskState_FormatError_ExecutableField2Unsigned + uint32_t(count)))
                    throw FormatError("desk: folder child count outside Source dimensions");
                item.width = exe.u8(::bmh::layout::DeskState_FormatError_ExecutableFieldUnsigned + uint32_t(count));
                item.height = exe.u8(::bmh::layout::DeskState_FormatError_ExecutableField2Unsigned + uint32_t(count));
            }
            // Club-only finance, stadium, Toto and training screens require
            // projections unavailable to a national coach.
            if (d.national_team && (item.kind == 13 || item.kind == 14 ||
                                    item.kind == 15 || item.kind == 16 ||
                                    item.kind == 22)) item.disabled = true;
            if (item.x < 0 || item.x > 319 || item.y > 239 || item.kind > 72)
                throw FormatError("desk: invalid layout coordinates/type");
            d.items.push_back(item);
        }
        current = memory.u8(at + 5);
    }
    validate_desk_state(d);
    return d;
}
std::vector<uint16_t> words(const Bytes& data) {
    if (data.size() % 2) throw FormatError("desk: odd word-table size");
    std::vector<uint16_t> result;
    for (size_t i = 0; i < data.size(); i += 2) result.push_back(le16(data, i));
    return result;
}
} // namespace

void validate_desk_profile(const SetupOptions& o) {
    if (o.managers < 1 || o.managers > 4 || o.difficulty < 0 || o.difficulty > 9 ||
        o.duration < 1 || o.duration > 5 || o.mode < 1 || o.mode > 3 ||
        (!o.league && !o.world) || (!o.league && o.managers != 1))
        throw FormatError("unsupported_desk_profile: invalid manager count or competition options");
}

void validate_desk_state(const DeskState& d) {
    validate_desk_profile(d.options);
    if (d.mailbox_shortcut && (d.mailbox_shortcut->id != 44 ||
        d.mailbox_shortcut->kind != 63 || d.mailbox_shortcut->x != 178 || d.mailbox_shortcut->y != 215 ||
        d.mailbox_shortcut->width != 12 || d.mailbox_shortcut->height != 11))
        throw FormatError("desk: invalid original mailbox shortcut");
    if (d.national_team && d.mailbox_notification_pending)
        throw FormatError("desk: national runtime cannot auto-open the mailbox");
    if (d.menu_layout_shortcut && (d.menu_layout_shortcut->id != 53 ||
        d.menu_layout_shortcut->kind != 53 || d.menu_layout_shortcut->x != 286 ||
        d.menu_layout_shortcut->y != 215 || d.menu_layout_shortcut->width != 12 ||
        d.menu_layout_shortcut->height != 11))
        throw FormatError("desk: invalid original menu-layout shortcut");
    if (d.manager_teams.size() != size_t(d.options.managers) || d.managers.size() != size_t(d.options.managers) || d.manager_slot < 0 ||
        d.manager_slot >= d.options.managers || d.runtime_club != d.manager_teams[size_t(d.manager_slot)])
        throw FormatError("desk: inconsistent manager slot/runtime mapping");
    std::set<int> unique, unique_identities;
    for (size_t slot = 0; slot < d.manager_teams.size(); ++slot) {
        const int team = d.manager_teams[slot];
        const auto& manager = d.managers[slot];
        if (team < 0 || !unique.insert(team).second || manager.runtime_club != team ||
            manager.club_identity_key < 0 || !unique_identities.insert(manager.club_identity_key).second ||
            manager.portrait < 0 || manager.portrait > 11 ||
            (team != kNoTeam && (manager.league < 0 || manager.league > 8)))
            throw FormatError("desk: invalid or duplicate manager runtime club");
    }
    const bool national = d.runtime_club == kNoTeam;
    if (d.table_position && (*d.table_position < 0 || *d.table_position > 31))
        throw FormatError("desk: invalid Source table position");
    if (d.national_team != national ||
        (national && (d.manager_slot != 0 || d.options.managers != 1 ||
                      (!d.options.world && !d.national_coach_takeover))))
        throw FormatError("desk: inconsistent national-team manager context");
    if (national && d.national_coach_takeover) {
        if (d.national_return_roster.size() < 1 || d.national_return_roster.size() > 4)
            throw FormatError("desk: missing national return roster");
        std::set<int> return_keys;
        for (const auto& manager : d.national_return_roster)
            if (manager.runtime_club < 0 || manager.runtime_club >= kNoTeam ||
                manager.club_identity_key < 0 || !return_keys.insert(manager.club_identity_key).second ||
                manager.portrait < 0 || manager.portrait > 11 || manager.league < 0 || manager.league > 8)
                throw FormatError("desk: invalid national return roster identity");
        const auto& active = d.managers[size_t(d.manager_slot)];
        if (std::none_of(d.national_return_roster.begin(), d.national_return_roster.end(),
            [&](const DeskManagerIdentity& manager) {
                return manager.club_identity_key == active.club_identity_key &&
                       manager.name == active.name && manager.portrait == active.portrait;
            }))
            throw FormatError("desk: national manager does not match return roster");
    } else if (!d.national_return_roster.empty()) {
        throw FormatError("desk: unexpected national return roster outside takeover");
    }
    const auto& active = d.managers[size_t(d.manager_slot)];
    if (d.manager_name != active.name || d.portrait != active.portrait || d.league != active.league || d.badge != active.badge)
        throw FormatError("desk: active manager identity does not match its slot projection");
}

DeskDate desk_date(uint32_t counter, const LeImage& exe) {
    if (counter == 0xffff || counter > 3000000) throw FormatError("desk: unsupported date counter");
    DeskDate d; d.weekday = int((counter + 2) % 7);
    int cycle = 3, years = 0;
    while (counter > uint32_t(364 + (cycle == 4))) {
        if (cycle == 4) { cycle = 0; --counter; }
        ++years; counter -= 365; ++cycle;
    }
    d.month = 7;
    auto month_days = [&] { return d.month == 2 ? 28 + (cycle == 4) : exe.u8(::bmh::layout::DeskState_DeskDate_RuntimeField + d.month); };
    while (counter > uint32_t(month_days())) {
        counter -= uint32_t(month_days());
        if (++d.month == 13) { d.month = 1; ++years; }
    }
    d.day = int(counter) + 1;
    if (d.day > month_days()) {
        d.day -= month_days();
        if (++d.month == 13) { d.month = 1; ++years; }
    }
    d.year = 2026 + years;
    return d;
}

DeskState desk_from_save(const SaveState& s, const LeImage& exe, uint64_t revision) {
    StaticMemory memory(s);
    auto manager = block_copy(s, ::bmh::layout::ManagerRecords), tactics = block_copy(s, ::bmh::layout::TacticsBoards), options = block_copy(s, ::bmh::layout::ManagerAutomation);
    auto stocks = block_copy(s, ::bmh::layout::StockExchange), badges = block_copy(s, ::bmh::layout::ClubBadgeSelectors), morale = block_copy(s, ::bmh::layout::TeamMorale);
    auto offsets = words(block_copy(s, ::bmh::layout::ClubIdentities)), squads = words(block_copy(s, ::bmh::layout::SquadSlots));
    auto w1 = block_copy(s, ::bmh::layout::PlayerConditionTechniqueForm), w2 = block_copy(s, ::bmh::layout::PlayerSkillWords), w3 = block_copy(s, ::bmh::layout::PlayerIdentityWords);
    std::vector<Player> players;
    if (w1.size() != w2.size() || w1.size() != w3.size() || w1.size() % 4) throw FormatError("desk: inconsistent player tables");
    for (size_t at = 0; at < w1.size(); at += 4) players.push_back({{}, le32(w1, at), le32(w2, at), le32(w3, at)});
    std::vector<int> teams;
    for (int i = 0; i < memory.u8(::bmh::layout::ManagerCount); ++i) teams.push_back(word(manager, size_t(i) * 0xb0c + 0x290));
    auto rounds = block_copy(s, ::bmh::layout::LeagueRoundCounters);
    auto d = project(memory, {manager, tactics, options, stocks, badges, s.pool(0), offsets, squads, players, morale, teams, rounds},
                     exe, memory.u8(::bmh::layout::Difficulty), memory.u32(::bmh::layout::CalendarDay), memory.u16(::bmh::layout::DaysIntoSeason), int16_t(memory.u16(::bmh::layout::SeasonIndex)), revision);
    GameState table_source;
    const auto position_history = block_copy(s, ::bmh::layout::LeagueRankHistory);
    const auto results_by_opponent = block_copy(s, ::bmh::layout::ManagerMatchHistory);
    table_source.position_history = position_history;
    table_source.league_rounds = rounds;
    project_table_position(d, table_source);
    project_drawer_rows(d, exe, manager, s.pool(0), offsets, position_history,
                        results_by_opponent, rounds, std::nullopt);
    d.source = "original_save_read_only"; return d;
}

DeskState desk_from_game(const GameState& g, const LeImage& exe, uint64_t revision,
                         const std::vector<MatchGroup>* current_groups,
                         std::optional<int> source_default_competition,
                         const DeskMatchCallargs* source_callargs) {
    auto d = project(g.memory, {g.manager_records, g.tactics, g.manager_options, g.stocks, g.club_heap_78, g.club_names,
                     g.club_ids, g.squad_slots, g.players, g.team_morale, g.manager_teams, g.league_rounds},
                     exe, g.level, g.date, g.season_day(), g.season, revision);
    // DeskDate only auto-opens for an ordinary club manager; the national
    // runtime (0x165) suppresses this consumer while manual mailbox access remains valid.
    if (d.runtime_club != kNoTeam && d.manager_slot >= 0 && d.manager_slot < 32 && g.memory.has(::bmh::layout::MailboxRecipientMask))
        d.mailbox_notification_pending = (g.memory.u32(::bmh::layout::MailboxRecipientMask) & (uint32_t(1) << d.manager_slot)) != 0;
    project_table_position(d, g);
    if (!current_groups && g.date == 0 && g.season == 0 && g.season_day() == 0 && g.texts) {
        const size_t title = (::bmh::layout::DeskState_DeskFromGame_TextPointer - ::bmh::layout::TextPointerTable) / 4 + 33;
        if (title < g.texts->strings.size()) d.next_competition_label = g.texts->strings[title];
    }
    if (current_groups) {
        // DayCompetitions2 defaults to 33, changes the fallback to 34 on a day with
        // active competition bits, and uses 32 for the season-end desk. The
        // adapter supplies that already-known desk state; this projection never
        // prepares a fixture or advances a calendar day.
        d.next_competition = source_default_competition.value_or(current_groups->empty() ? 33 : 34);
        d.next_opponent = -1;
        d.next_match_away = false;
        if (source_callargs) {
            d.next_competition = source_callargs->competition;
            d.next_opponent = source_callargs->opponent;
        } else {
          const int team = d.manager_teams.at(size_t(d.manager_slot));
          for (const MatchGroup& group : *current_groups) {
            const int type = group.desc.empty() ? -1 : group.desc.at(0);
            int mapped_competition = -1;
            int league = -1;
            if (type == 0) {
                league = int(le32(group.desc, 0xcc));
                // FixturesFromGenerateFixtures receives one of the nine league IDs represented
                // by LeagueCalendarBits; the prepared group descriptor carries that ID.
                if (league >= 0 && league < 9) mapped_competition = exe.u8(::bmh::layout::LeagueCalendarBits + uint32_t(league));
            } else if (type > 0) {
                // setup_cup_groups stores Source type + 1 in descriptor byte 0;
                // MatchdayNeedsManagerDesks writes cup type + 5 to the desk array.
                mapped_competition = type + 4;
            }
            for (size_t match = 0; match < size_t(group.matches()); ++match) {
                const int home = group.fixtures.at(match * 2);
                const int away = group.fixtures.at(match * 2 + 1);
                if (team != home && team != away) continue;
                // MatchdayNeedsManagerDesks checks the raw league result byte before writing
                // the competition code. setup_matchday does not populate
                // MatchGroup::results; kick_off normalizes g.results later. Use
                // the group copy when present, otherwise read the authoritative
                // raw season result at league*0x17c + round*10 + match.
                bool has_result = false;
                uint8_t raw_result = 0xcc;
                if (type == 0 && match < group.results.size()) {
                    has_result = true;
                    raw_result = group.results[match];
                } else if (type == 0 && league >= 0 &&
                           league < int(g.league_rounds.size())) {
                    const int round = int(g.league_rounds[size_t(league)]);
                    const size_t result_at = size_t(league) * 0x17c +
                                             size_t(round) * 10 + match;
                    if (round >= 0 && result_at < g.results.size()) {
                        has_result = true;
                        raw_result = g.results[result_at];
                    }
                }
                if (type == 0 && has_result && raw_result != 0xcc) {
                    d.next_opponent = -1;
                    d.next_opponent_name.clear();
                    d.next_opponent_badge.reset();
                    d.next_opponent_badge_fallback = false;
                    d.next_match_away = false;
                    continue;
                }
                // Source MatchdayNeedsManagerDesks walks groups and fixture slots in this
                // order; each later matching group overwrites the prior desk
                // values. Preserve that behavior for unusual overlapping events.
                const int opponent = team == home ? away : home;
                d.next_opponent = opponent;
                if (mapped_competition >= 0) d.next_competition = mapped_competition;
            }
          }
        }
        if (d.next_opponent && *d.next_opponent >= 0) {
            const size_t manager_record = size_t(d.manager_slot) * 0xb0c;
            d.next_match_away = manager_record + 0x28a < g.manager_records.size() &&
                g.manager_records[manager_record + 0x28a] != 0;
        }
        if (d.next_opponent && *d.next_opponent >= 0 &&
            *d.next_opponent < int(g.club_ids.size())) {
            const int opponent = *d.next_opponent;
            d.next_opponent_name = g.club_name(opponent);
            const bool fallback = opponent == 0x0fff ||
                (opponent > 0x59 && (opponent < 0x8a || opponent > 0xa7) && opponent < 0x165);
            d.next_opponent_badge_fallback = fallback;
            if (!fallback) {
                const size_t badge_at = size_t(opponent) * 2;
                if (badge_at + 1 >= g.club_heap_78.size())
                    throw FormatError("desk: missing Source opponent badge selector");
                d.next_opponent_badge = uint16_t(g.club_heap_78[badge_at] |
                    (uint16_t(g.club_heap_78[badge_at + 1]) << 8)) & 0x1ff;
            } else d.next_opponent_badge.reset();
        } else {
            d.next_opponent_name.clear();
            d.next_opponent_badge.reset();
            d.next_opponent_badge_fallback = d.next_opponent && *d.next_opponent >= 0;
        }
        const int competition = d.next_competition.value_or(-1);
        if (competition >= 0 && competition < 5) {
            const size_t name_id = size_t((::bmh::layout::Achievements_FormatError_TextPointer3 - ::bmh::layout::TextPointerTable) / 4 + competition);
            const size_t league = exe.u8(::bmh::layout::DeskState_FormatError_RuntimeField + uint32_t(competition));
            if (g.texts && name_id < g.texts->strings.size() && league < g.league_rounds.size()) {
                d.next_competition_label = g.texts->strings[name_id] + "\n" +
                    std::to_string(int(g.league_rounds[league]) + 1) + g.texts->strings.at((::bmh::layout::CupDraw_FormatError_TextPointer6 - ::bmh::layout::TextPointerTable) / 4);
            }
        } else if (competition >= 5) {
            size_t label_id = 0;
            const uint32_t calendar_at = ::bmh::layout::CompetitionCalendar + uint32_t(std::max(0, g.season_day())) * 2;
            if (g.season_day() >= 0 && g.season_day() < 365 && g.memory.has(calendar_at + 1) &&
                g.memory.u16(calendar_at) == 0x4006) {
                label_id = (::bmh::layout::DeskState_FormatError_RuntimeField4 - ::bmh::layout::TextPointerTable) / 4;
            } else if (competition < 30) {
                label_id = (::bmh::layout::DeskState_FormatError_ExecutableField4 - ::bmh::layout::TextPointerTable) / 4 + size_t(competition);
            } else {
                label_id = (::bmh::layout::DeskState_DeskFromGame_TextPointer - ::bmh::layout::TextPointerTable) / 4 + size_t(competition);
            }
            if (g.texts && label_id < g.texts->strings.size()) d.next_competition_label = g.texts->strings[label_id];
            if (competition == 34 && !current_groups->empty()) {
                for (const MatchGroup& group : *current_groups) {
                    if (!group.desc.empty() && group.desc.at(0) == 0) {
                        const int fallback_league = int(le32(group.desc, 0xcc));
                        if (fallback_league >= 0 && fallback_league < 9) {
                            d.next_competition_detail = "(" + league_name(g, fallback_league) + ")";
                        }
                        break;
                    } else if (!group.desc.empty() && group.desc.at(0) > 0) {
                        // SourceDrawerTeam reads these through the runtime TEXTD
                        // pointer table. Do not substitute LE-executable strings;
                        // the decompile's repeated-dereference condition for the
                        // optional prefix is unverified and is intentionally omitted.
                        const size_t name_id = size_t((::bmh::layout::DeskState_FormatError_ExecutableField3 - ::bmh::layout::TextPointerTable) / 4) + group.desc.at(0);
                        if (g.texts && name_id < g.texts->strings.size() &&
                            !g.texts->strings[name_id].empty())
                            d.next_competition_detail = "(" + g.texts->strings[name_id] + ")";
                        break;
                    }
                }
            }
        }
    }
    std::optional<int> authenticated_rival;
    if (source_callargs && source_callargs->opponent >= 0)
        authenticated_rival = source_callargs->opponent;
    else if (current_groups && d.next_opponent && *d.next_opponent >= 0)
        authenticated_rival = *d.next_opponent;
    project_drawer_rows(d, exe, g.manager_records, g.club_names, g.club_ids,
                        g.position_history, g.manager_results, g.league_rounds,
                        authenticated_rival, &g);
    d.source = "engine_projection"; return d;
}

DeskState load_reference_desk(const std::filesystem::path& path, const LeImage& exe) {
    auto fbm = load_savegame(path);
    if (!fbm.checksum_ok) throw FormatError("desk: FBM checksum mismatch");
    if (fbm.version != "V2.24") throw FormatError("desk: unsupported FBM version");
    DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2), exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
    SaveState save(fbm.stream, counts);
    auto d = desk_from_save(save, exe);
    validate_desk_state(d);
    return d;
}

std::string desk_json(const DeskState& d) {
    std::ostringstream out;
    out << "{\"screen\":\"desktop\",\"source\":" << json_quote(d.source) << ",\"revision\":" << d.revision
        << ",\"options\":" << setup_json(d.options) << ",\"manager_slot\":" << d.manager_slot
        << ",\"manager_name\":" << json_quote(original_to_utf8(d.manager_name)) << ",\"runtime_club\":" << d.runtime_club
        << ",\"national_team\":" << (d.national_team ? "true" : "false")
        << ",\"national_coach_takeover\":" << (d.national_coach_takeover ? "true" : "false")
        << ",\"manager_teams\":[";
    for (size_t i = 0; i < d.manager_teams.size(); ++i) {
        if (i) out << ',';
        out << d.manager_teams[i];
    }
    out << "],\"managers\":[";
    for (size_t i = 0; i < d.managers.size(); ++i) {
        if (i) out << ',';
        const auto& manager = d.managers[i];
        out << "{\"slot\":" << i << ",\"runtime_club\":" << manager.runtime_club
            << ",\"club_identity_key\":" << manager.club_identity_key
            << ",\"name\":" << json_quote(original_to_utf8(manager.name)) << ",\"league\":" << manager.league
            << ",\"portrait\":" << manager.portrait << ",\"badge\":" << manager.badge << '}';
    }
    out << "]"
        << ",\"club_name\":" << json_quote(original_to_utf8(d.club_name)) << ",\"date\":{\"year\":" << d.date.year
        << ",\"month\":" << d.date.month << ",\"day\":" << d.date.day << "},\"day_counter\":" << d.day_counter
        << ",\"season_index\":" << d.season_index << ",\"season_day\":" << d.season_day << ",\"league_matchday\":null"
        << ",\"balance\":" << d.balance << ",\"savings\":" << d.savings << ",\"table_position\":";
    if (d.table_position) out << *d.table_position; else out << "null";
    out << ",\"strength\":";
    if (d.strength) out << *d.strength; else out << "null";
    // The desk title index of DayCompetitions2 (next_competition): 33 = no competition today
    // ("Saisonbeginn" on the very first day), 34 = competitions without the own club, 32 = the
    // season-end desk, any other value = a match of the own club (issue #9).
    const char* phase = "matchday";
    if (!d.next_competition) phase = "unknown";
    else if (*d.next_competition == 32) phase = "season_end";
    else if (*d.next_competition == 33) phase = d.day_counter == 0 ? "season_begin" : "free_day";
    else if (*d.next_competition == 34) phase = "free_day";
    out << ",\"phase\":" << json_quote(phase) << ",\"next_competition\":";
    if (d.next_competition) out << *d.next_competition; else out << "null";
    out << ",\"next_opponent\":";
    if (d.next_opponent) out << *d.next_opponent; else out << "null";
    out << ",\"next_match_away\":" << (d.next_match_away ? "true" : "false")
        << ",\"next_competition_label\":" << json_quote(original_to_utf8(d.next_competition_label))
        << ",\"next_competition_detail\":" << json_quote(original_to_utf8(d.next_competition_detail))
        << ",\"next_opponent_name\":" << json_quote(original_to_utf8(d.next_opponent_name))
        << ",\"next_opponent_badge\":";
    if (d.next_opponent_badge) out << *d.next_opponent_badge; else out << "null";
    out << ",\"next_opponent_badge_fallback\":" << (d.next_opponent_badge_fallback ? "true" : "false");
    out << ",\"warnings\":[";
    for (size_t i = 0; i < d.warnings.size(); ++i) {
        if (i) out << ',';
        if (d.warnings[i]) out << (*d.warnings[i] ? "true" : "false"); else out << "null";
    }
    out << "],\"shortcut_bar_always\":" << (d.shortcut_bar_always ? "true" : "false")
        << ",\"mailbox_notification_pending\":" << (d.mailbox_notification_pending ? "true" : "false")
        << ",\"mailbox_shortcut_item\":";
    if (d.mailbox_shortcut) out << d.mailbox_shortcut->id; else out << "null";
    out << ",\"menu_layout_shortcut_item\":";
    if (d.menu_layout_shortcut) out << d.menu_layout_shortcut->id; else out << "null";
    out << ",\"layout\":[";
    for (size_t i = 0; i < d.items.size(); ++i) {
        if (i) out << ',';
        const auto& item = d.items[i];
        out << "{\"id\":" << item.id << ",\"kind\":" << item.kind << ",\"x\":" << item.x << ",\"y\":" << item.y
            << ",\"open\":" << (item.open ? "true" : "false") << ",\"disabled\":" << (item.disabled ? "true" : "false") << '}';
    }
    out << "],\"engine_attached\":false}";
    return out.str();
}
} // namespace bmh::ui
