// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/cups.hpp"
#include "game/cup.hpp"
#include "game/driver.hpp"
#include "formats/le.hpp"
#include <set>
#include <stdexcept>

namespace bmh::world12 {
Country cup_country(DomesticCup cup) {
    switch(cup) {
    case DomesticCup::german_cup: case DomesticCup::german_supercup: return Country::germany;
    case DomesticCup::fa_cup: case DomesticCup::league_cup: case DomesticCup::english_supercup: return Country::england;
    }
    throw std::invalid_argument("world12: unknown domestic cup");
}
bool show_domestic_draw(const World& world,DomesticCup cup) {
    const auto country=cup_country(cup);
    for(const auto& manager:world.managers) {
        const auto& club=world.clubs.at(manager.club);
        if(club.kind==ClubKind::club && club.league.valid() && club.country==country) return true;
    }
    return false;
}
std::optional<CupDrawNotice> cup_draw_notice(const World& world,const KnockoutStage& stage) {
    stage.validate(world);
    if(!show_domestic_draw(world,stage.cup) || stage.complete() || stage.next_leg()!=1) return std::nullopt;
    return CupDrawNotice{stage.cup,stage.source_entry,uint8_t(stage.rounds.size()),stage.next_fixtures()};
}
std::optional<std::vector<DomesticCup>> domestic_cups_on(const OtherAppointment& appointment) {
    if(appointment.domain!=CalendarDomain::germany && appointment.domain!=CalendarDomain::england) return std::nullopt;
    const bool english=appointment.domain==CalendarDomain::england;
    const auto raw=appointment.source_entry;
    if(raw==0x4002 && english) return std::vector{DomesticCup::league_cup};
    if(!raw || (raw&~0x2020u)) return std::nullopt;
    std::vector<DomesticCup> cups;
    if(raw&0x20) cups.push_back(english?DomesticCup::fa_cup:DomesticCup::german_cup);
    if(raw&0x2000) cups.push_back(english?DomesticCup::english_supercup:DomesticCup::german_supercup);
    return cups;
}
namespace {
bool resolved(const CupRound& round,bool two_legs) {
    return round.first_leg.size()==round.fixtures.size() &&
        (!two_legs || round.second_leg.size()==round.fixtures.size());
}
std::vector<ClubId> winners(const CupRound& round,uint8_t flags) {
    const bool two_legs=(flags&1)!=0;
    if(!resolved(round,two_legs)) throw std::invalid_argument("world12: cup round is unfinished");
    bmh::MatchGroup group; group.desc.assign(0xd0,0); group.desc[1]=flags;
    group.cup_results=two_legs?round.second_leg:round.first_leg;
    if(two_legs) group.cup_other_leg=round.first_leg;
    std::vector<ClubId> output;
    for(size_t i=0;i<round.fixtures.size();++i) {
        const int side=bmh::cup_tie(group,int(i));
        if(side==2) throw std::invalid_argument("world12: unresolved knockout tie");
        const auto& fixture=round.fixtures[i];
        output.push_back((side==0)^two_legs?fixture.home:fixture.away);
    }
    return output;
}
std::set<ClubId> participants(const std::vector<Fixture>& fixtures,const World& world,Country country) {
    std::set<ClubId> clubs;
    for(const auto& fixture:fixtures) for(auto id:{fixture.home,fixture.away}) {
        const auto found=world.clubs.find(id);
        if(found==world.clubs.end() || found->second.kind!=ClubKind::club ||
            found->second.country!=country || !clubs.insert(id).second)
            throw std::invalid_argument("world12: missing, duplicate or foreign domestic cup entrant");
    }
    return clubs;
}
uint8_t source_cup_id(DomesticCup cup) {
    switch(cup) {
    case DomesticCup::german_cup: case DomesticCup::fa_cup: return 0;
    case DomesticCup::german_supercup: case DomesticCup::english_supercup: return 8;
    case DomesticCup::league_cup: return 11;
    }
    throw std::invalid_argument("world12: unknown source cup identity");
}
}
KnockoutStage KnockoutStage::from_source(DomesticCup cup,const LeImage& image,uint8_t entry,
    const std::vector<Fixture>& draw,const World& world,const std::vector<ClubId>& seeded) {
    if(entry>=bmh::kCupEntries) throw std::invalid_argument("world12: source cup entry outside table");
    const auto at=bmh::cup_entry_address(entry);
    if(!image.contains(at+0x27) || !image.contains(bmh::layout::LeagueSizes) ||
        (image.u8(bmh::layout::LeagueSizes)!=18 && image.u8(bmh::layout::LeagueSizes)!=20) ||
        (cup_country(cup)==Country::england)!=(image.u8(bmh::layout::LeagueSizes)==20) ||
        image.u16(at+0x18)!=source_cup_id(cup) ||
        image.u8(at+0x1a)!=draw.size()*2 || image.u8(at+0x20)!=seeded.size())
        throw std::invalid_argument("world12: source cup stage does not match its draw or seeded slots");
    KnockoutStage stage; stage.cup=cup; stage.source_entry=entry;
    stage.flags=image.u8(at+0x14); stage.planned_rounds=image.u8(at+0x1b);
    stage.next_entry=image.u8(at+0x16);stage.seeded=seeded;
    stage.rounds.push_back({draw,{},{}}); stage.validate(world); return stage;
}
bool KnockoutStage::complete()const {
    return !rounds.empty() && rounds.size()==planned_rounds && resolved(rounds.back(),two_legs());
}
uint8_t KnockoutStage::next_leg()const {
    if(rounds.empty() || complete()) throw std::invalid_argument("world12: cup stage has no pending leg");
    return rounds.back().first_leg.empty()?1:2;
}
std::vector<Fixture> KnockoutStage::next_fixtures()const {
    const auto leg=next_leg();
    auto fixtures=rounds.back().fixtures;
    if(leg==2) for(auto& fixture:fixtures) std::swap(fixture.home,fixture.away);
    return fixtures;
}
std::vector<ClubId> KnockoutStage::qualifiers()const {
    if(!complete()) throw std::invalid_argument("world12: qualifiers requested before stage end");
    auto output=winners(rounds.back(),flags);
    output.insert(output.end(),seeded.begin(),seeded.end());return output;
}
void KnockoutStage::validate(const World& world)const {
    const auto country=cup_country(cup);
    if(source_entry>=bmh::kCupEntries || next_entry>=bmh::kCupEntries ||
        (next_entry && next_entry==source_entry) || (!next_entry && !seeded.empty()) ||
        !(flags&4) || planned_rounds<1 || planned_rounds>7 ||
        rounds.empty() || rounds.size()>planned_rounds || rounds.front().fixtures.empty() ||
        rounds.front().fixtures.size()>64 || (rounds.front().fixtures.size()*2)%(1u<<planned_rounds)!=0)
        throw std::invalid_argument("world12: invalid knockout stage dimensions");
    auto initial=participants(rounds.front().fixtures,world,country);
    if(seeded.size()>128-initial.size())throw std::invalid_argument("world12: too many seeded cup entrants");
    for(auto id:seeded) {
        const auto found=world.clubs.find(id);
        if(found==world.clubs.end() || found->second.kind!=ClubKind::club ||
            found->second.country!=country || !initial.insert(id).second)
            throw std::invalid_argument("world12: missing, duplicate or foreign seeded club");
    }
    uint64_t played=0;
    std::set<ClubId> previous;
    for(size_t r=0;r<rounds.size();++r) {
        const auto& round=rounds[r];
        if(round.fixtures.size()!=rounds.front().fixtures.size()/(size_t(1)<<r))
            throw std::invalid_argument("world12: knockout bracket has wrong round size");
        const auto entrants=participants(round.fixtures,world,country);
        if(r && entrants!=previous) throw std::invalid_argument("world12: next draw is not the preceding winners");
        for(const auto* leg:{&round.first_leg,&round.second_leg}) {
            if(!leg->empty() && leg->size()!=round.fixtures.size())
                throw std::invalid_argument("world12: partial cup leg");
            for(auto score:*leg) if((score>>10)>2)
                throw std::invalid_argument("world12: unplayed or invalid cup score phase");
            played+=!leg->empty();
        }
        if((!two_legs() && !round.second_leg.empty()) ||
            (!round.second_leg.empty() && round.first_leg.empty()))
            throw std::invalid_argument("world12: unexpected return-leg results");
        if(two_legs()) for(auto score:round.first_leg) if(score>>10)
            throw std::invalid_argument("world12: extra time or penalties in a first leg");
        if(resolved(round,two_legs())) {
            const auto qualified=winners(round,flags); previous={qualified.begin(),qualified.end()};
            if(r+1==rounds.size() && r+1<planned_rounds)
                throw std::invalid_argument("world12: completed round missing its next draw");
        } else if(r+1<rounds.size()) throw std::invalid_argument("world12: skipped unfinished cup round");
    }
    if(revision!=played) throw std::invalid_argument("world12: cup revision disagrees with played legs");
}
void commit_cup_leg(KnockoutStage& stage,const CupLegResult& result,const World& world) {
    stage.validate(world);
    if(result.cup!=stage.cup || result.source_entry!=stage.source_entry || result.revision!=stage.revision || stage.complete())
        throw std::invalid_argument("world12: stale or foreign cup leg");
    auto staged=stage; auto& round=staged.rounds.back();
    if(result.scores.size()!=round.fixtures.size() || result.scores.empty())
        throw std::invalid_argument("world12: incomplete cup score batch");
    if(staged.next_leg()==1) round.first_leg=result.scores; else round.second_leg=result.scores;
    ++staged.revision;
    if(resolved(round,staged.two_legs()) && staged.rounds.size()<staged.planned_rounds) {
        if(result.next_draw.empty()) throw std::invalid_argument("world12: next original cup draw is required");
        staged.rounds.push_back({result.next_draw,{},{}});
    } else if(!result.next_draw.empty()) throw std::invalid_argument("world12: unexpected next cup draw");
    staged.validate(world);
    if(result.next_stage) {
        if(!staged.complete() || !staged.next_entry || result.next_stage->cup!=staged.cup ||
            result.next_stage->source_entry!=staged.next_entry || result.next_stage->revision!=0)
            throw std::invalid_argument("world12: unexpected or already played successor stage");
        validate_cup_programmes(world,{staged,*result.next_stage});
    }
    stage=std::move(staged);
}
std::vector<ClubId> cup_leg_qualifiers(const KnockoutStage& stage,const std::vector<uint16_t>& scores,const World& world) {
    stage.validate(world);
    if(stage.complete() || scores.empty() || scores.size()!=stage.rounds.back().fixtures.size())
        throw std::invalid_argument("world12: invalid pending cup leg score count");
    const auto leg=stage.next_leg();
    for(auto score:scores)if((score>>10)>2 || (stage.two_legs() && leg==1 && (score>>10)))
        throw std::invalid_argument("world12: invalid pending cup score phase");
    auto round=stage.rounds.back();
    if(leg==1)round.first_leg=scores;else round.second_leg=scores;
    if(!resolved(round,stage.two_legs()))return {};
    auto qualified=winners(round,stage.flags);
    if(stage.rounds.size()==stage.planned_rounds)qualified.insert(qualified.end(),stage.seeded.begin(),stage.seeded.end());
    return qualified;
}
void validate_cup_programmes(const World& world,const std::vector<KnockoutStage>& stages) {
    if(stages.size()>5*bmh::kCupEntries)throw std::invalid_argument("world12: too many domestic cup stages");
    std::map<DomesticCup,const KnockoutStage*> previous;
    std::set<std::pair<DomesticCup,uint8_t>> seen;
    for(const auto& stage:stages) {
        stage.validate(world);
        if(!seen.emplace(stage.cup,stage.source_entry).second)throw std::invalid_argument("world12: repeated/cyclic cup stage");
        if(const auto found=previous.find(stage.cup);found!=previous.end()) {
            const auto& predecessor=*found->second;
            if(!predecessor.complete() || !predecessor.next_entry || predecessor.next_entry!=stage.source_entry)
                throw std::invalid_argument("world12: cup stage bypasses an unfinished or unrelated predecessor");
            const auto qualified=predecessor.qualifiers();
            if(participants(stage.rounds.front().fixtures,world,cup_country(stage.cup))!=std::set<ClubId>(qualified.begin(),qualified.end()))
                throw std::invalid_argument("world12: successor stage does not contain all qualifiers and seeds");
        }
        previous[stage.cup]=&stage;
    }
    for(const auto& [cup,stage]:previous)if(stage->complete() && stage->next_entry)
        throw std::invalid_argument("world12: completed cup stage is missing its linked successor");
}
}
