// SPDX-License-Identifier: GPL-3.0-only
#include "formats/le.hpp"
#include "formats/badge_policy.hpp"
#include "formats/executable_layout.hpp"
#include "game/career.hpp"
#include "game/environment.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main()try {
    using namespace bmh;
    constexpr uint32_t image_base=0x10000;
    constexpr size_t image_size=0xb0000;
    auto image=LeImage::from_bytes(image_base,Bytes(image_size,0));
    uint8_t original=7;image.patch(layout::LiveSceneOptions,&original,1);
    const auto file=std::filesystem::temp_directory_path()/"openbmh-classic-original-rules.image";
    save_image(image,file);const auto loaded=load_le(file);std::filesystem::remove(file);
    check(loaded.bytes()==image.bytes(),"Loading silently replaced original rules");
    Bytes ranges;for(int i=0;i<9;++i){ranges.push_back(uint8_t(30+i));ranges.push_back(uint8_t(40+i));}
    image.patch(layout::LeagueStrengthRanges,ranges.data(),ranges.size());install_career_rules(image);
    check(career_strength_range(0)==std::pair{30,40} && career_strength_range(8)==std::pair{38,48},"Original league ranges not installed");
    uint8_t invalid[]={90,20};image.patch(layout::LeagueStrengthRanges+16,invalid,2);bool rejected=false;
    try{install_career_rules(image);}catch(const FormatError&){rejected=true;}
    check(rejected,"Invalid original ranges accepted");
    check(career_strength_range(0)==std::pair{30,40} && career_strength_range(8)==std::pair{38,48},"Failed range installation partially changed rules");
    install_environment_rules(image);check(!environment_rules_supported(),"Unknown program code guessed an environment model");
    auto badges=LeImage::from_bytes(image_base,Bytes(256,0));
    const uint8_t legacy_gate[]={0x66,0x81,0xfb,138,0,0x7c,7};
    badges.patch(layout::ForeignBadgeGateInstruction,legacy_gate,sizeof legacy_gate);
    install_original_badge_policy(badges);check(original_badge_fallback(90)&&original_badge_fallback(137),"Older edition no longer uses its original neutral badge");
    const uint8_t extended=90;badges.patch(layout::ForeignBadgeGateInstruction+3,&extended,1);
    install_original_badge_policy(badges);check(!original_badge_fallback(90)&&!original_badge_fallback(137)&&original_badge_fallback(168)&&original_badge_fallback(0xfff),"Extended foreign badge gate is incorrect");
    check(!original_badge_fallback(0xf05a) && !original_badge_fallback(138) &&
          !original_badge_fallback(357) && original_badge_fallback(356),
          "Tagged club IDs and national/amateur badge ranges changed");
    auto truncated=LeImage::from_bytes(layout::ForeignBadgeGateInstruction+1,Bytes(6,0));
    check(original_badge_gate(truncated)==138,"Truncated badge routine was not rejected");
    const uint8_t unknown=1;badges.patch(layout::ForeignBadgeGateInstruction+3,&unknown,1);install_original_badge_policy(badges);check(original_badge_fallback(90),"Unknown badge routine guessed a crest capability");
    std::cout<<"Classic original rules retained; strength ranges and unknown-variant guard passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
