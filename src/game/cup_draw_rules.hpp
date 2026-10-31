// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "game/random.hpp"
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace bmh {
// PrepareEvent: one game-generator draw per position, not Fisher-Yates.
template<class Swap> void initial_cup_shuffle(int count,GameRandom& game,Swap swap) {
    if(count<0 || count>255)throw std::invalid_argument("cup: invalid initial shuffle size");
    for(int i=0;i<count;++i) {
        const int j=game.range(0,count-1);
        if(j<0 || j>=count)throw std::invalid_argument("cup: game RNG produced an invalid shuffle index");
        swap(i,j);
    }
}
// AdvanceCup: two Watcom draws per entrant, with both indices random.
template<class Swap> void subsequent_cup_shuffle(int count,WatcomRandom& random,Swap swap) {
    if(count<0 || count>255)throw std::invalid_argument("cup: invalid subsequent shuffle size");
    for(int i=0;i<count;++i) {
        const int a=random.range(0,count-1),b=random.range(0,count-1);
        swap(b,a);
    }
}
inline uint8_t cup_league_weight(int league) {
    constexpr uint8_t weights[]={3,1,0,0,2,0,0,0,7,0};
    if(league<0)throw std::invalid_argument("cup: negative league classification");
    return weights[league<10?league:9];
}
struct CupDrawClass {bool lower_division=false;uint8_t weight=0;};
// Both legacy national-cup ordering routines and the typed adapter use this
// exact exchange order. Replacing it with stable_sort changes some equal ties.
template<class Fixture,class Classify>
void order_national_cup_pairs(std::vector<Fixture>& pairs,Classify classify) {
    std::vector<uint8_t> weights;weights.reserve(pairs.size());
    for(auto& pair:pairs) {
        const auto home=classify(pair.home),away=classify(pair.away);
        if(away.lower_division && !home.lower_division)std::swap(pair.home,pair.away);
        weights.push_back(uint8_t(home.weight+away.weight));
    }
    for(size_t i=0;i+1<pairs.size();++i)for(size_t j=i+1;j<pairs.size();++j)
        if(weights[i]<weights[j]) {std::swap(weights[i],weights[j]);std::swap(pairs[i],pairs[j]);}
}
}
