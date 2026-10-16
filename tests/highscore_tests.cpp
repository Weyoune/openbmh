#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "capture_io.hpp"
#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "game/highscore.hpp"
#include "game/save.hpp"
#include "game/season.hpp"
#include "game/turn_driver.hpp"

using namespace bmh;
namespace {
void require(bool v, const char* why) { if (!v) throw std::runtime_error(why); }
GameState native(const std::filesystem::path& fixture) {
    const auto file = load_savegame(fixture / "GAMEND.FBM");
    require(file.checksum_ok, "native fixture checksum");
    return GameState::from_save(SaveState(file.stream));
}
void set_record16(GameState& g, int m, size_t offset, uint16_t value) {
    const size_t at = size_t(m) * 0xb0c + offset;
    g.manager_records.at(at) = uint8_t(value);
    g.manager_records.at(at + 1) = uint8_t(value >> 8);
}
GameState post7(const std::filesystem::path& fixture) {
    auto g = native(fixture);
    const auto original_bayern = g.club_ids.at(size_t(g.manager_teams.at(0)));
    std::ifstream in(fixture / "post7.jsonl");
    std::string meta, state; std::getline(in, meta); std::getline(in, state);
    auto dump = capture::state_from_dump(state);
    g.manager_records = dump.manager_records;
    g.manager_teams = dump.manager_teams;
    g.squad_slots = dump.squad_slots;
    g.players = dump.players;
    g.team_morale = dump.team_morale;
    g.stocks = capture::unhex(capture::field(state, "stocks"));
    g.season = 1; g.date = 365;
    // Club-name offsets are not captured. This observed PNG identity is explicitly
    // restored from the native BEFORE Bayern entry after the captured club shuffle.
    g.club_ids.at(size_t(g.manager_teams.at(0))) = original_bayern;
    return g;
}
void check_original_score(const std::filesystem::path& data, const std::filesystem::path& fixture) {
    const auto exe = bmh::test::load_executable(data / "BMH.exe");
    constexpr uint8_t weights[] = {3,5,4,30,20,10,5,40,30,50};
    for (uint32_t k = 0; k < sizeof weights; ++k)
        require(exe.u8(::bmh::layout::HighscoreRuntimeField + k) == weights[k], "Source score weights changed");
    require(exe.u16(::bmh::layout::CareerSession_DispatchLineup_RuntimeField) + exe.u16(::bmh::layout::CareerSession_InstallLeagueSizes_RuntimeField6) + exe.u16(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField3) == kNoTeam,
            "national honours base changed");
    auto g = post7(fixture);
    const auto before = save_stream(g);
    const auto score = highscore_score(g, 0, HighscoreContext::SeasonBoundary);
    require(score.assets == 0 && score.patience == 50 && score.facilities == 400 &&
            score.team == 176 && score.history == 137 && score.titles == 0 && score.total == 763,
            "exact Original post7 Source score breakdown");
    // Source global ActiveManager is read independently of candidate manager. The prior
    // legacy routine manager loop leaves1; using the current port desk slot0 would give863.
    require(highscore_score(g, 0).total == 863, "global active-manager assets quirk");
    g.memory.set8(::bmh::layout::ActiveManager, 1);
    require(highscore_score(g, 0).total == 763, "Source loop-bound asset slot");
    g.memory.set8(::bmh::layout::ActiveManager, 0);
    // Source allows negative assets and clamps only the final candidate score.
    for (size_t k = 0; k < 4; ++k)
        g.manager_records[0xb0c + 0x314 + k] = uint8_t(uint32_t(-100000000) >> (k * 8));
    g.memory.set8(::bmh::layout::ActiveManager, 1);
    const auto negative = highscore_score(g, 0);
    require(negative.assets == -2000 && negative.total == 0, "negative assets/final score clamp");
    for (size_t k = 0; k < 4; ++k) g.manager_records[0xb0c + 0x314 + k] = 0;
    g.memory.set8(::bmh::layout::ActiveManager, 0);
    const auto row = highscore_candidate(g, 0, HighscoreContext::SeasonBoundary);
    std::printf("post7 candidate: score=%d manager=%s club=%s\n", row.score, row.manager_name.c_str(), row.club_name.c_str());
    require(row.score == 763 && row.manager_name == "Nobody" &&
            row.club_name == "FC BAYERN M]NCHEN" && row.titles == std::array<uint16_t,3>{},
            "Original post7 candidate identity/titles");
    auto projection = make_highscore_projection(g, {}, HighscoreContext::SeasonBoundary);
    require(projection.ranked && projection.signature == 135 && projection.rows[0].score == 763,
            "native signature135 ranked projection");
    require(save_stream(g) == before, "read-only score/projection mutated game state");
    set_record16(g, 0, 0x8a4, 2); set_record16(g, 0, 0x8a6, 3); set_record16(g, 0, 0x8a8, 4);
    require(highscore_score(g, 0, HighscoreContext::SeasonBoundary).total == 1133,
            "Source title weights40/30/50");
    // History sum must lose its upper16bits before division (legacy routine).
    g.season = 49;
    for (size_t year = 0; year < 50; ++year) {
        auto at = 0xda + year * 3;
        g.manager_records[at] = 255; g.manager_records[at+1] = 255; g.manager_records[at+2] = 255;
    }
    require(highscore_score(g, 0, HighscoreContext::SeasonBoundary).history == 694,
            "history low16 truncation before50-season averaging");
}
void check_national_and_gate(const std::filesystem::path& fixture) {
    auto g = native(fixture);
    g.manager_teams[0] = kNoTeam;
    for (uint32_t k = 0; k < 80; ++k) g.memory.set8(::bmh::layout::Placements+k, 0);
    g.memory.set8(::bmh::layout::Placements, 0x80); g.memory.set8(::bmh::layout::Placements+5, 0x80);
    g.memory.set8(::bmh::layout::Placements+10, 0x81); // another country's honour does not count.
    require(highscore_score(g, 0).total == 500, "national ranked honours weights");
    HighscoreTable old{}; old[0].score = 99; old[0].manager_name = "Old"; old[0].club_name = "Club";
    auto p = make_highscore_projection(g, old);
    require(p.rows[0].score == 99, "Source slot0-national with leagues skips ALL candidates");
    g.memory.set8(::bmh::layout::CompetitionSelection, 2);
    require(make_highscore_projection(g, old).rows[0].score == 500,
            "world-only national candidate allowed");
    g.memory.set16(::bmh::layout::CareerSession_FormatError_RuntimeField7, 0);
    p = make_highscore_projection(g, old);
    require(!p.ranked && p.rows[0].score == 99, "signature0 alternate visual gate");
    g.memory.set16(::bmh::layout::CareerSession_FormatError_RuntimeField7, 3);
    require(make_highscore_projection(g).ranked, "Source nonzero gate is not signature135 whitelist");
}
void check_storage() {
    HighscoreTable table{};
    for (size_t k = 0; k < 5; ++k) {
        table[k].score = 500 - int32_t(k)*100;
        table[k].club_name = "Club"; table[k].manager_name = "M" + std::to_string(k);
    }
    auto equal = table[2]; equal.score = 50;
    insert_highscore(table, equal);
    require(table.back().manager_name == "M2" && table.back().score == 50,
            "same manager/club must replace even lower score");
    HighscoreRow tie; tie.score = 50; tie.manager_name = "Tie"; tie.club_name = "Club";
    insert_highscore(table, tie);
    require(table.back().manager_name == "M2", "strictly greater worst-row insertion");
    auto improved = equal; improved.score = 600; improved.titles = {0x1234,2,3};
    insert_highscore(table, improved); insert_highscore(table, improved);
    require(table[0].score == 600 && table[0].manager_name == "M2", "sort and two-update idempotence");
    int count=0; for(const auto& r:table) if(r.manager_name=="M2") ++count;
    require(count==1,"duplicate Source update inserted twice");
    table[0].original_bytes[70] = 0xab; table[0].original_bytes[71] = 0xcd;
    auto bytes=encode_highscore_table(table);
    require(bytes.size()==360 && bytes[0]==0x58 && bytes[1]==2 && bytes[0x40]==0x34 &&
            bytes[0x41]==0x12 && bytes[70]==0xab && bytes[71]==0xcd,
            "72-byte Source row offsets and opaque padding");
    require(encode_highscore_table(decode_highscore_table(bytes))==bytes,"exact byte roundtrip");
    auto dir=std::filesystem::temp_directory_path()/"openbmh-highscore-source-test";
    std::filesystem::create_directories(dir/"high");
    auto file=highscore_file(dir,135);
    std::filesystem::remove(file);
    auto missing=load_highscore_table(file);
    require(missing[0].score==0 && std::filesystem::file_size(file)==360,"missing Source table creates zeros");
    require(save_highscore_table(file,table),"file write");
    require(encode_highscore_table(load_highscore_table(file))==bytes,"native row persistence");
    require(highscore_file(dir,3)!=file,"signature file partition");
    require(!save_highscore_table(dir/"missing"/"h.135",table),"unwritable file reports failure");
    std::filesystem::remove(file); std::filesystem::remove(dir/"high"); std::filesystem::remove(dir);
}
void check_terminal(const std::filesystem::path& fixture) {
    auto g = native(fixture);
    require(g.date==365 && g.season==0 && g.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4)==1 && g.memory.u8(::bmh::layout::CareerDuration)==1,
            "native completed-season cursor");
    TurnDriver driver; driver.stage=TurnDriver::Stage::SeasonEndDesk;
    driver.revision=1; driver.session_id=1;
    std::vector<bool> display;
    driver.season_highscore=[&](const GameState& state,bool show) {
        require(state.date==365 && state.season==1,"highscore reached before opening date advance");
        require(state.memory.u8(::bmh::layout::CareerDuration)==(show?0:1),"Source highscore before/after duration decrement");
        const auto before=save_stream(state);
        require(make_highscore_projection(state,{},HighscoreContext::SeasonBoundary).ranked,"highscore checkpoint");
        require(save_stream(state)==before,"highscore callback mutation");
        display.push_back(show);
    };
    GameRandom game(665367); WatcomRandom rand(589388519); MinuteReport report;
    const auto out=advance_to_next_desk(driver,g,{0,1},game,rand,report);
    require(!report.unsupported && out.kind==TurnDeskKind::CareerComplete &&
            driver.stage==TurnDriver::Stage::CareerComplete,"duration1 is normal completion");
    require(g.date==365 && g.season==1 && g.memory.u8(::bmh::layout::CareerDuration)==0 && g.memory.u8(::bmh::layout::CareerSession_RuntimeError_RuntimeField4)==0 &&
            g.season_start==0 && out.revision==2,"terminal date/counter/revision retained");
    require(display==std::vector<bool>{false,true},"Source two highscore calls");
    const auto settled=save_stream(g); const auto gs=game.seed(); const auto ws=rand.seed();
    const auto gc=game.calls(); const auto wc=rand.calls();
    MinuteReport replay;
    require(advance_to_next_desk(driver,g,{0,2},game,rand,replay).kind==TurnDeskKind::Failed,
            "terminal cursor rejects another advance");
    require(driver.stage==TurnDriver::Stage::CareerComplete && save_stream(g)==settled &&
            game.seed()==gs && rand.seed()==ws && game.calls()==gc && rand.calls()==wc,
            "terminal retry is inert");
    MinuteReport duplicate;
    require(season_transition(g,game,rand,duplicate)==SeasonTransitionOutcome::Failed &&
            save_stream(g)==settled && game.calls()==gc && rand.calls()==wc,
            "completed direct transition rejected before mutation");
    for(uint8_t duration:{uint8_t(2),uint8_t(3),uint8_t(4),uint8_t(5)}) {
        auto next=native(fixture); next.memory.set8(::bmh::layout::CareerDuration,duration);
        GameRandom ng(665367); WatcomRandom nw(589388519); MinuteReport nr;
        std::vector<bool> events;
        const auto result=season_transition(next,ng,nw,nr,{},[&](const GameState&,bool show){events.push_back(show);});
        require(result==SeasonTransitionOutcome::NextSeason && !nr.unsupported && next.date==371 &&
                next.season_start==371 && next.memory.u8(::bmh::layout::CareerDuration)==(duration==5?5:duration-1),
                "durations2..5 preserve first-Wednesday continuation");
        require(events==std::vector<bool>{false},"continuing season only updates table");
    }
}
} // namespace
int main(int argc,char**argv) {
    if(argc!=3)return 2;
    if(!*argv[1]) {std::puts("highscore Source tests: skipped (Original data directory not configured)"); return 77;}
    try {
        check_original_score(argv[1],argv[2]); check_national_and_gate(argv[2]); check_storage(); check_terminal(argv[2]);
        std::puts("highscore Source tests: Original763, native rows/gates, terminal/durations OK"); return 0;
    } catch(const std::exception& e) {std::printf("FAIL: %s\n",e.what());return 1;}
}
