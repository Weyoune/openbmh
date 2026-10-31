// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/standings.hpp"
#include "game/schedule.hpp"
#include "game/table.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using namespace bmh::world12;
void check(bool value,const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function,const char* message) {
    bool rejected=false;
    try { function(); } catch (const std::exception&) { rejected=true; }
    check(rejected,message);
}
World world_fixture() {
    World world;
    uint32_t identity=100000;
    for (const auto& league:world.catalogue.leagues) {
        auto& members=world.membership[league.id.value]; members.league=league.id;
        for (int i=0;i<league.clubs;++i) {
            ClubRecord club;
            club.id={identity++}; club.country=league.country; club.league=league.id;
            // Reverse name order deliberately: ties must follow Classic's draw order.
            club.name="Club "+std::to_string(999999-club.id.value);
            members.clubs.push_back(club.id); world.clubs.emplace(club.id,club);
        }
    }
    return world;
}
void install_fixture_tables() {
    bmh::FixtureTableCache tables;
    for (int count:{18,20}) {
        std::vector<int> ring(size_t(count),0);
        std::iota(ring.begin(),ring.end(),0);
        for (int round=0;round<count-1;++round) {
            std::string order;
            for (int match=0;match<count/2;++match) {
                order+=char('a'+ring[size_t(match)]);
                order+=char('a'+ring[size_t(count-1-match)]);
            }
            tables[count].push_back(order);
            const int last=ring.back();
            for (int i=count-1;i>1;--i) ring[size_t(i)]=ring[size_t(i-1)];
            ring[1]=last;
        }
    }
    bmh::set_fixture_tables(std::move(tables));
}
}

int main() try {
    using namespace bmh::world12;
    install_fixture_tables();
    const auto world=world_fixture();
    int comparisons=0;
    const uint8_t original_size=bmh::kLeagueSizes[0];
    for (const auto& league:world.catalogue.leagues) {
        if (league.clubs==10) continue; // Newly scheduled group has no legacy table.
        bmh::kLeagueSizes[0]=league.clubs;
        const auto& members=world.membership[league.id.value].clubs;
        for (int played:{0,1,2,league.rounds()/2,league.rounds()}) {
            auto results=SeasonResults::empty(world.catalogue);
            bmh::GameState legacy;
            legacy.results.assign(9*0x17c,0xcc);
            legacy.league_rounds.assign(9,0); legacy.league_rounds[0]=uint8_t(played);
            for (int round=1;round<=played;++round) for (int match=0;match<league.clubs/2;++match) {
                auto& score=results.result(world.catalogue,league.id,round,match);
                score={true,uint8_t((round+match)%4),uint8_t((round+match*2)%3)};
                legacy.results[size_t(round-1)*10+size_t(match)]=uint8_t(score.home<<4 | score.away);
            }
            for (const auto venue:{TableVenue::all,TableVenue::home,TableVenue::away})
                for (bool three_points:{false,true}) for (uint8_t suppressed:{uint8_t(0),uint8_t(2),uint8_t(7)}) {
                    TableQuery query; query.venue=venue; query.three_points=three_points; query.suppressed_parts=suppressed;
                    const auto actual=compute_standings(world,results,league.id,played,query);
                    const int flags=venue==TableVenue::home ? 1 : venue==TableVenue::away ? 2 : 3;
                    const auto expected=bmh::compute_league_table(legacy,0,0,league.rounds(),flags,
                        (int(suppressed)<<1)|int(three_points));
                    for (const auto& row:actual) {
                        const auto found=std::find(members.begin(),members.end(),row.club);
                        check(found!=members.end(),"global identity replaced by local table position");
                        const auto i=size_t(found-members.begin());
                        check(row.rank==expected.rank(int(i))+1 && row.previous_rank==expected.previous_rank(int(i))+1,
                            "Classic current/previous ranking changed");
                        check(row.games==expected.games(int(i)) && row.wins==expected.wins[i] && row.defeats==expected.defeats[i] &&
                            row.draws==row.games-row.wins-row.defeats && row.goals_for==expected.goals_for[i] &&
                            row.goals_against==expected.goals_against[i] && row.points==expected.points[i] && row.form==expected.form[i],
                            "Classic points, goals or form calculation changed");
                    }
                    ++comparisons;
                }
            TableQuery second_half; second_half.first_round=league.clubs;
            const auto actual=compute_standings(world,results,league.id,played,second_half);
            const auto expected=bmh::compute_league_table(legacy,0,league.clubs-1,league.rounds(),3,1);
            for (const auto& row:actual) {
                const int index=int(std::find(members.begin(),members.end(),row.club)-members.begin());
                check(row.games==expected.games(index) && row.points==expected.points[size_t(index)] &&
                    row.rank==expected.rank(index)+1,"second-half filtering changed");
            }
        }
    }
    bmh::kLeagueSizes[0]=original_size;

    auto results=SeasonResults::empty(world.catalogue);
    const LeagueId fifth{9};
    for (int round=1;round<=18;++round) for (int match=0;match<5;++match)
        results.result(world.catalogue,fifth,round,match)={true,2,1};
    const auto rows=compute_standings(world,results,fifth,18);
    int wins=0,goals=0,points=0;
    for (const auto& row:rows) {
        check(row.club.value>=100000 && row.games==18 && row.wins==9 && row.defeats==9 && row.draws==0,
            "ten-team group is not a complete balanced home/away season");
        check(row.goals_for==27 && row.goals_against==27 && row.points==27,"ten-team totals incorrect");
        wins+=row.wins; goals+=row.goals_for; points+=row.points;
    }
    check(rows.size()==10 && wins==90 && goals==270 && points==270,"ten-team season totals lost");
    TableQuery deduction; deduction.point_deductions[rows[0].club]=5;
    const auto penalized=compute_standings(world,results,fifth,18,deduction);
    check(penalized.back().club==rows[0].club && penalized.back().points==22,"penalty assigned to another club");
    TableQuery away; away.venue=TableVenue::away;
    for (const auto& row:compute_standings(world,results,fifth,18,away))
        check(row.games==9 && row.points==0 && row.goals_for==9,"away filter counts home matches");
    auto high_draw=results;
    high_draw.result(world.catalogue,fifth,1,0)={true,12,12};
    const auto drawn_match=league_fixtures(world,fifth,1)[0];
    for (const auto& row:compute_standings(world,high_draw,fifth,18))
        if (row.club==drawn_match.home || row.club==drawn_match.away)
            check(row.games==18 && row.draws==1,"12:12 confused with the old unplayed byte sentinel");
    auto invalid=results; invalid.result(world.catalogue,fifth,1,0).home=16;
    rejects([&]{compute_standings(world,invalid,fifth,18);},"unrepresentable score accepted");
    auto malformed=world; malformed.membership[9].clubs[0]=world.membership[5].clubs[0];
    rejects([&]{compute_standings(malformed,results,fifth,18);},"cross-league identity accepted");
    TableQuery bad; bad.point_deductions[{0}]=1;
    rejects([&]{compute_standings(world,results,fifth,18,bad);},"foreign club penalty accepted");
    bad={}; bad.first_round=0;
    rejects([&]{compute_standings(world,results,fifth,18,bad);},"invalid round range accepted");
    rejects([&]{compute_standings(world,results,fifth,19);},"foreign round limit accepted");
    std::cout<<comparisons<<" Classic table comparisons and complete ten-team standings passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
