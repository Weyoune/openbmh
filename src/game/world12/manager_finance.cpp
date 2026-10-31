// SPDX-License-Identifier: GPL-3.0-only
#include "game/world12/manager_finance.hpp"
#include "game/matchday.hpp"
#include <bit>
#include <stdexcept>
namespace bmh::world12 {
namespace {
constexpr size_t record_size=0xb0c;
constexpr size_t balance_at=0x314,credit_at=0x31c,income_at=0x874,expenses_at=0x8a0,bonus_at=0x334;
uint32_t word(const Bytes& bytes,size_t at){return uint32_t(bytes.at(at))|uint32_t(bytes.at(at+1))<<8|uint32_t(bytes.at(at+2))<<16|uint32_t(bytes.at(at+3))<<24;}
void set_word(Bytes& bytes,size_t at,uint32_t value){for(size_t i=0;i<4;++i)bytes.at(at+i)=uint8_t(value>>(i*8));}
}
ManagerFinance manager_finance_from_legacy(const GameState& state,int manager) {
    if(manager<0||manager>4||state.manager_records.size()<(size_t(manager)+1)*record_size)
        throw std::invalid_argument("world12: missing initialized manager financial record");
    const auto at=size_t(manager)*record_size;
    return {std::bit_cast<int32_t>(word(state.manager_records,at+balance_at)),word(state.manager_records,at+credit_at),
        word(state.manager_records,at+income_at),word(state.manager_records,at+expenses_at),word(state.manager_records,at+bonus_at)};
}
ManagerMoneyOutcome calculate_manager_booking(const ManagerFinance& input,int difficulty,int32_t amount,bool force,bool statistics) {
    if(difficulty<0||difficulty>9)throw std::invalid_argument("world12: invalid financial game difficulty");
    GameState workspace;workspace.level=difficulty;workspace.manager_records.assign(record_size,0);
    set_word(workspace.manager_records,balance_at,std::bit_cast<uint32_t>(input.balance));set_word(workspace.manager_records,credit_at,input.credit);
    set_word(workspace.manager_records,income_at,input.income);set_word(workspace.manager_records,expenses_at,input.expenses);set_word(workspace.manager_records,bonus_at,input.bonus_paid);
    const bool allowed=bmh::book_money(workspace,0,amount,force,statistics);
    const auto result=manager_finance_from_legacy(workspace,0);
    return {allowed,result!=input,result};
}
}
