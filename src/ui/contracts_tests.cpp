// SPDX-License-Identifier: GPL-3.0-only
#include "ui/contracts.hpp"
#include "ui/source_notice_test_helpers.hpp"

#include <stdexcept>

using namespace bmh::ui;

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ui_contracts_tests DATA");
        ContractsState state;
        state.manager_slot = 0;
        state.runtime_club = 12;
        state.revision = 4;
        state.request_id_base = uint64_t(5) << 32;
        state.club_name = "UNIT FOOTBALL CLUB";
        state.players.push_back({0, 150, "TORWART", 25, 0, 8, 180000});
        state.players.push_back({1, 151, "STÜRMER", 21, 3, 14, 90000});
        auto blocked_state = state;
        blocked_state.players[0].offer_blocked = true;
        ContractsScreen blocked(argv[1], blocked_state);
        check(blocked.click(20,33,true) && blocked.pending(), "offended players still display their salary demands");
        auto blocked_quote=blocked_state;blocked_quote.revision=5;blocked_quote.quote_id=3;blocked_quote.quote_row=0;blocked_quote.selected_row=0;
        blocked_quote.demands=std::array<uint32_t,4>{180000,190000,200000,210000};blocked.acknowledge_quote(blocked_quote);
        check(!blocked.click(160,182), "Original grayed demands cannot open a salary calculator");
        ContractsScreen screen(argv[1], state);
        const auto pixels = screen.render().pixels;
        check(pixels.size() == 320 * 240 && screen.render().pixels == pixels,
              "contract roster renders deterministically in the fixed 320x240 view");
        check(screen.state_json().find("UNIT FOOTBALL CLUB") != std::string::npos,
              "owned projection retains UTF-8 club identity");
        check(screen.click(20, 33, true) && screen.pending() &&
              screen.pending()->kind == ContractActionKind::request_demands &&
              screen.pending()->row == 0 && screen.pending()->player_id == 150 &&
              screen.pending()->request_id == (uint64_t(5) << 32) + 1,
              "right-clicking the selected row requests its term offers");
        auto bad = state;
        bad.revision = 5;
        bad.quote_row = 0;
        bad.quote_id = 8;
        bad.demands = std::array<uint32_t,4>{180000,190000,200000,210000};
        ++bad.players[0].player_id;
        bool rejected = false;
        try { screen.acknowledge_quote(bad); } catch (const std::exception&) { rejected = true; }
        check(rejected && screen.pending(), "stale player identity cannot consume a quote request");
        auto quote = state;
        quote.revision = 5;
        quote.selected_row = 0;
        quote.quote_row = 0;
        quote.quote_id = 8;
        quote.demands = std::array<uint32_t,4>{180000,190000,200000,210000};
        screen.acknowledge_quote(quote);
        check(!screen.pending() && screen.state().demands == quote.demands &&
              screen.state_json().find("190000") != std::string::npos,
              "matching four-term quote is shown after ACK");
        {
            auto talk = screen;
            check(talk.click(160,182) && talk.click(76,132) && talk.text("17000") && talk.click(94,132),
                  "a salary can be accepted locally before an offer is submitted");
            check(talk.process_timer_expired(100,100) && !talk.pending() && !talk.close_requested() &&
                  talk.state_json().find("\"page\":0") != std::string::npos,
                  "41232 event8 cancels a locally accepted salary without submitting it");
            check(talk.process_timer_expired(100,100) && talk.close_requested(),
                  "the next outer poll returns the roster to the desk");
            auto name_column = screen;
            check(name_column.process_timer_expired(20,33) && !name_column.pending() &&
                  name_column.process_timer_expired(20,33) && name_column.pending() &&
                  name_column.pending()->kind == ContractActionKind::request_demands &&
                  name_column.pending()->player_id == 150,
                  "parent handback retains the real name-column pointer rather than inventing a desk close");
            auto premium = screen;
            check(premium.click(285,210) && premium.click(30,157) && premium.click(76,132) &&
                  premium.text("3") && premium.click(94,132), "a premium draft is accepted locally");
            const auto proposed_image = premium.render().pixels;
            check(premium.click(30,157) && premium.click(76,132) && premium.text("17"),
                  "a second unconfirmed premium draft is entered");
            check(premium.process_timer_expired(100,100) && !premium.pending() &&
                  premium.render().pixels == proposed_image,
                  "40c65 sentinel retains the earlier local premium and discards only unconfirmed digits");
            check(premium.process_timer_expired(100,100) && !premium.pending() &&
                  premium.state_json().find("\"page\":0") != std::string::npos,
                  "premium parent deadline discards local rates without an offer command");
            auto team = state; team.mode_three = true;
            ContractsScreen team_premiums(argv[1], team);
            check(team_premiums.click(210,219) && team_premiums.process_timer_expired(100,100) &&
                  !team_premiums.pending() && team_premiums.state_json().find("\"page\":0") != std::string::npos,
                  "team premium deadline does not commit a squad-wide bonus request");
            auto insurance_state = state;
            insurance_state.mode_three = true; insurance_state.players[0].insurance_available = true;
            ContractsScreen insurance(argv[1], insurance_state);
            check(insurance.click(20,33,true), "eligible mode3 player requests a quote");
            auto insurance_quote = quote;
            insurance_quote.mode_three = true; insurance_quote.players[0].insurance_available = true;
            insurance.acknowledge_quote(insurance_quote);
            check(insurance.click(220,197) && insurance.click(76,132) && insurance.text("24"),
                  "unconfirmed coverage duration is entered");
            check(insurance.process_timer_expired(100,100) && !insurance.pending() &&
                  !insurance.state().players[0].insurance && insurance.state_json().find("\"page\":1") != std::string::npos,
                  "calculator sentinel discards coverage duration without buying an insurance policy");
        }
        check(screen.click(160, 182) && screen.state_json().find("\"page\":3") != std::string::npos,
              "clicking a term demand opens the original calculator overlay");
        check(screen.click(111, 132) && screen.state_json().find("\"page\":1") != std::string::npos,
              "N cancels the calculator draft without submitting an offer");
        check(screen.click(160, 182), "the selected term can reopen its calculator");
        check(screen.click(76, 132) && screen.text("1234") && screen.click(41, 132) &&
              screen.state_json().find("\"monthly_salary_input\":123") != std::string::npos,
              "C, digit entry, and D edit the calculator draft locally");
        check(screen.click(58, 132) &&
              screen.state_json().find("\"monthly_salary_input\":190000") != std::string::npos,
              "R restores the original demand value");
        check(screen.click(94, 132) && screen.state_json().find("\"page\":1") != std::string::npos,
              "original J calculator key accepts the salary draft");
        check(screen.click(125, 210) && screen.pending() &&
              screen.pending()->kind == ContractActionKind::submit_offer &&
              screen.pending()->term_column == 1 && screen.pending()->demand == 190000 &&
              screen.pending()->monthly_salary == 190000 && screen.pending()->quote_id == 8,
              "ANGEBOT MACHEN submits the mouse-selected quoted term and amount");
        auto offer_ack = quote;
        offer_ack.revision = 6;
        offer_ack.demands.reset();
        offer_ack.quote_id = 0;
        offer_ack.quote_row = -1;
        offer_ack.feedback = "Der Spieler hat angenommen.";
        screen.acknowledge_offer(offer_ack);
        bmh::ui::test::expect_notice_symbols(screen.render(),bmh::load_vga(std::filesystem::path(argv[1])/"KB/0.VGA"));
        check(!screen.pending() && screen.state().feedback == "Der Spieler hat angenommen.",
              "accepted offer ACK returns the view to the live roster");
        const auto notice = screen.state_json();
        check(!screen.process_timer_expired(100,100) && screen.state_json() == notice,
              "result notice keeps its physical KLICK rather than consuming the process deadline");
        check(screen.click(150, 170) && screen.state_json().find("\"page\":0") != std::string::npos,
              "KLICK closes the source-style manager response overlay back to the roster");
        check(screen.click(20,33,true), "another individual negotiation opens via right click");
        quote.revision=7;quote.quote_id=9;screen.acknowledge_quote(quote);
        check(screen.click(285, 210) && screen.state_json().find("\"page\":2") != std::string::npos,
              "PRÄMIEN opens the original individual bonus view");
        check(screen.click(30, 160) && screen.state_json().find("\"page\":3") != std::string::npos,
              "clicking a premium opens the bounded calculator");
        check(screen.click(76, 132) && screen.text("1") && screen.click(41, 132) &&
              screen.state_json().find("\"monthly_salary_input\":0") != std::string::npos,
              "premium calculator C and D keep edits local");
        check(screen.click(58, 132) && screen.text("3"), "R restores the initial premium before a new value");
        check(screen.click(94, 132), "J accepts the local premium draft");
        check(screen.click(125, 210) && screen.pending() &&
              screen.pending()->kind == ContractActionKind::submit_bonuses &&
              screen.pending()->row == 0 && screen.pending()->player_id == 150 &&
              screen.pending()->proposed_bonuses[0] == 3,
              "individual premium edit emits a typed player request");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "contracts UI test: %s\n", e.what());
        return 1;
    }
}
