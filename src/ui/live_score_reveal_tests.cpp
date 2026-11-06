// SPDX-License-Identifier: GPL-3.0-only
// Tests for LiveScoreReveal keeping preview scores until the result hook (ui/live_matchday.hpp).
#include "ui/live_matchday.hpp"
#include <stdexcept>
using namespace bmh::ui;
int main() {
    LiveMatchdayFrame frame;
    frame.minute=23;
    LiveMatchRow home;home.home_team=10;home.away_team=11;home.score_known=true;
    home.home_goals=2;home.away_goals=1;frame.matches.push_back(home);
    LiveMatchRow other=home;other.home_team=20;other.away_team=21;frame.matches.push_back(other);
    LiveScoreReveal reveal;
    if(reveal.project(frame)!=frame)throw std::runtime_error("idle reveal changed scores");
    for (int side : {0,1}) {
        reveal.observe(bmh::LiveChanceStage::preview,frame);
        auto resolved=frame;
        (side==0?resolved.matches[0].home_goals:resolved.matches[0].away_goals)++;
        resolved.matches[0].spectators=12500;
        // Animation frames can be rendered repeatedly without committing scores.
        for(int animation=0;animation<18;++animation) {
            const auto shown=reveal.project(resolved);
            if(shown.matches[0].home_goals!=2 || shown.matches[0].away_goals!=1 ||
               shown.matches[0].spectators!=12500 || shown.matches[1]!=resolved.matches[1])
                throw std::runtime_error("animation leaked score or replaced unrelated fields");
        }
        reveal.observe(bmh::LiveChanceStage::result,resolved);
        if(reveal.project(resolved)!=resolved)throw std::runtime_error("result did not reveal goal");
    }
    reveal.observe(bmh::LiveChanceStage::preview,frame);
    if(reveal.project(frame)!=frame)throw std::runtime_error("miss changed score");
    auto next=frame;next.minute++;
    if(reveal.project(next)!=next)throw std::runtime_error("old chance held a later minute");
    // Team identity, rather than row index, survives a reordered board.
    auto reversed=frame;std::swap(reversed.matches[0],reversed.matches[1]);
    reversed.matches[1].away_goals++;
    if(reveal.project(reversed)!=LiveMatchdayFrame{frame.minute,frame.minute_text,frame.gradient_text,
        {frame.matches[1],frame.matches[0]}})throw std::runtime_error("reordered board used wrong score");
}
