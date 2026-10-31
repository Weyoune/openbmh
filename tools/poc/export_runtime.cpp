// SPDX-License-Identifier: GPL-3.0-only
#include "formats/le.hpp"
#include "formats/executable_layout.hpp"
#include "formats/badge_policy.hpp"
#include "game/calendar.hpp"
#include "game/environment.hpp"
#include <iostream>
int main(int argc,char**argv)try {
    if(argc!=3)throw std::runtime_error("usage: export_community_runtime <supplied executable> <output runtime.dat>");
    const auto image=bmh::load_le(argv[1]);
    const auto epoch=bmh::calendar_epoch_from_executable(image);
    bmh::install_environment_rules(image);
    bmh::RuntimePolicy policy{uint32_t(epoch.year),uint32_t(epoch.weekday),uint32_t(bmh::original_badge_gate(image)),uint32_t(bmh::environment_rules_supported()),{}};
    for(auto address:{bmh::layout::FreeKickTakerLabel,bmh::layout::PenaltyTakerLabel,bmh::layout::CornerTakerLabel,bmh::layout::AutomaticTakersLabel})
        policy.labels.emplace(address,image.cstring(address));
    bmh::save_runtime_image(image,policy,argv[2]);
    std::cout<<"Data-only runtime exported; no DOS header, loader or code objects\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
