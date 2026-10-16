// SPDX-License-Identifier: GPL-3.0-only
#include "fixture_tables.hpp"
#include "formats/executable_layout.hpp"
#include "formats/le.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

using namespace bmh;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void invalid(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("malformed fixture input was accepted");
}
void same(const std::vector<Fixture>& a, const std::vector<Fixture>& b) {
    check(a.size() == b.size(), "fixture count changed");
    for (size_t i = 0; i < a.size(); ++i)
        check(a[i].home == b[i].home && a[i].away == b[i].away, "fixture ownership changed");
}
void unit() {
    const auto authored = bmh::test::authored_fixture_tables();
    set_fixture_tables(authored);
    for (const auto& [teams, rows] : authored) {
        std::set<std::pair<int, int>> pairs;
        for (int round = 1; round < teams; ++round) {
            const auto actual = generate_fixtures(teams, round, teams - 1, 37);
            const auto reversed = generate_fixtures(teams, round + teams - 1, teams - 1, 37);
            const auto repeated = generate_fixtures(teams, round + 2 * (teams - 1), teams - 1, 37);
            check(actual.size() == size_t(teams / 2), "one match per authored pair");
            for (size_t match = 0; match < actual.size(); ++match) {
                check(actual[match].home == 37 + rows[size_t(round - 1)][match * 2] - 'a' &&
                      actual[match].away == 37 + rows[size_t(round - 1)][match * 2 + 1] - 'a',
                      "authored team letters resolve with the caller's base");
                check(reversed[match].home == actual[match].away && reversed[match].away == actual[match].home,
                      "return rounds reverse home and away");
                pairs.insert(std::minmax(actual[match].home, actual[match].away));
            }
            same(actual, repeated);
        }
        check(pairs.size() == size_t(teams * (teams - 1) / 2), "authored fixture inputs form a full round robin");
    }
    set_fixture_tables({{4, authored.at(4)}});
    const auto before = generate_fixtures(4, 1, 3, 0);
    invalid([] { generate_fixtures(6, 1, 5, 0); });
    invalid([] { set_fixture_tables({}); });
    invalid([] { set_fixture_tables({{8, {}}}); });
    auto wrong = authored;
    wrong.at(4).pop_back(); invalid([&] { set_fixture_tables(wrong); });
    wrong = authored; wrong.at(4)[0].pop_back(); invalid([&] { set_fixture_tables(wrong); });
    wrong = authored; wrong.at(4)[0][0] = 'z'; invalid([&] { set_fixture_tables(wrong); });
    wrong = authored; wrong.at(4)[0][0] = wrong.at(4)[0][1]; invalid([&] { set_fixture_tables(wrong); });
    wrong = authored; wrong.at(20).back()[0] = 'z'; invalid([&] { set_fixture_tables(wrong); });
    same(before, generate_fixtures(4, 1, 3, 0));
    invalid([] { generate_fixtures(6, 1, 5, 0); });
    const auto missing_tables = LeImage::from_bytes(layout::LeagueSizes, Bytes{1});
    invalid([&] { install_fixture_tables(missing_tables); });
    same(before, generate_fixtures(4, 1, 3, 0));
    invalid([] { generate_fixtures(6, 1, 5, 0); });
}
void data(const std::filesystem::path& directory) {
    const auto exe = load_le(directory / "BMH.exe");
    install_fixture_tables(exe);
    const std::map<int, uint32_t> german{
        {4, layout::CareerSession_RuntimeError_RuntimeField},
        {6, layout::CareerSession_RuntimeError_RuntimeField2},
        {12, layout::CareerSession_RuntimeError_RuntimeField3},
        {16, layout::CareerSession_RuntimeError_ExecutableField3},
        {18, layout::CareerSession_RuntimeError_ExecutableField},
        {20, layout::CareerSession_RuntimeError_ExecutableField2}};
    const std::map<int, uint32_t> english{
        {4, layout::EnglishFourTeamFixtureTable}, {6, layout::EnglishSixTeamFixtureTable},
        {12, layout::EnglishTwelveTeamFixtureTable}, {20, layout::EnglishTwentyTeamFixtureTable}};
    const bool is_english = exe.u8(layout::LeagueSizes) == 20;
    const auto& tables = is_english ? english : german;
    size_t count = 0;
    for (const auto& [teams, address] : tables) {
        check(exe.string_at_pointer(address).empty(), "runtime round zero is empty");
        for (int round = 1; round < teams; ++round) {
            const auto raw = exe.string_at_pointer(address + uint32_t(round) * 4);
            check(raw.size() == size_t(teams), "runtime round has exactly one byte per team");
            for (int half = 0; half < 4; ++half) {
                const auto actual = generate_fixtures(teams, round + half * (teams - 1), teams - 1, 37);
                check(actual.size() == size_t(teams / 2), "runtime fixture count");
                for (size_t match = 0; match < actual.size(); ++match) {
                    const auto home = size_t(match * 2 + (half % 2));
                    const auto away = size_t(match * 2 + 1 - (half % 2));
                    check(actual[match].home == 37 + raw[home] - 'a' &&
                          actual[match].away == 37 + raw[away] - 'a',
                          "every runtime pairing byte and home/away reversal is preserved");
                    ++count;
                }
            }
        }
    }
    if (is_english) {
        invalid([] { generate_fixtures(16, 1, 15, 0); });
        invalid([] { generate_fixtures(18, 1, 17, 0); });
    }
    std::cout << "Runtime edition: " << tables.size() << " tables, " << count << " exact pairings\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) unit();
        else if (argc == 2 && *argv[1]) data(argv[1]);
        else throw std::runtime_error("usage: fixture_table_tests [external data directory]");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
