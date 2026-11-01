// SPDX-License-Identifier: GPL-3.0-only
#include "game/week.hpp"
#include <bit>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace bmh;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void write(GameState& state,size_t at,int32_t value) {
    for(int byte=0;byte<4;++byte)state.manager_records.at(at+byte)=uint8_t(uint32_t(value)>>(byte*8));
}
int32_t read(const GameState& state,size_t at) {
    uint32_t value=0;
    for(int byte=0;byte<4;++byte)value|=uint32_t(state.manager_records.at(at+byte))<<(byte*8);
    return std::bit_cast<int32_t>(value);
}
}
int main()try {
    check(daily_interest_amounts(0,1500000000,0).cash_and_deposit==-82191,
          "large deposits reverse interest because of an intermediate product overflow");
    check(daily_interest_amounts(-2000000000,0,1000000000).borrowing==821917,
          "large debt loses its annual-rate charges before division");
    check(daily_interest_amounts(-1800000000,0,1800000000).borrowing==591780,
          "credit-line interest overflows before division");
    check(daily_interest_amounts(0,std::numeric_limits<int32_t>::max(),0).cash_and_deposit==-117670,
          "maximum native deposit interest is not computed with a wide product");
    check(daily_interest_amounts(std::numeric_limits<int32_t>::min(),0,std::numeric_limits<int32_t>::max()).borrowing==706022,
          "native debt/credit boundary changed the division order");
    for(int32_t cash:{-1000000,-1,0,1,1000000})
        for(int32_t deposit:{0,9999,10000,1000000000})
            for(int32_t credit:{0,100000,1000000}) {
                int32_t expected=cash>=0?cash/-36500:0;
                if(cash<0) {
                    if(cash+credit<0)expected=(cash+credit)*18/-36500;
                    expected+=(cash>-credit?cash:-credit)*12/-36500;
                }
                expected+=((deposit/10000)*20000)/-36500;
                const auto interest=daily_interest_amounts(cash,deposit,credit);
                check(interest.borrowing+interest.cash_and_deposit==expected,
                      "ordinary balances changed their original truncation or rate split");
            }
    GameState state;state.level=1;state.manager_teams={0};state.manager_records.resize(5*0xb0c);
    write(state,0x318,1500000000);
    WatcomRandom random(713);const auto seed=random.seed();const auto calls=random.calls();
    daily_interest(state,random);
    check(read(state,0x314)==82191&&read(state,0x870)==82191&&read(state,0x318)==1500000000,
          "daily booking and income statistics disagree with large-deposit interest");
    check(random.seed()==seed&&random.calls()==calls,"interest arithmetic consumed extra randomness");
    write(state,0x314,-2000000000);write(state,0x318,0);write(state,0x31c,1000000000);
    daily_interest(state,random);
    check(read(state,0x314)==-2000821917&&read(state,0x89c)==821917,
          "large-debt booking and expense statistics disagree with the wide calculation");
    std::cout<<"Wide daily interest, native boundary amounts, ordinary-rate parity and actual booking passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
