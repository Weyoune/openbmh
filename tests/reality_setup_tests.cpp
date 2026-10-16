// SPDX-License-Identifier: GPL-3.0-only
#include <cstdio>
#include <stdexcept>

#include "game/reality_setup.hpp"

using namespace bmh;
namespace {
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void put16(Bytes& bytes,size_t at,uint16_t value) {
    bytes.at(at)=uint8_t(value);bytes.at(at+1)=uint8_t(value>>8);
}
GameState fixture() {
    GameState state;
    state.squad_slots.resize(48,kEmptySlot);state.players.resize(22);state.player_cards.resize(22);
    state.team_flags.resize(2);state.formations.resize(0x580);
    for (int side = 0; side < 2; ++side) for (int row = 0; row < 11; ++row) {
        const int player = side*11+row;
        state.squad_slots[size_t(side*24+row)] = uint16_t(kSlotInLineup|player);
        state.player_cards[size_t(player)] = uint16_t(0xa000+player);
        state.formations[size_t(row)] = row ? 12 : 0;
        state.formations[size_t(row+11)] = uint8_t(row ? row%7+1 : 4);
    }
    return state;
}
void human(GameState& state,int team) {
    state.manager_teams = {team};state.manager_records.resize(0xb0c);state.tactics.resize(0x840);
    for (int row = 0; row < 24; ++row) {
        const size_t at = size_t(row)*0x58;
        state.tactics[at+2] = 19;state.tactics[at+3] = 25;state.tactics[at+4] = 50;
        state.tactics[at+0xf] = 50;
        state.tactics[at+0x41] = uint8_t(row ? 12 : 0);state.tactics[at+0x42] = uint8_t(row ? row%7+1 : 4);
        put16(state.tactics,at+0x44,0xfff);put16(state.tactics,at+0x46,0xfff);
    }
    state.manager_records[0xd8] = 1; // pressing option of the attacking manager
}
}
int main(int argc,char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: reality_setup_tests <Original data>");
        const auto exe = load_le(std::filesystem::path(argv[1])/"BMH.exe");
        const std::array<int,2> teams{0,1};
        for (int side : {0,1}) for (int seed = 0; seed < 16; ++seed) {
            auto state = fixture();const auto before = state;
            const auto formations = reality_formations(state,teams);
            WatcomRandom rand(12345+uint32_t(seed));GameRandom game(67890+seed);RealitySetup setup;
            initialize_reality(state,teams,side,formations,exe,rand,game,setup);
            check(setup.ready && game.calls() == 24,"successful initialization consumes exactly the Source 24 sprite-phase game draws");
            check(rand.calls() >= 55,"AI profiles, player placement and ball selection use the real Watcom stream");
            check(state.squad_slots == before.squad_slots && state.tactics == before.tactics &&
                  state.manager_records == before.manager_records,"AI initialization leaves saved squad/tactics records untouched");
            check(setup.grid.ball().first > 10 && setup.grid.ball().first < 30,"Source candidate ball starts in midfield");
            for (int id = 0; id < 22; ++id) {
                const auto& figure = setup.motion.figures[size_t(id)];const auto& player = setup.players[size_t(id)];
                check(figure.active && player.player == id && player.card_value == (id&31),"Source fixture identity/card metadata preserved");
                const auto point = setup.grid.position(id);
                check(setup.grid.occupant(point.first,point.second,id/11) == id+1,"every active figure owns its Source occupancy cell");
                if (id%11) check(point.first>=2 && point.first<=40 && point.second>=1 && point.second<=23,
                                "outfield rejection sampling retains Source interior bounds");
                else check(point.first == (id ? 42 : 0) && point.second == 12,"keepers retain Source endpoints without placement draws");
            }
            auto repeated = fixture();WatcomRandom wr(12345+uint32_t(seed));GameRandom gr(67890+seed);RealitySetup replay;
            initialize_reality(repeated,teams,side,formations,exe,wr,gr,replay);
            check(replay.motion == setup.motion && wr.seed() == rand.seed() && wr.calls() == rand.calls() &&
                  gr.seed() == game.seed(),"initialization repeats the same positions and exact RNG trajectory");
            const auto ws=rand.seed();const auto gs=game.seed();const auto wc=rand.calls();const auto gc=game.calls();
            const auto frames=reality_motion(setup.motion,exe);(void)frames;
            check(rand.seed()==ws && game.seed()==gs && rand.calls()==wc && game.calls()==gc,
                  "drawing the initialized scene consumes no simulation randomness");
        }
        auto state = fixture();RealitySetup reused,fresh;
        WatcomRandom first_rand(100);GameRandom first_game(200);
        initialize_reality(state,teams,0,reality_formations(state,teams),exe,first_rand,first_game,reused);
        const auto previous_ball=reused.grid.ball();
        WatcomRandom reused_rand(300),fresh_rand(300);GameRandom reused_game(400),fresh_game(400);
        initialize_reality(state,teams,1,reality_formations(state,teams),exe,reused_rand,reused_game,reused);
        initialize_reality(state,teams,1,reality_formations(state,teams),exe,fresh_rand,fresh_game,fresh);
        check(reused.motion==fresh.motion && reused_rand.seed()==fresh_rand.seed() &&
              reused_rand.calls()==fresh_rand.calls() && reused_game.seed()==fresh_game.seed(),
              "new scene clears old occupancy without changing the fresh initialization trajectory");
        check(reused.grid.previous_ball()==previous_ball,"new scene preserves the Source previous ball before placement");
        state = fixture();human(state,0);const auto before = state.tactics;
        WatcomRandom rand(12345);GameRandom game(67890);RealitySetup setup;
        initialize_reality(state,teams,0,reality_formations(state,teams),exe,rand,game,setup);
        check(setup.ready,"human source movement boxes produce a playable initial scene");
        for (size_t i=0;i<state.tactics.size();++i) {
            if (i%0x58==0xf && i/0x58<11)
                check(state.tactics[i]>=51 && state.tactics[i]<=52,"Source manager pressure/scatter update board fatigue");
            else check(state.tactics[i]==before[i],"initialization changes only Source board fatigue bytes");
        }
        // AI defenders are positioned at the attacking human's marked player.
        state=fixture();human(state,0);put16(state.tactics,0x46,11+3);
        rand=WatcomRandom(12345);game=GameRandom(67890);setup={};
        initialize_reality(state,teams,0,reality_formations(state,teams),exe,rand,game,setup);
        check(setup.grid.position(14)==setup.grid.position(0),"Source inverse human marking places the corresponding AI defender on its opponent");
        state=fixture();human(state,1);put16(state.tactics,3*0x58+0x44,0);
        rand=WatcomRandom(12345);game=GameRandom(67890);setup={};
        initialize_reality(state,teams,0,reality_formations(state,teams),exe,rand,game,setup);
        check(setup.grid.position(14)==setup.grid.position(0),"Source direct human marking uses the opponent's player identity");
        state=fixture();state.squad_slots[0]=kEmptySlot;
        rand=WatcomRandom(12345);game=GameRandom(67890);setup={};
        initialize_reality(state,teams,0,reality_formations(state,teams),exe,rand,game,setup);
        check(setup.ready && !setup.motion.figures[0].active && setup.players[1].center_x==0,
              "inactive rows do not consume the compressed Source prepared-formation cursor");
        state=fixture();human(state,0);
        for (int row=0;row<11;++row) state.tactics[size_t(row)*0x58+0xf]=99;
        rand=WatcomRandom(12345);game=GameRandom(67890);setup={};
        initialize_reality(state,teams,0,reality_formations(state,teams),exe,rand,game,setup);
        for (int row=0;row<11;++row) check(state.tactics[size_t(row)*0x58+0xf]==99,"Source fatigue clamps at99");
        // No attacking midfield candidates: Source returns before defenders or sprite phases.
        state=fixture();for (auto& slot:state.squad_slots) slot=kEmptySlot;
        state.squad_slots[0]=kSlotInLineup;rand=WatcomRandom(12345);game=GameRandom(67890);setup={};
        initialize_reality(state,teams,0,reality_formations(state,teams),exe,rand,game,setup);
        check(!setup.ready && game.calls()==0 && rand.calls()==14,"unavailable scene retains Source early-return RNG boundary");
        auto malformed=fixture();malformed.squad_slots[0]=uint16_t(kSlotInLineup|0xfff);
        rand=WatcomRandom(12345);game=GameRandom(67890);bool rejected=false;
        try { initialize_reality(malformed,teams,0,reality_formations(malformed,teams),exe,rand,game,setup); }
        catch(const FormatError&){rejected=true;}
        check(rejected && rand.calls()==0 && game.calls()==0,"invalid starting player fails before simulation writes/draws");
        std::puts("PASS: Source Reality setup, both sides, identities, RNG boundaries, movement boxes, fatigue, marking and empty scene");return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
