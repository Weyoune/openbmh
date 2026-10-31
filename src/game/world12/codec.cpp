// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/codec.hpp"
#include "cli/json.hpp"
#include <stdexcept>
namespace bmh::world12 {
namespace {
using J=cli::Json;using A=J::Array;using O=J::Object;
const J& field(const J& j,const char*name){if(!j.object())throw FormatError("world12: object expected");auto it=j.object()->find(name);if(it==j.object()->end())throw FormatError(std::string("world12: missing ")+name);return it->second;}
const A& array(const J& j){if(!j.array())throw FormatError("world12: array expected");return *j.array();}
uint32_t number(const J& j,uint32_t maximum=4294967294u){if(!j.integer()||*j.integer()<0||uint64_t(*j.integer())>maximum)throw FormatError("world12: integer/reference out of range");return uint32_t(*j.integer());}
std::string text(const J& j){if(!j.string()||j.string()->size()>4096)throw FormatError("world12: invalid text");return *j.string();}
J id(uint32_t value){return J(int64_t(value));}
int32_t signed_number(const J& j){if(!j.integer()||*j.integer()<-2147483648LL||*j.integer()>2147483647LL)throw FormatError("world12: signed 32-bit value out of range");return int32_t(*j.integer());}
bool boolean(const J& j){if(!j.boolean())throw FormatError("world12: boolean game scope expected");return *j.boolean();}
J encode_tactical_backing(const std::optional<ManagerTacticalBacking>& value) {
    if(!value)return nullptr;
    A rows,marks,marked_by,formations,tail,enabled,automation;
    for(const auto& row:value->scalar_rows){A bytes;for(auto b:row)bytes.emplace_back(int(b));rows.emplace_back(std::move(bytes));}
    for(auto p:value->marks)marks.push_back(p.valid()?id(p.value):J(nullptr));
    for(auto p:value->marked_by)marked_by.push_back(p.valid()?id(p.value):J(nullptr));
    for(auto b:value->formations)formations.emplace_back(int(b));
    for(auto b:value->formation_tail)tail.emplace_back(int(b));
    for(auto b:value->enabled_formations)enabled.emplace_back(int(b));
    for(auto b:value->automation)automation.emplace_back(int(b));
    return O{{"scalar_rows",std::move(rows)},{"marks",std::move(marks)},{"marked_by",std::move(marked_by)},
        {"formations",std::move(formations)},{"formation_tail",std::move(tail)},{"enabled_formations",std::move(enabled)},{"automation",std::move(automation)}};
}
std::optional<ManagerTacticalBacking> decode_tactical_backing(const J& value) {
    if(std::holds_alternative<std::nullptr_t>(value.value))return {};
    ManagerTacticalBacking result;
    const auto bytes=[&](const J& input,auto& output){const auto& a=array(input);if(a.size()!=output.size())throw FormatError("world12: invalid tactical byte dimensions");for(size_t i=0;i<a.size();++i)output[i]=uint8_t(number(a[i],255));};
    const auto refs=[&](const J& input,auto& output){const auto& a=array(input);if(a.size()!=output.size())throw FormatError("world12: invalid tactical reference dimensions");for(size_t i=0;i<a.size();++i){if(a[i].integer())output[i]={number(a[i])};else if(!std::holds_alternative<std::nullptr_t>(a[i].value))throw FormatError("world12: invalid tactical player reference");}};
    const auto& rows=array(field(value,"scalar_rows"));if(rows.size()!=24)throw FormatError("world12: invalid full tactical row count");
    for(size_t row=0;row<24;++row)bytes(rows[row],result.scalar_rows[row]);
    refs(field(value,"marks"),result.marks);refs(field(value,"marked_by"),result.marked_by);
    bytes(field(value,"formations"),result.formations);bytes(field(value,"formation_tail"),result.formation_tail);bytes(field(value,"enabled_formations"),result.enabled_formations);bytes(field(value,"automation"),result.automation);
    return result;
}
}
Bytes encode_world(const World& world){
    world.validate();A players,clubs,members,crests,loans,managers;
    for(const auto&[key,p]:world.players)players.emplace_back(O{{"id",id(key.value)},{"name",p.attributes.name},{"form_word",id(p.attributes.w35c)},{"skill_word",id(p.attributes.w368)},{"identity_word",id(p.attributes.w364)},{"bonus",int(p.bonus)},{"cards",int(p.cards)}});
    for(const auto&[key,c]:world.clubs){A counters,opponents,dates;for(auto value:c.season_stats.counters)counters.emplace_back(int(value));for(auto value:c.season_stats.opponents)opponents.push_back(value.valid()?id(value.value):J(nullptr));for(auto value:c.season_stats.dates)dates.emplace_back(int(value));A roster;for(const auto& s:c.roster)roster.emplace_back(O{{"player",s.empty()?J(nullptr):id(s.player.value)},{"flags",int(s.flags)}});clubs.emplace_back(O{{"id",id(key.value)},{"name",c.name},{"country",int(c.country)},{"kind",int(c.kind)},{"league",c.league.valid()?id(c.league.value):J(nullptr)},{"crest",id(c.crest.value)},{"kit",int(c.kit)},{"rating",int(c.rating)},{"short_span",int(c.short_span)},{"match_flags",int(c.match_flags)},{"morale",int(c.morale)},{"lineup_indoor",c.lineup_indoor},{"season_stats",O{{"counters",std::move(counters)},{"opponents",std::move(opponents)},{"dates",std::move(dates)}}},{"roster",std::move(roster)}});}
    for(const auto&m:world.membership){A ids;for(auto c:m.clubs)ids.push_back(id(c.value));members.emplace_back(O{{"league",id(m.league.value)},{"clubs",std::move(ids)}});}
    for(const auto&[key,crest]:world.crests)crests.emplace_back(O{{"id",id(key.value)},{"resource",crest.path},{"cell",int(crest.cell)}});
    for(const auto&l:world.loans)loans.emplace_back(O{{"player",id(l.player.value)},{"owner",id(l.owner.value)},{"borrower",id(l.borrower.value)},{"months",int(l.months)}});
    for(const auto&m:world.managers){J finance=nullptr;if(m.finance){const auto& f=*m.finance;finance=O{{"balance",J(int64_t(f.balance))},{"credit",id(f.credit)},{"income",id(f.income)},{"expenses",id(f.expenses)},{"bonus_paid",id(f.bonus_paid)}};}J tactics=nullptr;if(m.tactics){A rows;for(const auto& row:m.tactics->rows){A orders;for(auto value:row.orders)orders.emplace_back(int(value));rows.emplace_back(O{{"orders",std::move(orders)},{"partner",int(row.partner)},{"depth",int(row.depth)},{"lane",int(row.lane)}});}tactics=O{{"rows",std::move(rows)},{"automatic_weights",int(m.tactics->automatic_weights)},{"automatic_formation",int(m.tactics->automatic_formation)},{"backing",encode_tactical_backing(m.tactics->backing)}};}managers.emplace_back(O{{"seat",int(m.seat)},{"name",m.name},{"club",id(m.club.value)},{"start_league",id(m.start_league.value)},{"portrait",int(m.portrait)},{"finance",std::move(finance)},{"tactics",std::move(tactics)}});}
    auto raw=cli::dump_json(O{{"format","openbmh-world-12"},{"version",7},{"profile",kProfile},{"scope",O{{"germany",world.catalogue.scope.germany},{"england",world.catalogue.scope.england},{"international",world.catalogue.scope.international}}},{"development_balance",J(int64_t(world.development_balance))},{"trend_rotation",int(world.trend_rotation)},{"players",std::move(players)},{"clubs",std::move(clubs)},{"leagues",std::move(members)},{"crests",std::move(crests)},{"loans",std::move(loans)},{"managers",std::move(managers)}});return Bytes(raw.begin(),raw.end());
}
World decode_world(const Bytes& bytes){
    if(bytes.size()>32*1024*1024)throw FormatError("world12: database too large");auto root=cli::parse_json(std::string(bytes.begin(),bytes.end()));const auto version=number(field(root,"version"));if(text(field(root,"format"))!="openbmh-world-12"||(version<1||version>7)||text(field(root,"profile"))!=kProfile)throw FormatError("world12: unsupported database profile/version");
    World w;if(version>=4){w.development_balance=signed_number(field(root,"development_balance"));w.trend_rotation=uint8_t(number(field(root,"trend_rotation"),2));}if(version>=2){const auto& scope=field(root,"scope");w.catalogue=Catalogue::dual_classic({boolean(field(scope,"germany")),boolean(field(scope,"england")),boolean(field(scope,"international"))});}for(const auto& j:array(field(root,"players"))){PlayerRecord p;p.id={number(field(j,"id"))};p.attributes={text(field(j,"name")),number(field(j,"form_word"),4294967295u),number(field(j,"skill_word"),4294967295u),number(field(j,"identity_word"),4294967295u)};p.bonus=uint8_t(number(field(j,"bonus"),255));if(version>=3)p.cards=uint16_t(number(field(j,"cards"),65535));if(!w.players.emplace(p.id,p).second)throw FormatError("world12: duplicate player ID");}
    for(const auto&j:array(field(root,"clubs"))){ClubRecord c;c.id={number(field(j,"id"))};c.name=text(field(j,"name"));c.country=Country(number(field(j,"country"),61));c.kind=ClubKind(number(field(j,"kind"),1));const auto& l=field(j,"league");if(l.integer())c.league={number(l,uint32_t(kLeagueSlots-1))};else if(!std::holds_alternative<std::nullptr_t>(l.value))throw FormatError("world12: invalid optional league");c.crest={number(field(j,"crest"))};c.kit=uint8_t(number(field(j,"kit"),255));c.rating=uint8_t(number(field(j,"rating"),255));c.short_span=uint8_t(number(field(j,"short_span"),255));if(version>=3){c.match_flags=uint16_t(number(field(j,"match_flags"),65535));c.morale=uint8_t(number(field(j,"morale"),63));}if(version>=7)c.lineup_indoor=boolean(field(j,"lineup_indoor"));if(version>=4){const auto& stats=field(j,"season_stats");const auto& counters=array(field(stats,"counters"));const auto& opponents=array(field(stats,"opponents"));const auto& dates=array(field(stats,"dates"));if(counters.size()!=42||opponents.size()!=8||dates.size()!=8)throw FormatError("world12: invalid club statistics dimensions");for(size_t i=0;i<42;++i)c.season_stats.counters[i]=uint8_t(number(counters[i],255));for(size_t i=0;i<8;++i){if(opponents[i].integer())c.season_stats.opponents[i]={number(opponents[i])};else if(!std::holds_alternative<std::nullptr_t>(opponents[i].value))throw FormatError("world12: invalid statistics opponent");c.season_stats.dates[i]=uint16_t(number(dates[i],65535));}}
const auto& roster=array(field(j,"roster"));if(roster.size()!=24)throw FormatError("world12: roster must have 24 positions");for(size_t i=0;i<24;++i){const auto&p=field(roster[i],"player");if(p.integer())c.roster[i].player={number(p)};else if(!std::holds_alternative<std::nullptr_t>(p.value))throw FormatError("world12: invalid optional player");c.roster[i].flags=uint16_t(number(field(roster[i],"flags"),15));}if(!w.clubs.emplace(c.id,c).second)throw FormatError("world12: duplicate club ID");}
    const auto& leagues=array(field(root,"leagues"));if(leagues.size()!=kLeagueSlots)throw FormatError("world12: exactly thirteen leagues required");bool seen[kLeagueSlots]{};for(const auto&j:leagues){LeagueId id{number(field(j,"league"),uint32_t(kLeagueSlots-1))};if(seen[id.value])throw FormatError("world12: duplicate league");seen[id.value]=true;auto&m=w.membership[id.value];m.league=id;for(const auto& c:array(field(j,"clubs")))m.clubs.push_back({number(c)});}
    for(const auto&j:array(field(root,"crests")))if(!w.crests.emplace(CrestId{number(field(j,"id"))},CrestResource{text(field(j,"resource")),uint8_t(number(field(j,"cell"),7))}).second)throw FormatError("world12: duplicate crest ID");
    for(const auto&j:array(field(root,"loans")))w.loans.push_back({{number(field(j,"player"))},{number(field(j,"owner"))},{number(field(j,"borrower"))},uint8_t(number(field(j,"months"),255))});
    for(const auto&j:array(field(root,"managers"))){Manager manager{uint8_t(number(field(j,"seat"),3)),text(field(j,"name")),{number(field(j,"club"))},{number(field(j,"start_league"),uint32_t(kLeagueSlots-1))},uint8_t(number(field(j,"portrait"),11))};if(version>=5){const auto& f=field(j,"finance");if(f.object())manager.finance=ManagerFinance{signed_number(field(f,"balance")),number(field(f,"credit"),4294967295u),number(field(f,"income"),4294967295u),number(field(f,"expenses"),4294967295u),number(field(f,"bonus_paid"),4294967295u)};else if(!std::holds_alternative<std::nullptr_t>(f.value))throw FormatError("world12: invalid optional manager finances");}if(version>=6){const auto& t=field(j,"tactics");if(t.object()){
ManagerTactics tactics;const auto& rows=array(field(t,"rows"));if(rows.size()!=24)throw FormatError("world12: tactics require 24 rows");
for(size_t i=0;i<24;++i){const auto& r=rows[i];const auto& orders=array(field(r,"orders"));if(orders.size()!=4)throw FormatError("world12: tactics require four player orders");for(size_t a=0;a<4;++a)tactics.rows[i].orders[a]=uint8_t(number(orders[a],255));tactics.rows[i].partner=uint8_t(number(field(r,"partner"),255));tactics.rows[i].depth=uint8_t(number(field(r,"depth"),255));tactics.rows[i].lane=uint8_t(number(field(r,"lane"),255));}
tactics.automatic_weights=uint8_t(number(field(t,"automatic_weights"),255));tactics.automatic_formation=uint8_t(number(field(t,"automatic_formation"),8));if(version>=7)tactics.backing=decode_tactical_backing(field(t,"backing"));manager.tactics=tactics;
}else if(!std::holds_alternative<std::nullptr_t>(t.value))throw FormatError("world12: invalid optional manager tactics");}w.managers.push_back(std::move(manager));}
    try{w.validate();}catch(const std::exception&e){throw FormatError(e.what());}return w;
}
}
