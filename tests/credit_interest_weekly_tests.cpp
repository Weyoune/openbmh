// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <bit>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "app/career_session.hpp"
#include "formats/le.hpp"
#include "game/weekly.hpp"

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected BMH data directory");
        bmh::app::CareerSession session(argv[1], 0x351b);
        const auto choices = session.selection_context(1);
        bmh::ui::SetupOptions options;
        bmh::ui::ManagerChoice choice;
        choice.slot = 0;
        choice.name = "Kredit";
        choice.master_club = choices.club_order.front();
        choice.selection_club = 0;
        choice.roster_revision = choices.revision;
        std::string error;
        if (!session.confirm_manager(options, choice, error)) throw std::runtime_error(error);
        bmh::ui::CareerInitializationResult initialized;
        if (!session.finish(initialized, error)) throw std::runtime_error(error);
        const bmh::LeImage exe = bmh::load_le(std::filesystem::path(argv[1]) / "BMH.exe");

        const auto& baseline = session.state();
        const int manager = int(baseline.memory.u8(::bmh::layout::ActiveManager));
        const uint32_t loan = ::bmh::layout::Loans + uint32_t(manager) * 0x78;
        bool reached = false;
        for (uint32_t seed = 1; seed <= 3000 && !reached; ++seed) {
            bmh::GameState state = baseline;
            state.level = 1;
            state.memory.set32(loan, 100000);
            state.memory.set32(loan + 4, 120000);
            state.memory.set32(loan + 8, 2500);
            state.memory.set32(loan + 12, 48);
            state.memory.set32(loan + 16, 12);
            state.memory.set32(loan + 20, 30000);
            bmh::WatcomRandom random(seed);
            bmh::MinuteReport report;
            bmh::manager_offers(state, manager, random, report);
            if (state.memory.u32(loan) == 100000) continue;
            reached = true;
            const uint8_t rate = state.manager_records[size_t(manager) * 0xb0c + 0x179];
            const int32_t principal = 750 * (100 - rate);
            const int32_t factor = exe.u8(::bmh::layout::BankCredit_BankCreditContext_RuntimeField3 + 36);
            const int32_t base = principal * factor / 100;
            const int32_t denominator = 10000 / (factor - 100);
            const double divisor = std::bit_cast<double>(uint64_t(exe.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField)) | uint64_t(exe.u32(::bmh::layout::BankCredit_BankCreditContext_RuntimeField2)) << 32);
            const double raw_payment = (double(base) / divisor) * denominator / divisor;
            const int32_t expected_payment = std::max(1, int32_t(std::trunc(raw_payment)));
            if (report.unsupported || state.memory.u32(loan) != 750 * (100 - rate) ||
                state.memory.u32(loan + 4) != state.memory.u32(loan + 8) * 36 ||
                state.memory.u32(loan + 8) != uint32_t(expected_payment) || state.memory.u32(loan + 12) != 36 ||
                state.memory.u32(loan + 16) != 0 || state.memory.u32(loan + 20) != 0 ||
                rate < 10 || rate > 30 || rate % 10 != 0 || state.mailbox.empty() ||
                state.mailbox[10] == 0)
                throw std::runtime_error("Source credit-rate recalc did not preserve native terms");
        }
        if (!reached) throw std::runtime_error("test seeds did not reach weekly kind 9");
        std::puts("weekly credit interest: kind 9 recalculated principal, term and payment");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
