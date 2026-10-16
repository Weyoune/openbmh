// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// Development helper: prints a club's squad from a savegame (player, age, words), or with
// "p<id>" every squad slot that holds that player.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "formats/fbm.hpp"
#include "formats/savestate.hpp"
#include "game/state.hpp"

using namespace bmh;

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    Savegame sg = load_savegame(argv[1]);
    SaveState st(sg.stream);
    GameState g = GameState::from_save(st);
    const std::string arg = argv[2];
    if (arg == "m") {
        // the mailbox: recipients, date and text of each entry
        for (size_t k = 0; k * 0xA0 + 0xA0 <= g.mailbox.size(); k++) {
            const size_t e = k * 0xA0;
            std::string text;
            for (size_t i = 10; i < 0xA0 && g.mailbox[e + i]; i++) text.push_back(char(g.mailbox[e + i]));
            if (text.empty()) continue;
            std::printf("%2zu to %02x%02x%02x%02x date %u: %s\n", k, g.mailbox[e + 3], g.mailbox[e + 2], g.mailbox[e + 1],
                        g.mailbox[e], g.mailbox[e + 8] << 8 | g.mailbox[e + 9], text.c_str());
        }
        return 0;
    }
    if (arg == "t") {
        // the indoor tournament's name (heap text)
        for (uint8_t c : g.tournament_name) {
            if (c == 0) break;
            std::putchar(c);
        }
        std::printf(" (%zu bytes)\n", g.tournament_name.size());
        return 0;
    }
    if (arg == "o") {
        // the pre-match offer slots (format field) and their transfer list entries 40..43
        for (uint32_t k = 0; k < 4; k++) {
            const uint32_t v = g.memory.u32(::bmh::layout::OfferSlots + k * 4);
            const uint32_t e = ::bmh::layout::EditorList + (0x28 + k) * 0x58;
            const int player = g.squad_slots[0xb70 + 0x28 + k] & 0xfff;
            std::printf("slot %u: %08x pool player %d %s club %04x bidders %04x %04x %04x %04x fee %u salary %u"
                        " bid salary %u bid fee %u\n",
                        k, v, player, player < 0xfff ? g.player_name(player).c_str() : "-", g.memory.u16(e + 8),
                        g.memory.u16(e), g.memory.u16(e + 2), g.memory.u16(e + 4), g.memory.u16(e + 6),
                        g.memory.u32(e + 0x2c), g.memory.u32(e + 0x30), g.memory.u32(e + 0xc), g.memory.u32(e + 0x1c));
            const int bidder = g.memory.u16(e);
            if (bidder < 0x200) std::printf("  bidder club: %s\n", g.club_name(bidder).c_str());
        }
        return 0;
    }
    if (arg[0] == 'p') {
        const int want = std::atoi(arg.c_str() + 1);
        for (size_t s = 0; s < g.squad_slots.size(); s++)
            if ((g.squad_slots[s] & 0xfff) == want) std::printf("slot %zu value %04x\n", s, g.squad_slots[s]);
        std::printf("w364 %08x w35c %08x w368 %08x\n", g.players[size_t(want)].w364, g.players[size_t(want)].w35c,
                    g.players[size_t(want)].w368);
        for (uint32_t k = 0; k < 200; k++) {
            const uint32_t e = g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField + k * 4);
            if (e != 0 && int(e & 0xfff) == want) std::printf("loan entry %u: %08x\n", k, e);
        }
        return 0;
    }
    const SquadRange r = squad_range(std::atoi(argv[2]));
    for (size_t s = r.first; s < r.first + r.count; s++) {
        const int p = g.squad_slots[s] & 0xfff;
        if (p == 0xfff) continue;
        std::printf("%zu: %4d age %2u w364 %08x w35c %08x %s\n", s, p, (g.players[size_t(p)].w364 >> 21) & 0x1f,
                    g.players[size_t(p)].w364, g.players[size_t(p)].w35c, g.player_name(p).c_str());
    }
}
