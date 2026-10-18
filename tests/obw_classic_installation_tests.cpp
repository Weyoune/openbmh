// SPDX-License-Identifier: GPL-3.0-only
#include "import/classic.hpp"
#include "formats/executable_layout.hpp"
#include "launch/game_data.hpp"
#include "launch/fltd.hpp"
#include "game/career.hpp"
#include "game/season.hpp"
#include "game/save.hpp"
#include "game/schedule.hpp"
#include "game/desk_actions.hpp"
#include <bit>
#include <chrono>
#include <iostream>

using namespace bmh;
namespace fs = std::filesystem;
namespace {
void check(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
fs::path file(const fs::path& root, const std::string& name) {
    const auto value = launch::find_file(root, name);
    if (!value) throw std::runtime_error("missing " + name);
    return *value;
}
void install(const LeImage& exe) {
    install_fixture_tables(exe);
    uint8_t sizes[9], first[8], counts[8];
    for (uint32_t i = 0; i < 9; ++i) sizes[i] = exe.u8(layout::LeagueSizes + i);
    set_league_sizes(sizes); kEnglishCompetitions = sizes[0] == 20;
    for (uint32_t i = 0; i < 8; ++i) {
        first[i] = exe.u8(layout::CalendarFirstLeague + i); counts[i] = exe.u8(layout::CalendarLeagueCounts + i);
        kGroupBelow[i] = exe.u8(layout::CareerSession_InstallLeagueSizes_RuntimeField5 + i);
        kGroupBelowCount[i] = exe.u8(layout::CareerSession_InstallLeagueSizes_RuntimeField4 + i);
    }
    set_league_groups(first, counts);
    for (uint32_t i = 0; i < 9; ++i) {
        kLeagueRelegation[i] = exe.u8(layout::CareerSession_InstallLeagueSizes_RuntimeField2 + i);
        kLeagueEurope[i] = exe.u8(layout::CareerSession_InstallLeagueSizes_RuntimeField + i);
        kLeagueSuffix[i] = i < 5 ? char(exe.u8(layout::CareerSession_InstallLeagueSizes_RuntimeField3 + i * 2)) : 0;
    }
    kNewcomerFirst = exe.u16(layout::CareerSession_DispatchLineup_RuntimeField);
    kNewcomerCount = exe.u16(layout::CareerSession_InstallLeagueSizes_RuntimeField6);
}
struct Run { Bytes state; int32_t game; uint32_t watcom; uint64_t game_calls, watcom_calls; };
Run start(const LeImage& exe, const Database& db, const TextDb& texts, const Bytes& men, const Bytes& dat) {
    install(exe);
    auto state = startup_state(exe, db, men, dat);
    state.texts = &texts;
    state.currency_suffix = exe.cstring(layout::CurrencySuffixText);
    configure_training_camp_data(state, exe);
    for (size_t i = 0; i < state.credit_rate_table.size(); ++i) state.credit_rate_table[i] = exe.u8(layout::BankCredit_BankCreditContext_RuntimeField3 + uint32_t(i));
    state.credit_divisor = std::bit_cast<double>(uint64_t(exe.u32(layout::BankCredit_BankCreditContext_RuntimeField)) |
        uint64_t(exe.u32(layout::BankCredit_BankCreditContext_RuntimeField2)) << 32);
    state.credit_rate_table_available = true;
    GameRandom game; WatcomRandom watcom(8128); MinuteReport report;
    startup_players(state, game, watcom);
    check(new_career_before_dialog(state, game, watcom, report), "pre-dialog initialization: " + report.reason);
    state.memory.set8(layout::CareerDuration, 20);
    state.memory.set8(layout::Difficulty, 1); state.level = 1;
    state.memory.set8(layout::ManagerCount, 1);
    state.memory.set8(layout::GameMode, 3);
    state.memory.set32(layout::CompetitionSelection, 3);
    state.manager_records.at(0x21c) = 1;
    career_managers_begin(state);
    check(career_add_manager(state, exe, 0, {"IMPORT QA", 0, 0, 1, 0}, false, game, watcom, report), "manager initialization: " + report.reason);
    career_managers_end(state, false);
    check(career_season_setup(state, game, watcom, report), "season setup: " + report.reason);
    check(career_first_steps(state, game, watcom, report), "first steps: " + report.reason);
    // Compare in RAM only: this is an installation test, never an FBM converter.
    return {save_stream(state), game.seed(), watcom.seed(), game.calls(), watcom.calls()};
}
} // namespace
int main(int argc, char** argv) {
    fs::path temp;
    try {
        if (argc < 2 || std::string(argv[1]).empty()) return 77;
        const fs::path root = argv[1];
        const bool english = bool(launch::find_file(root, "FLTD.EXE")) && !launch::find_file(root, "BMH.EXE");
        const auto exe = english ? launch::translate_fltd(file(root, "FLTD.EXE")) : load_le(file(root, "BMH.EXE"));
        const auto source = english ? launch::translate_fltd(file(root, "FLTD.EXE"), false) : load_le(file(root, "BMH.EXE"), false);
        const auto db = load_database(file(root, english ? "M3E.MAN" : "M3D.MAN"));
        const auto texts = load_textd(file(root, english ? "TEXTE.BLK" : "TEXTD.BLK"));
        const auto men = read_file(file(root, "M3.MEN")), dat = read_file(file(root, "M3.DAT"));
        const auto original_run = start(exe, db, texts, men, dat);
        const auto converted = obw::import_installation(root, {"2026-07-01", "Private installation test"});
        temp = fs::temp_directory_path() / ("openbmh-classic-qa-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(temp);
        obw::write(converted, temp / "world.obw", false);
        auto imported = obw::load_classic(obw::read(temp / "world.obw"));
        const auto imported_run = start(imported.executable, imported.database, imported.texts, imported.desktops, imported.formations);
        check(original_run.state == imported_run.state, "new-career first-season state differs after OBW round trip");
        check(original_run.game == imported_run.game && original_run.watcom == imported_run.watcom &&
            original_run.game_calls == imported_run.game_calls && original_run.watcom_calls == imported_run.watcom_calls,
            "initialization random sequence differs");
        for (uint32_t at = layout::GermanDataSegmentBegin; at < layout::GermanImageEnd; ++at) {
            check(exe.u8(at) == imported.executable.u8(at), "effective data-table byte changed");
            check(source.u8(at) == imported.source_tables.u8(at), "source data-table byte changed");
        }
        if (english) for (uint32_t at = layout::EnDataStart; at < layout::Career_CompetitionPointersEnglish_RuntimeField; ++at)
            check(exe.u8(at) == imported.executable.u8(at) && source.u8(at) == imported.source_tables.u8(at), "English data-table byte changed");
        for (const auto address : {layout::FreeKickTakerLabel, layout::PenaltyTakerLabel, layout::CornerTakerLabel, layout::AutomaticTakersLabel})
            check(exe.cstring(address) == imported.executable.cstring(address) && source.cstring(address) == imported.source_tables.cstring(address), "embedded literal changed");
        for (const auto& [name, image] : imported.images) {
            if (!name.starts_with("kb/") && !name.starts_with("w/")) continue;
            const auto folder = file(root, name.starts_with("kb/") ? "KB" : "W");
            const auto original = load_vga(file(folder, name.substr(name.find('/') + 1) + ".VGA"));
            check(original.width == image.width && original.height == image.height && original.pixels == image.pixels, "original atlas pixels changed");
        }
        const auto pal = load_palette(file(root, "M3PC.COL"));
        check(imported.palette.argb == pal.argb, "palette changed");
        for (const auto& [name, font] : imported.fonts) {
            const auto original = load_font(file(file(root, "F"), name));
            check(original.bitmap == font.bitmap && original.height == font.height && original.glyphs.size() == font.glyphs.size(), "font changed");
        }
        for (const auto& [name, bytes] : imported.audio) check(bytes == read_file(file(file(root, "S"), name)), "audio bytes changed");
        std::cout << (english ? "FLTD" : "BMH") << " Classic: " << converted.size() << " entries; " << original_run.state.size()
            << " first-season bytes identical; RNG draws G=" << original_run.game_calls << " W=" << original_run.watcom_calls
            << "; source/effective data regions, pixels, palette, fonts and audio retained\n";
        fs::remove_all(temp);
        return 0;
    } catch (const std::exception& error) {
        if (!temp.empty()) { std::error_code ec; fs::remove_all(temp, ec); }
        std::cerr << error.what() << '\n'; return 1;
    }
}
