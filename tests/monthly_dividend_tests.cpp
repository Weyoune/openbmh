// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/month.hpp"
#include "game/balance_plan.hpp"
#include "ui/advertising.hpp"
#include <stdexcept>

using namespace bmh;
namespace {
uint32_t word(const GameState& g,int manager,size_t offset) {
    return le32(g.manager_records,size_t(manager)*0xb0c+offset);
}
GameState fixture(uint32_t price) {
    GameState g;g.level=1;g.manager_teams={0,1};g.manager_records.resize(5*0xb0c);
    g.memory.add_block(layout::FundBlock,6*0x1c);
    g.memory.add_block(layout::Loans,4*0x78);
    g.memory.add_block(layout::Spending,4*0x24);
    g.memory.add_block(layout::ManagerCount,1);g.memory.set8(layout::ManagerCount,2);
    g.memory.add_block(layout::ActiveManager,1);g.memory.set8(layout::ActiveManager,1);
    g.memory.add_block(layout::GameMode,1);g.memory.set8(layout::GameMode,3);
    g.memory.add_block(layout::CareerDuration,1);g.memory.set8(layout::CareerDuration,5);
    g.tactics.resize(4*0x840);g.league_rounds.resize(9);
    for(size_t m=0;m<4;++m)for(size_t row=0;row<24;++row)g.tactics[m*0x840+row*0x58+0x3d]=0xff;
    g.memory.set32(layout::FundBlock+5*0x1c,price);
    g.memory.set16(layout::Advertising_OriginalShareAdjustment_RuntimeField+5*0x1c,3);
    g.memory.set16(layout::Advertising_OriginalShareAdjustment_RuntimeField+5*0x1c+2,10);
    return g;
}
void verify(uint32_t initial,uint32_t grown,uint32_t per_unit) {
    auto g=fixture(initial);monthly_shares_and_loans(g);
    if(g.memory.u32(layout::FundBlock+5*0x1c)!=grown)
        throw std::runtime_error("monthly dividend changed price-growth rounding");
    // The default legacy policy retains only the last holder and pays record2.
    if(word(g,0,0x314)!=0||word(g,1,0x314)!=0||
       word(g,2,0x314)!=10*per_unit||word(g,2,0x870)!=10*per_unit)
        throw std::runtime_error("representable monthly dividend or legacy recipient is incorrect");
    const auto settled_records=g.manager_records;
    const auto preview=bmh::ui::original_finance_preview(g,1);
    if(preview.income!=int32_t(word(g,2,0x314)))
        throw std::runtime_error("monthly property preview differs from the real settled dividend");
    auto forecast=g;forecast.manager_records[0xb0c+0x310]=2; // Twelve-month view.
    const auto forecast_records=forecast.manager_records;
    const auto plan=open_balance_plan(forecast,1);
    if(plan.rows[5]!=int32_t(120*per_unit))
        throw std::runtime_error("annual property preview differs from settled monthly dividends");
    if(g.manager_records!=settled_records||forecast.manager_records!=forecast_records)
        throw std::runtime_error("dividend previews mutated the financial records");
#ifdef BMH_TEST_PER_HOLDER_DIVIDENDS
    auto typed=fixture(initial);monthly_shares_and_loans(typed,true);
    if(word(typed,0,0x314)!=3*per_unit||word(typed,1,0x314)!=10*per_unit||
       word(typed,2,0x314)!=0||word(typed,0,0x870)!=3*per_unit||word(typed,1,0x870)!=10*per_unit)
        throw std::runtime_error("typed policy did not pay the actual holders");
#endif
}
}
int main() {
    verify(1'000'000,1'002'500,3'007);
    verify(999,999,2); // Per-unit truncation must precede multiplying holdings.
    verify(200'000'000,200'500'000,601'500); // Product exceeds UINT32_MAX, payout fits.
    verify(2'000'000'000,2'005'000'000,6'015'000);
}
