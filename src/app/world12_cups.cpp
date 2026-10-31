// SPDX-License-Identifier: GPL-3.0-only
#include "app/world12_cups.hpp"
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace bmh::app::world12 {
using namespace bmh::world12;
static_assert(std::is_nothrow_move_assignable_v<CareerSnapshot>);
std::vector<CupDrawNotice> install_domestic_cup_fields(CareerSnapshot& state,
    const CountryCupRanking& german,const CountryCupRanking& english,const CupSources& sources) {
    state.validate();
    if(!state.cups.empty() || (state.calendar && state.calendar->next_day!=0) ||
        std::any_of(state.league_rounds.begin(),state.league_rounds.end(),[](auto round){return round!=0;}))
        throw std::invalid_argument("world12: initial cup fields require an unstarted season");
    auto candidate=state;GameRandom game(state.game_seed);
    if(candidate.world.catalogue.scope.germany)candidate.cups=initial_domestic_cup_fields(candidate.world,Country::germany,german,sources.german,game);
    if(candidate.world.catalogue.scope.england) {
        auto english_fields=initial_domestic_cup_fields(candidate.world,Country::england,english,sources.english,game);
        candidate.cups.insert(candidate.cups.end(),english_fields.begin(),english_fields.end());
    }
    candidate.game_seed=game.seed();candidate.validate();
    std::vector<CupDrawNotice> notices;
    for(const auto& cup:candidate.cups)if(auto notice=cup_draw_notice(candidate.world,cup))notices.push_back(std::move(*notice));
    state=std::move(candidate);return notices;
}
DayPresentation commit_day_with_domestic_draws(CareerSnapshot& state,const DayTicket& ticket,
    const std::vector<CompletedLeagueRound>& leagues,const std::vector<CupScores>& scores,const CupSources& sources) {
    const auto expected=prepare_day(state);
    if(!expected || *expected!=ticket || scores.size()!=ticket.cups.size())
        throw std::invalid_argument("world12: stale or incomplete cup day score input");
    auto candidate=state;WatcomRandom random(state.watcom_seed);
    std::vector<CupLegResult> completed;
    for(size_t i=0;i<scores.size();++i) {
        const auto& score=scores[i];const auto& plan=ticket.cups[i];
        if(score.cup!=plan.cup || score.source_entry!=plan.source_entry || score.revision!=plan.revision)
            throw std::invalid_argument("world12: cup day score input belongs to another stage");
        const auto cup=std::find_if(candidate.cups.rbegin(),candidate.cups.rend(),[&](const auto& stage){return stage.cup==score.cup;});
        const auto& source=cup_country(score.cup)==Country::germany?sources.german:sources.english;
        completed.push_back(draw_after_cup_leg(candidate.world,*cup,score.scores,source,random));
    }
    auto notices=commit_day(candidate,ticket,leagues,completed);
    candidate.watcom_seed=random.seed();candidate.validate();state=std::move(candidate);return notices;
}
}
