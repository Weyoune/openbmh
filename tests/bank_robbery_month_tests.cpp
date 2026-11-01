// SPDX-License-Identifier: GPL-3.0-only
#include "game/bank_robbery.hpp"
#include "formats/executable_layout.hpp"
#include "game/save.hpp"
#include "game/week.hpp"
#include <bit>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace bmh;
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void cash(GameState& state,int manager,int32_t value) {
    const size_t at=size_t(manager)*0xb0c+0x314;
    for(size_t n=0;n<4;++n)state.manager_records.at(at+n)=uint8_t(uint32_t(value)>>(8*n));
}
uint32_t expense(const GameState& state,int manager) {
    const size_t at=size_t(manager)*0xb0c+0x8a0;
    return uint32_t(state.manager_records.at(at))|uint32_t(state.manager_records.at(at+1))<<8|
        uint32_t(state.manager_records.at(at+2))<<16|uint32_t(state.manager_records.at(at+3))<<24;
}
GameState fixture() {
    GameState state;state.level=1;state.manager_records.resize(5*0xb0c);state.manager_teams={0,1};
    state.memory.add_block(layout::ManagerCount,1);state.memory.set8(layout::ManagerCount,2);
    state.club_ids={0,0};state.club_names={'C',0};return state;
}
template<class F>void rejects(F function,const char* why){bool rejected=false;try{function();}catch(const FormatError&){rejected=true;}check(rejected,why);}
}
int main()try {
    uint32_t expected_key=0;
    for(uint32_t day=0;day<1800;++day) {
        if(day&&month_code(day)!=month_code(day-1))expected_key=day;
        check(bank_robbery_month_key(day)==expected_key,"monthly key disagrees with the source calendar or collides across years");
    }
    auto state=fixture();cash(state,0,90000000);WatcomRandom random(713);
    const auto initial_calls=random.calls();daily_interest(state,random);
    check(!bank_robbery_guard_month(state,0)&&random.calls()==initial_calls,
          "ordinary low cash initializes a guard or consumes randomness");
    const auto old_native=save_stream(state);
    check(SaveState(old_native).extension().empty(),"ordinary native saves gain an unnecessary extension");
    state=GameState::from_save(SaveState(old_native));cash(state,0,250000000);
    daily_interest(state,random);
    check(bank_robbery_guard_month(state,0)==0&&expense(state,0)==0&&random.calls()==initial_calls,
          "an unknown imported history was robbed instead of receiving its grace month");
    for(int retry=0;retry<5;++retry)daily_interest(state,random);
    check(expense(state,0)==0&&random.calls()==initial_calls,"grace-month retries charge money or consume random draws");
    state.date=31;daily_interest(state,random);
    const auto loss=expense(state,0);
    check(loss>=10000000&&loss<=20000000&&loss%1000==0&&random.calls()==initial_calls+1&&bank_robbery_guard_month(state,0)==31,
          "the next month does not preserve the original threshold and loss distribution");
    for(int retry=0;retry<5;++retry)daily_interest(state,random);
    check(expense(state,0)==loss&&random.calls()==initial_calls+1,"a monthly repeat charges again or consumes RNG");

    WatcomRandom save_random(25);const auto save=make_savegame(state,save_random);
    const auto path=std::filesystem::temp_directory_path()/("openbmh-robbery-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".FBM");
    write_savegame(path,save);const auto loaded_file=load_savegame(path);std::filesystem::remove(path);
    check(loaded_file.checksum_ok,"monthly guard invalidated encrypted save checksum");
    auto loaded=GameState::from_save(SaveState(loaded_file.stream));
    WatcomRandom resumed_random(100);daily_interest(loaded,resumed_random);
    check(bank_robbery_guard_month(loaded,0)==31&&expense(loaded,0)==loss&&resumed_random.calls()==0,
          "save/load resets the monthly limit or draws a new loss");
    cash(loaded,1,250000000);daily_interest(loaded,resumed_random);
    check(bank_robbery_guard_month(loaded,1)==31&&expense(loaded,1)==0&&resumed_random.calls()==0,
          "one manager's limit is reused by another manager or its grace draws RNG");
    loaded.date=62;daily_interest(loaded,resumed_random);
    check(bank_robbery_guard_month(loaded,0)==62&&bank_robbery_guard_month(loaded,1)==62&&resumed_random.calls()==2,
          "the next calendar month does not permit exactly one independent draw per manager");
    loaded.date=183;daily_interest(loaded,resumed_random);const auto december_calls=resumed_random.calls();
    loaded.date=184;daily_interest(loaded,resumed_random);
    check(resumed_random.calls()==december_calls+2,"December to January does not reset each manager's month");
    loaded.date=365;daily_interest(loaded,resumed_random);const auto yearly_calls=resumed_random.calls();
    check(bank_robbery_guard_month(loaded,0)==365,"a later July collides with an earlier year's July");
    daily_interest(loaded,resumed_random);check(resumed_random.calls()==yearly_calls,"same-month retry after year rollover consumes RNG");
    auto malformed=old_native;append_save_extension(malformed,{{kBankRobberyMonthExtension,Bytes(15,0)}});
    rejects([&]{(void)GameState::from_save(SaveState(malformed));},"truncated monthly guard is accepted");
    malformed=old_native;append_save_extension(malformed,{{kBankRobberyMonthExtension,Bytes(16,0)},{kBankRobberyMonthExtension,Bytes(16,0)}});
    rejects([&]{(void)GameState::from_save(SaveState(malformed));},"duplicate monthly guards are accepted");
    std::cout<<"Monthly robbery limit, source month/year boundaries, per-manager grace, no repeat RNG and encrypted save/load passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
