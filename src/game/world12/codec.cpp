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
}
Bytes encode_world(const World& world){
    world.validate();A players,clubs,members,crests,loans,managers;
    for(const auto&[key,p]:world.players)players.emplace_back(O{{"id",id(key.value)},{"name",p.attributes.name},{"form_word",id(p.attributes.w35c)},{"skill_word",id(p.attributes.w368)},{"identity_word",id(p.attributes.w364)},{"bonus",int(p.bonus)}});
    for(const auto&[key,c]:world.clubs){A roster;for(const auto& s:c.roster)roster.emplace_back(O{{"player",s.empty()?J(nullptr):id(s.player.value)},{"flags",int(s.flags)}});clubs.emplace_back(O{{"id",id(key.value)},{"name",c.name},{"country",int(c.country)},{"kind",int(c.kind)},{"league",c.league.valid()?id(c.league.value):J(nullptr)},{"crest",id(c.crest.value)},{"kit",int(c.kit)},{"rating",int(c.rating)},{"short_span",int(c.short_span)},{"roster",std::move(roster)}});}
    for(const auto&m:world.membership){A ids;for(auto c:m.clubs)ids.push_back(id(c.value));members.emplace_back(O{{"league",id(m.league.value)},{"clubs",std::move(ids)}});}
    for(const auto&[key,crest]:world.crests)crests.emplace_back(O{{"id",id(key.value)},{"resource",crest.path},{"cell",int(crest.cell)}});
    for(const auto&l:world.loans)loans.emplace_back(O{{"player",id(l.player.value)},{"owner",id(l.owner.value)},{"borrower",id(l.borrower.value)},{"months",int(l.months)}});
    for(const auto&m:world.managers)managers.emplace_back(O{{"seat",int(m.seat)},{"name",m.name},{"club",id(m.club.value)},{"start_league",id(m.start_league.value)},{"portrait",int(m.portrait)}});
    auto raw=cli::dump_json(O{{"format","openbmh-world-12"},{"version",1},{"profile",kProfile},{"players",std::move(players)},{"clubs",std::move(clubs)},{"leagues",std::move(members)},{"crests",std::move(crests)},{"loans",std::move(loans)},{"managers",std::move(managers)}});return Bytes(raw.begin(),raw.end());
}
World decode_world(const Bytes& bytes){
    if(bytes.size()>32*1024*1024)throw FormatError("world12: database too large");auto root=cli::parse_json(std::string(bytes.begin(),bytes.end()));if(text(field(root,"format"))!="openbmh-world-12"||number(field(root,"version"))!=1||text(field(root,"profile"))!=kProfile)throw FormatError("world12: unsupported database profile/version");
    World w;for(const auto& j:array(field(root,"players"))){PlayerRecord p;p.id={number(field(j,"id"))};p.attributes={text(field(j,"name")),number(field(j,"form_word"),4294967295u),number(field(j,"skill_word"),4294967295u),number(field(j,"identity_word"),4294967295u)};p.bonus=uint8_t(number(field(j,"bonus"),255));if(!w.players.emplace(p.id,p).second)throw FormatError("world12: duplicate player ID");}
    for(const auto&j:array(field(root,"clubs"))){ClubRecord c;c.id={number(field(j,"id"))};c.name=text(field(j,"name"));c.country=Country(number(field(j,"country"),61));c.kind=ClubKind(number(field(j,"kind"),1));const auto& l=field(j,"league");if(l.integer())c.league={number(l,13)};else if(!std::holds_alternative<std::nullptr_t>(l.value))throw FormatError("world12: invalid optional league");c.crest={number(field(j,"crest"))};c.kit=uint8_t(number(field(j,"kit"),255));c.rating=uint8_t(number(field(j,"rating"),255));c.short_span=uint8_t(number(field(j,"short_span"),255));const auto& roster=array(field(j,"roster"));if(roster.size()!=24)throw FormatError("world12: roster must have 24 positions");for(size_t i=0;i<24;++i){const auto&p=field(roster[i],"player");if(p.integer())c.roster[i].player={number(p)};else if(!std::holds_alternative<std::nullptr_t>(p.value))throw FormatError("world12: invalid optional player");c.roster[i].flags=uint16_t(number(field(roster[i],"flags"),15));}if(!w.clubs.emplace(c.id,c).second)throw FormatError("world12: duplicate club ID");}
    const auto& leagues=array(field(root,"leagues"));if(leagues.size()!=14)throw FormatError("world12: exactly fourteen leagues required");bool seen[14]{};for(const auto&j:leagues){LeagueId id{number(field(j,"league"),13)};if(seen[id.value])throw FormatError("world12: duplicate league");seen[id.value]=true;auto&m=w.membership[id.value];m.league=id;for(const auto& c:array(field(j,"clubs")))m.clubs.push_back({number(c)});}
    for(const auto&j:array(field(root,"crests")))if(!w.crests.emplace(CrestId{number(field(j,"id"))},CrestResource{text(field(j,"resource")),uint8_t(number(field(j,"cell"),7))}).second)throw FormatError("world12: duplicate crest ID");
    for(const auto&j:array(field(root,"loans")))w.loans.push_back({{number(field(j,"player"))},{number(field(j,"owner"))},{number(field(j,"borrower"))},uint8_t(number(field(j,"months"),255))});
    for(const auto&j:array(field(root,"managers")))w.managers.push_back({uint8_t(number(field(j,"seat"),3)),text(field(j,"name")),{number(field(j,"club"))},{number(field(j,"start_league"),13)},uint8_t(number(field(j,"portrait"),11))});
    try{w.validate();}catch(const std::exception&e){throw FormatError(e.what());}return w;
}
}
