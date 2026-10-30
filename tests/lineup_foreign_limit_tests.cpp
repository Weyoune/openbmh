// SPDX-License-Identifier: GPL-3.0-only
#include "game/lineup.hpp"
#include "game/match.hpp"
#include "game/weekly.hpp"
#include "formats/le.hpp"
#include "formats/executable_layout.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void enabled(bool value) {
    auto exe = bmh::LeImage::from_bytes(bmh::layout::ForeignPlayerLimitEnabled, bmh::Bytes{uint8_t(value)});
    bmh::install_lineup_rules(exe);
}
bmh::GameState fixture(int team, bool human = false, bool all_foreign = false) {
    bmh::GameState g;
    const auto range = bmh::squad_range(team);
    g.squad_slots.assign(3096, 0xfff);
    g.players.resize(range.count); g.player_bonus.resize(range.count); g.player_cards.resize(range.count, 0x9a00);
    g.team_flags.resize(138); g.formations.resize(0x160);
    g.formations[0] = 0;
    for (int p = 1; p < 11; ++p) g.formations[size_t(p)] = 7;
    const int country = bmh::team_region(team);
    for (size_t i = 0; i < range.count; ++i) {
        g.squad_slots[range.first+i] = uint16_t(i);
        auto& player = g.players[i];
        const int strength = i < 6 ? 95 : 60;
        player.w35c = 60u | uint32_t(strength) << 7 | uint32_t(strength) << 14;
        player.w364 = i == 0 || i == 11 ? 0 : 1;
        player.w368 = uint32_t(i < 6 || all_foreign ? country + 1 : country) << 27;
    }
    if (human) {
        g.manager_teams = {team}; g.manager_options = {1,0,0,0,0};
        g.manager_records.resize(0xb0c); g.tactics.resize(0x840);
        for (size_t i = 0; i < range.count; ++i) g.tactics[i*0x58+0x50] = uint8_t(i+1);
    }
    return g;
}
int foreign_starters(const bmh::GameState& g, int team) {
    int count = 0; const auto r = bmh::squad_range(team);
    for (int row = 0; row < 11; ++row) {
        const auto entry = g.squad_slots[r.first+size_t(row)];
        if ((entry & 0x8000) && bmh::nationality(g, entry & 0xfff) != bmh::team_region(team)) ++count;
    }
    return count;
}
}
int main() try {
    enabled(false); auto unrestricted = fixture(0); bmh::auto_lineup(unrestricted,0,100);
    check(foreign_starters(unrestricted,0)==6,"disabled edition changed foreign selection");
    enabled(true);
    for (int team : {0,90}) {
        auto g = fixture(team); bmh::auto_lineup(g,team,100);
        check(foreign_starters(g,team)==(team==0?3:2),"original domestic/foreign league limit not enforced");
    }
    auto human = fixture(0,true); bmh::auto_lineup(human,0,100);
    check(foreign_starters(human,0)==3,"human assistant ignored original limit");
    for (int row=0;row<24;++row) {
        const auto entry=human.squad_slots[size_t(row)];const size_t id=entry&0xfff;
        check(human.tactics[size_t(row)*0x58+0x50]==id+1,"player salary board lost during foreign swap");
        if(entry&0xc000)check((human.player_cards[id]&31)==row,"card row index not updated after foreign swap");
        if(row<11)check(human.tactics[size_t(row)*0x58+0x41]==(row?7:0),"formation coordinate moved with replaced player");
    }
    auto shortage = fixture(0,false,true); bmh::auto_lineup(shortage,0,100);
    check(foreign_starters(shortage,0)==3,"shortage of domestic reserves did not terminate with a legal partial lineup");
    enabled(false); auto restored=fixture(0);bmh::auto_lineup(restored,0,100);
    check(restored.squad_slots==unrestricted.squad_slots && restored.player_cards==unrestricted.player_cards,
          "switching back to disabled edition did not restore unchanged selection");
    std::cout<<"Original foreign lineup limits, board swaps and edition switching passed\n";
    return 0;
} catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
