// SPDX-License-Identifier: GPL-3.0-only
#include "formats/data_profile.hpp"
#include "formats/resource_bundle.hpp"
#include <fstream>
#include <numeric>
#include <set>
#include <sstream>
namespace bmh {
DataProfile DataProfile::international_11() {
    DataProfile p;
    p.id="classic-plus-1";
    p.leagues={18,18,18,18,18,18,20,18,18};
    p.counts={445,3334,194,4104,164,30};
    p.big_squad_teams=164;
    p.national_first=383;
    return p;
}
void DataProfile::validate() const {
    const auto total=std::accumulate(leagues.begin(),leagues.end(),uint32_t(0));
    if(total!=counts.league_teams || big_squad_teams>total || counts.n42!=total+counts.cup_teams ||
       counts.n66!=extra_slots_first() || counts.clubs!=national_first+62 ||
       national_first!=total+counts.cup_teams+189 || counts.clubs>512 || counts.players>=4095 || total_slots()>65535)
        throw FormatError("profile: inconsistent counts or packed identifier overflow");
    for(auto size:leagues)if(size!=12 && size!=18 && size!=20)throw FormatError("profile: unsupported league size");
}
DataProfile load_data_profile(const std::filesystem::path& directory) {
    const auto file=directory/"OBMHPROF.INI";
    if(!resource_exists(file))return {};
    return decode_data_profile(read_file(file));
}
DataProfile decode_data_profile(const Bytes& raw) {
    std::istringstream input(std::string(raw.begin(),raw.end()));
    std::string line,id;bool section=false,seen=false;
    while(std::getline(input,line)) {
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        const auto first=line.find_first_not_of(" \t");if(first==std::string::npos||line[first]==';'||line[first]=='#')continue;
        const auto last=line.find_last_not_of(" \t");line=line.substr(first,last-first+1);
        if(line=="[OpenBMH]"){if(section)throw FormatError("profile: duplicate section");section=true;continue;}
        if(!section||line.rfind("profile=",0)!=0||seen)throw FormatError("profile: unknown or duplicate metadata key");
        id=line.substr(8);seen=true;
    }
    if(id!="classic-plus-1")throw FormatError("profile: unsupported data profile");
    auto p=DataProfile::international_11();p.validate();return p;
}
}
