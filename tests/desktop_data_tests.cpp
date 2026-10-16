// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
// Desktop acceptance against the selected game's own MEN, key table and graphics.
// Run manually with prepared English data and --english for the FLTD profile.
// No original assets are embedded; no save or data file is written.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "app/career_session.hpp"
#include "formats/binary.hpp"
#include "game/save.hpp"
#include "ui/desktop.hpp"

using namespace bmh;
using namespace bmh::ui;
namespace fs = std::filesystem;

namespace {
void check(bool ok, const std::string& why) {
    if (!ok) throw std::runtime_error(why);
}
uint8_t pixel(const Image& image, int x, int y) {
    return image.pixels.at(size_t(y) * size_t(image.width) + size_t(x));
}
const DeskItem* item_of(const DeskState& desk, int kind) {
    const auto it = std::find_if(desk.items.begin(), desk.items.end(),
        [=](const auto& item) { return item.kind == kind; });
    return it == desk.items.end() ? nullptr : &*it;
}
std::vector<int> linked_items(const Bytes& men, bool visible_only) {
    check(men.size() == 54 * 16, "MEN has the complete 54-record layout");
    std::set<int> seen;
    std::vector<int> result;
    for (int id = men.at(5); id; id = men.at(size_t(id) * 16 + 5)) {
        check(id > 0 && id < 54 && seen.insert(id).second, "MEN links are valid and acyclic");
        if (!visible_only || !(le16(men, size_t(id) * 16) & 0x1000)) result.push_back(id);
    }
    return result;
}
void compare_layout(const DeskState& desk, const Bytes& men) {
    const auto expected = linked_items(men, true);
    if (desk.items.size() != expected.size()) {
        std::cerr << "MEN visible=" << expected.size() << ", initialized desktop=" << desk.items.size() << '\n';
        for (const auto& item : desk.items)
            if (std::find(expected.begin(), expected.end(), item.id) == expected.end())
                std::cerr << "unexpected visible record=" << item.id << " kind=" << item.kind << '\n';
        for (const auto id : expected)
            if (std::none_of(desk.items.begin(), desk.items.end(), [=](const auto& item) { return item.id == id; }))
                std::cerr << "missing visible record=" << id << '\n';
        check(false, "projection preserves the loaded MEN visible count");
    }
    for (size_t n = 0; n < expected.size(); ++n) {
        const size_t at = size_t(expected[n]) * 16;
        const auto& item = desk.items[n];
        check(item.id == expected[n] && item.kind == men[at + 12] &&
              item.x == int(le16(men, at) & ~0x1000) && item.y == men[at + 2],
              "projection preserves loaded MEN order, kind and coordinates at record " + std::to_string(expected[n]));
    }
}
void pointer_and_keys(const fs::path& data, const LeImage& exe, const DeskState& desk) {
    int tested_keys = 0;
    for (const auto& item : desk.items) {
        if (item.kind >= 30 || item.disabled) continue;
        DesktopScreen mouse(data, desk);
        const auto clicked = mouse.click(item.x + item.width / 2, item.y + item.height / 2);
        check(clicked.item_id == item.id && clicked.menu_kind == item.kind &&
              clicked.kind == (item.kind == 12 ? DeskRequestKind::advance_turn : DeskRequestKind::open_menu),
              "loaded icon rectangle dispatches its own menu " + std::to_string(item.kind));

        const unsigned char key = exe.u8(::bmh::layout::Desktop_Key_RuntimeField + uint32_t(item.kind));
        if (!key) continue;
        // A duplicate shortcut belongs to the first visible matching menu.
        const bool first = std::none_of(desk.items.begin(), desk.items.end(), [&](const auto& other) {
            return other.kind < item.kind && other.kind < 30 && !other.disabled &&
                exe.u8(::bmh::layout::Desktop_Key_RuntimeField + uint32_t(other.kind)) == key;
        });
        if (!first) continue;
        DesktopScreen keyboard(data, desk);
        const auto pressed = keyboard.key(std::string(1, char(key)));
        check(pressed.menu_kind == item.kind && pressed.item_id == item.id,
              "loaded executable shortcut dispatches menu " + std::to_string(item.kind));
        DesktopScreen uppercase(data, desk);
        check(uppercase.key(std::string(1, char(std::toupper(key)))).menu_kind == item.kind,
              "loaded shortcut accepts uppercase input");
        ++tested_keys;
    }
    check(tested_keys >= 10, "exercise at least ten loaded executable shortcuts");
}
void selected_graphics(const fs::path& data, const DeskState& desk) {
    DesktopScreen screen(data, desk);
    const auto image = screen.render();
    const auto background = load_vga(asset_path(data / "KB", "129.VGA"));
    const auto icons = load_vga(asset_path(data / "KB", "3.VGA"));
    check(image.width == 320 && image.height == 240, "selected desktop assets render a complete frame");
    check(desk.decorated_background, "fresh selected-data desktop uses its decorated background");
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 320; ++x)
        check(pixel(image, x, y) == pixel(background, x, y), "desktop copies the selected KB/129 background");
    const auto* icon = item_of(desk, 1);
    check(icon != nullptr, "selected layout exposes the fixtures icon");
    for (int y = 0; y < 25; ++y) for (int x = 0; x < 32; ++x)
        check(pixel(image, icon->x + x, icon->y + y) == pixel(icons, 32 + x, y),
              "fixtures icon uses the selected KB/3 atlas at the loaded MEN position");
}
void english_profile(const fs::path& data, const LeImage& exe, const Bytes& men, const DeskState& desk) {
    check(exe.u8(::bmh::layout::LeagueSizes) == 20, "--english requires actual English league data");
    check(linked_items(men, false).size() == 38 && desk.items.size() == 37,
          "English default layout has 38 linked records and 37 visible items");
    check(men.at(10 * 16 + 12) == 9 && (le16(men, 10 * 16) & 0x1000) && !item_of(desk, 9),
          "English IndoorTable record remains hidden by the loaded MEN flag");
    for (const auto key : {std::string("H"), std::string(1, char(exe.u8(::bmh::layout::Desktop_Key_RuntimeField + 9)))}) {
        DesktopScreen screen(data, desk);
        check(screen.key(key).menu_kind != 9,
              "hidden English IndoorTable cannot be opened through its desk shortcut");
    }
    const auto* advertising = item_of(desk, 21);
    check(advertising && advertising->x == 1 && advertising->y == 180,
          "English advertising rectangle is relocated by MEN");
    DesktopScreen previous_position(data, desk);
    check(previous_position.click(52, 190).menu_kind == 22,
          "old German advertising position dispatches the actual English stadium icon");
    for (const auto [record, kind] : {std::pair{42, 61}, {43, 62}, {50, 69}}) {
        check(men.at(size_t(record) * 16 + 12) == kind && men.at(size_t(record) * 16 + 5) == 0xff &&
              !(le16(men, size_t(record) * 16) & 0x1000) && !item_of(desk, kind),
              "optional English drawer exists but starts unlinked: " + std::to_string(kind));
    }
}
void quickbar_drawers(const fs::path& data, app::CareerSession& session, uint64_t& revision) {
    for (const int kind : {61, 62, 69}) {
        auto desk = session.desktop_projection(revision);
        // This acceptance path exercises initially unlinked drawers. German data
        // can already show them; the English profile above requires their absence.
        if (item_of(desk, kind)) continue;
        DesktopScreen screen(data, desk);
        screen.pointer_move(160, 239);
        const int x = 22 + (kind - 50) * 12 + 6;
        screen.pointer_move(x, 221);
        const auto before = screen.render();
        const auto request = screen.click(x, 221);
        check(request.kind == DeskRequestKind::change_layout && request.menu_kind == kind &&
              request.layout_operation == DeskLayoutOperation::toggle_quickbar_drawer,
              "quickbar uses the selected optional drawer record");
        std::string error;
        check(session.dispatch_desktop_layout(request, revision, error), "real quickbar dispatch: " + error);
        screen.acknowledge(session.desktop_projection(revision));
        const auto* restored = item_of(screen.state(), kind);
        check(restored && restored->open, "ACK restores and opens optional drawer " + std::to_string(kind));
        const auto after = screen.render();
        bool changed = false;
        for (int yy = restored->y; yy < std::min(240, restored->y + restored->height); ++yy)
            for (int xx = restored->x; xx < std::min(320, restored->x + restored->width); ++xx)
                changed |= pixel(before, xx, yy) != pixel(after, xx, yy);
        check(changed, "existing optional drawer renderer paints its loaded rectangle");
        const auto close = screen.click(x, 221);
        check(session.dispatch_desktop_layout(close, revision, error), "real quickbar close: " + error);
        screen.acknowledge(session.desktop_projection(revision));
        check(!item_of(screen.state(), kind), "second quickbar action removes the optional drawer");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2 || (argc == 3 && std::string(argv[2]) == "--english"),
              "usage: desktop_data_tests <prepared-data> [--english]");
        const fs::path data = argv[1];
        const bool english = argc == 3;
        const auto exe = load_le(asset_path(data, "BMH.exe"));
        const auto men = read_file(asset_path(data, "M3.MEN"));
        app::CareerSession session(data, 20261016);
        SetupOptions options; options.difficulty = 3; options.world = false;
        const auto selection = session.selection_context(1);
        ManagerChoice choice; choice.name = "Desktop"; choice.master_club = selection.club_order.at(0);
        choice.selection_club = 0; choice.roster_revision = selection.revision;
        std::string error;
        check(session.confirm_manager(options, choice, error), "confirm genuine manager: " + error);
        CareerInitializationResult initialized;
        check(session.finish(initialized, error), "initialize genuine career: " + error);
        uint64_t revision = initialized.desktop.revision;
        const auto desk = session.desktop_projection(revision);
        const auto native = save_stream(session.state());
        const auto game = session.game(); const auto watcom = session.rand();
        compare_layout(desk, men);
        pointer_and_keys(data, exe, desk);
        selected_graphics(data, desk);
        if (english) english_profile(data, exe, men, desk);
        check(save_stream(session.state()) == native && session.game().seed() == game.seed() &&
              session.game().calls() == game.calls() && session.rand().seed() == watcom.seed() &&
              session.rand().calls() == watcom.calls(), "desktop reads/input requests preserve full Native and both RNGs");
        quickbar_drawers(data, session, revision);
        check(session.game().seed() == game.seed() && session.game().calls() == game.calls() &&
              session.rand().seed() == watcom.seed() && session.rand().calls() == watcom.calls(),
              "committed layout changes consume neither RNG");
        std::cout << "desktop_data_tests passed: " << desk.items.size() << " visible items"
                  << (english ? "; English hidden IndoorTable and optional drawers accepted" : "") << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "desktop_data_tests: " << e.what() << '\n';
        return 1;
    }
}
