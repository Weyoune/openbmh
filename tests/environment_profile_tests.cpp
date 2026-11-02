// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/environment.hpp"
#include <iostream>
#include <stdexcept>

using namespace bmh;
namespace {void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}}
int main()try {
    const std::array<uint8_t,6> source{2,4,5,5,5,5};
    auto image=LeImage::from_bytes(layout::EnvironmentBudgetSteps,Bytes(source.begin(),source.end()));
    install_environment_rules(image);
    check(environment_rules_supported()&&kEnvironmentStep==std::array<int32_t,6>{2000,4000,5000,5000,5000,5000},
        "valid source steps require the common model without original instructions");
    const auto previous=kEnvironmentStep;
    auto missing=LeImage::from_bytes(layout::EnvironmentBudgetSteps,Bytes(source.begin(),source.end()-1));
    install_environment_rules(missing);check(!environment_rules_supported()&&kEnvironmentStep==previous,"missing final step partially installed");
    auto malformed=image;const uint8_t changed=9;malformed.patch(layout::EnvironmentBudgetSteps,&changed,1);const uint8_t zero=0;malformed.patch(layout::EnvironmentBudgetSteps+3,&zero,1);
    install_environment_rules(malformed);check(!environment_rules_supported()&&kEnvironmentStep==previous,"zero step partially installed");
    install_environment_rules(image);check(environment_rules_supported()&&kEnvironmentStep==previous,"valid profile could not recover after rejected input");
    GameState state;state.memory.add_block(layout::Spending,4*0x24);
    state.memory.set32(layout::Spending,12000);state.memory.set32(layout::Spending+4,24000);
    check(environment_monthly_cost(state,0,0)==12000&&environment_monthly_cost(state,0,1)==24000,"common profile changed owned monthly budgets");
    std::cout<<"Common Diamond3.7 monthly model accepts valid data without instruction provenance and rejects malformed numeric steps atomically\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
