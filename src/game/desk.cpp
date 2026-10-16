// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/desk.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "game/matchday.hpp"
#include "game/messages.hpp"
#include "game/prediction.hpp"
#include "game/schedule.hpp"
#include "game/week.hpp"

namespace bmh {

namespace {

constexpr size_t kRecord = 0xB0C;

// DeskRuntimeField: which of the 0x36 desk buttons exist, for the pseudo club 0x165 and per
// game mode (GameMode; the table for mode m starts at DeskRuntimeField + m * 0x36).
constexpr uint8_t kButtons[0xa2] = {
    0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1,
    1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1,
    0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1,
    1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0};

int16_t rd16(const std::vector<uint8_t>& v, size_t o) { return int16_t(v.at(o) | v.at(o + 1) << 8); }

}  // namespace

void update_prestige(GameState& g, int manager) {
    static const uint8_t kWeight[4] = {25, 50, 15, 10};  // DeskRuntimeField2
    std::vector<uint8_t>& r = g.manager_records;
    const size_t rec = size_t(manager) * kRecord;
    r.at(rec + 0x211) = 0;
    for (int k = 0; k < 4; k++) {
        int value = 0;  // local_c
        switch (k) {
            case 0: value = (rd16(r, rec + 0x8b2) * 100) / 0x44c; break;
            case 1: value = (rd16(r, rec + 0x8aa) * 100) / 0x44c; break;
            case 2: {
                const int32_t balance = int32_t(r.at(rec + 0x314) | r.at(rec + 0x315) << 8 | r.at(rec + 0x316) << 16 |
                                                uint32_t(r.at(rec + 0x317)) << 24);
                value = std::clamp(balance / 50000 + 0x32, 0, 100);
                break;
            }
            case 3: value = r.at(rec + 0x26f); break;
        }
        r.at(rec + 0x211) = uint8_t(r.at(rec + 0x211) + int8_t((kWeight[k] * value) / 100));
    }
}

void save_game_draws(WatcomRandom& rand) {
    const int high = rand.range(0, 0x8fff), low = rand.range(0, 0x8fff);
    (void)high, (void)low;  // the savegame id high * layout field + low + 1 (DeskRuntimeField3, list ab790)
    rand.range(0, 0xff);    // DeskRuntimeField4 = r + 'n', the check byte at the end of the file
    rand.range(0, 0xff);    // the key byte of the header
}

void enter_desk(GameState& g, int manager, GameRandom& game, WatcomRandom& rand,
                const DeskAutosaveHook& autosave) {
    const int team = g.manager_teams.at(size_t(manager));
    // HasNoEquivalentCondition: button states (block Desktops, 0x10 bytes per button, 0x360 per manager)
    const int mode = g.memory.has(::bmh::layout::GameMode) ? g.memory.u8(::bmh::layout::GameMode) : 0;
    const int mode2 = g.memory.has(::bmh::layout::CareerDuration) ? g.memory.u8(::bmh::layout::CareerDuration) : 0;
    if (g.memory.has(::bmh::layout::Desktops))
        for (int b = 1; b < 0x36; b++) {
            const uint32_t at = ::bmh::layout::Desktops + uint32_t(b) * 0x10 + uint32_t(manager) * 0x360 + 1;
            uint8_t v = uint8_t(g.memory.u8(at) & 0xf);
            // EN AlwaysHidesRecordIndoorTableIncluding always hides record 10 (IndoorTable), including
            // Modus III. DE HasNoEquivalentCondition has no equivalent condition.
            if ((kEnglishCompetitions && b == 10) ||
                (team == kNoTeam && kButtons[b] == 0) || (mode < 3 && kButtons[mode * 0x36 + b] == 0) ||
                (b == 0x16 && mode2 == 1 && g.date < 0x16e))
                v |= 0x10;
            g.memory.set8(at, v);
        }
    stir_random(game, rand);  // StirRandom
    if (team != kNoTeam) {
        std::array<uint8_t, 24> buf;
        table_prediction(g, team, 1, buf);
    }
    // The autosave: at the first manager's desk a new month code (CareerSession_PairingsRuntimeDraw_ExecutableField5) counts one
    // more month (DeskRuntimeField5); every Desk_EnterDesk_RuntimeField months (a setting, byte 0x61 of block
    // LeaguePresentationOptions, 0 = off) the game saves itself as AUTOSAVE (MakeSavegame)
    if (manager == 0) {
        const int month = month_code(g.date);
        if (g.autosave_month != month) g.autosave_month = month, g.autosave_months++;
    }
    const int period = g.memory.has(::bmh::layout::LeaguePresentationOptions) ? g.memory.u8(::bmh::layout::Desk_EnterDesk_RuntimeField) : 0;
    if (period != 0 && g.autosave_months != 0 && g.autosave_months % period == 0) {
        if (autosave) autosave(g, rand);
        else save_game_draws(rand); // unpresented/headless legacy model
        g.autosave_months = 0;
    }
    // (the warnings of Fill are display only)
    update_prestige(g, manager);
    // At the "Saisonende" desk (CareerSession_RuntimeError_RuntimeField4): the main sponsor's minimal goal (record
    // +0x176, a table place). Missed: "Ihnen entgehen Sponsorgelder, da Sie das Minimalziel
    // (<goal>. Platz) nicht erreicht haben." Reached: (4 - league) * 50000 per place better
    // than the goal plus one (the third divisions count as the second), booked as income.
    std::vector<uint8_t>& r = g.manager_records;
    const size_t rec = size_t(manager) * 0xB0C;
    const int goal = r.at(rec + 0x176);
    if (g.memory.has(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) && g.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4) != 0 && goal != 0) {
        const int place = int16_t(table_position(g, team)) + 1;  // TablePosition
        std::string text;
        if (goal < place) {
            text = message_text(g, ::bmh::layout::Desk_SaveGameDraws_TextPointer5) + format_number(g, goal, 1) + message_text(g, ::bmh::layout::Desk_SaveGameDraws_TextPointer6);
        } else {
            int league = int8_t(r.at(rec + 0x210));  // CareerSession_BeginFriendlyOpponentTable_RuntimeField2
            if (league > 2) league--;
            const int32_t amount = (4 - league) * 50000 * (goal - place + 1);
            book_money(g, manager, -amount, true, true);  // BookMoney2 with FinanceStatisticsEnabled
            text = message_text(g, ::bmh::layout::Desk_SaveGameDraws_TextPointer2) + format_number(g, amount, 0x101) + message_text(g, ::bmh::layout::Desk_SaveGameDraws_TextPointer) +
                   format_number(g, place, 1) + message_text(g, ::bmh::layout::Desk_SaveGameDraws_TextPointer3) + format_number(g, goal, 1) +
                   message_text(g, ::bmh::layout::Desk_SaveGameDraws_TextPointer4);
        }
        post_composed(g, manager, text);
        r.at(rec + 0x176) = 0;
    }
}

}  // namespace bmh
