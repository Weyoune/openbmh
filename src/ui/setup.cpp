// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "formats/resource_bundle.hpp"
#include "ui/setup.hpp"
#include "ui/setup_country_symbol.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace bmh::ui {
namespace {
constexpr std::array<int,4> country_button_x={46,105,164,223};

std::string folded(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void copy_rect(Image& dst, const Image& src, int sx, int sy, int dx, int dy,
               int width, int height, bool transparent = false) {
    if (sx < 0 || sy < 0 || sx + width > src.width || sy + height > src.height)
        throw FormatError("setup: original sprite rectangle outside atlas");
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            auto c = src.pixels[size_t(sy + y) * src.width + sx + x];
            if (dx + x >= 0 && dy + y >= 0 && dx + x < dst.width && dy + y < dst.height &&
                (!transparent || c != 0))
                dst.at(dx + x, dy + y) = c;
        }
}

}  // namespace

std::filesystem::path asset_path(const std::filesystem::path& directory, const std::string& name) {
    if (!std::filesystem::exists(directory / name) && resource_exists(directory / name)) return directory / name;
    std::filesystem::path found;
    for (const auto& e : std::filesystem::directory_iterator(directory)) {
        if (e.is_regular_file() && folded(e.path().filename().string()) == folded(name)) {
            if (!found.empty()) throw FormatError("ambiguous original asset: " + name);
            found = e.path();
        }
    }
    if (found.empty()) throw FormatError("missing original asset: " + (directory / name).string());
    return found;
}

SetupScreen::SetupScreen(const std::filesystem::path& data,SetupLeagueSymbol symbol)
    : background_(load_vga(asset_path(data / "KB", "134.VGA"))),
      atlas_(load_vga(asset_path(data / "KB", "20.VGA"))) {
    if (background_.width != 320 || background_.height != 240)
        throw FormatError("setup: expected original 320x240 background");
    const auto exe = load_le(asset_path(data, "BMH.exe"));
    for (int i = 0; i < 8; ++i) {
        button_x_[i] = exe.u16(::bmh::layout::Setup_FormatError_RuntimeField3 + i * 2);
        button_y_[i] = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField4 + i);
        joining_arrow_permission_[i] = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField11 + i);
    }
    for (int i = 0; i < 4; ++i) {
        minimum_[i] = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField5 + i);
        maximum_[i] = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField6 + i);
        display_width_[i] = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField + i);
        display_height_[i] = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField2 + i);
    }
    for (int i = 0; i < 3; ++i) {
        toggle_x_[i] = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField9 + i);
        toggle_source_x_[i] = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField7 + i);
    }
    for (int i = 0; i < 6; ++i) toggle_source_y_[i] = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField8 + i);
    button_source_y_ = exe.u8(::bmh::layout::Setup_FormatError_RuntimeField10);
    if (minimum_ != std::array<int, 4>{1, 0, 1, 1} || maximum_ != std::array<int, 4>{5, 9, 4, 3})
        throw FormatError("setup: unsupported original option table; verify this edition first");
    if (joining_arrow_permission_ != std::array<int, 8>{0, 1, 1, 0, 0, 1, 1, 0})
        throw FormatError("setup: unsupported Original joining-arrow table; verify this edition first");
    if(symbol==SetupLeagueSymbol::country_selection) {
        const auto prepared=load_vga(asset_path(data/"KB","SETUP12.VGA"));
        options_.country_selection=true;
        for(size_t control=0;control<4;++control)for(int active=0;active<2;++active)
            country_controls_[control][size_t(active)]=country_setup_button(prepared,control,active!=0);
    }
}

SetupScreen::SetupScreen(const std::filesystem::path& data, SetupOptions current,SetupLeagueSymbol symbol)
    : SetupScreen(data,symbol) {
    if (current.managers < minimum_[2] || current.managers > maximum_[2])
        throw FormatError("setup: joining manager count outside Original range");
    options_ = current;
    minimum_[2] = current.managers;
    joining_ = true;
    difficulty_arrows_locked_ = current.difficulty == 0;
}

SetupResult SetupScreen::click(int x, int y, bool right) {
    if (x < 0 || y < 0 || x >= 320 || y >= 240) return {};
    if (right) return {false, SetupRequest::new_game};
    if(!joining_ && options_.country_selection && y>=199 && y<239) {
        for(size_t i=0;i<4;++i)if(x>=country_button_x[i]&&x<country_button_x[i]+51) {
            if(i==3)return {false,SetupRequest::load_game};
            const auto before=options_;
            if(i==0)options_.germany=!options_.germany;
            if(i==1)options_.england=!options_.england;
            if(i==2)options_.world=!options_.world;
            options_.league=options_.germany||options_.england;
            if((!options_.league&&!options_.world)||(options_.managers>1&&!options_.league))options_=before;
            return {options_!=before,SetupRequest::none};
        }
        return {};
    }
    if (!joining_ && !options_.country_selection) {
        for (int i = 0; i < 3; ++i) {
            if (x >= toggle_x_[i] && x <= toggle_x_[i] + 0x33 && y > 198 && y < 240) {
                if (i == 2) return {false, SetupRequest::load_game};
                auto before = options_;
                if (i == 0 && (options_.managers < 2 || !options_.league)) options_.league = !options_.league;
                if (i == 1) options_.world = !options_.world;
                if (!options_.league && !options_.world) options_ = before;
                return {options_ != before, SetupRequest::none};
            }
        }
    }
    int* values[] = {&options_.duration, &options_.difficulty, &options_.managers, &options_.mode};
    for (int b = 0; b < 8; ++b) {
        if (joining_ && (!joining_arrow_permission_[b] ||
                         (difficulty_arrows_locked_ && (b == 1 || b == 5))))
            continue;
        if (x >= button_x_[b] && x <= button_x_[b] + 0x33 &&
            y >= button_y_[b] && y <= button_y_[b] + 0x28) {
            const int field = b % 4, old = *values[field];
            // SetupRuntimeField changes to 1 when league play is switched off.
            const int limit = field == 2 && !joining_ && !options_.league ? 1 : maximum_[field];
            *values[field] = std::clamp(old + (b < 4 ? -1 : 1), minimum_[field], limit);
            return {old != *values[field], SetupRequest::none};
        }
    }
    return {};
}

Image SetupScreen::render() const {
    Image out = background_;
    for (int b = 0; b < 8; ++b) {
        if (joining_ && (!joining_arrow_permission_[b] ||
                         (difficulty_arrows_locked_ && (b == 1 || b == 5))))
            continue;
        copy_rect(out, atlas_, 0x36 + (b > 3 ? 0x26 : 0), button_source_y_,
                  button_x_[b], button_y_[b], 0x26, 0x26);
    }
    const int values[] = {options_.duration, options_.difficulty, options_.managers, options_.mode};
    for (int i = 0; i < 4; ++i) {
        Image strip(179, 32, 0);
        const int value = values[i];
        int width = display_width_[i], height = display_height_[i];
        switch (i) {
            case 0:
                if (value < 5) {
                    width = value * 32; height = 32;
                    for (int k = 0; k < value; ++k)
                        copy_rect(strip, atlas_, 0x33, 0x6f, k * 32, 0, 31, 32);
                } else {
                    // Original requests a larger copy; only the displayed width survives.
                    copy_rect(strip, atlas_, 1, 0x95, 0, 0, width, height);
                }
                break;
            case 1:
                width = value * 15 + 30;
                copy_rect(strip, atlas_, 0, 0x78, width - 15, 0, 15, 29);
                for (int k = 1; k <= value; ++k)
                    copy_rect(strip, atlas_, 15, 0x78, width - 15 - k * 15, 0, 15, 29);
                copy_rect(strip, atlas_, 30, 0x78, 0, 0, 15, 29);
                break;
            case 2:
                width *= value;
                for (int k = 0; k < value; ++k)
                    copy_rect(strip, atlas_, 0x47, 0x50, k * 16, 0, 15, 31);
                break;
            case 3:
                width *= value;
                for (int k = 0; k < value; ++k)
                    copy_rect(strip, atlas_, 0x33, 0x4f, k * 21, 0, 20, 31);
                break;
        }
        copy_rect(out, strip, 0, 0, (0x9d - width / 2) & 0xff,
                  button_y_[i] + 4 + 0x10 - height / 2, width, height, true);
    }
    if(!joining_ && options_.country_selection) {
        // Clear the former three-control strip before drawing the four controls.
        const auto background=background_.pixels[239*320];
        for(int y=199;y<240;++y)for(int x=46;x<274;++x)out.at(x,y)=background;
        const bool active[]={options_.germany,options_.england,options_.world,true};
        for(size_t i=0;i<4;++i)copy_rect(out,country_controls_[i][size_t(active[i])],0,0,country_button_x[i],199,51,40);
    } else if (!joining_)
        for (int i = 0; i < 3; ++i) {
            const bool active = i == 0 ? options_.league : i == 1 ? options_.world : true;
            copy_rect(out, atlas_, toggle_source_x_[i], toggle_source_y_[i * 2 + int(active)],
                      toggle_x_[i], 199, 0x33, 0x28);
        }
    return out;
}

std::string setup_json(const SetupOptions& o) {
    std::ostringstream s;
    s << "{\"duration\":" << o.duration << ",\"unlimited\":" << (o.duration == 5 ? "true" : "false")
      << ",\"difficulty\":" << o.difficulty + 1 << ",\"managers\":" << o.managers
      << ",\"mode\":" << o.mode << ",\"league\":" << (o.league ? "true" : "false")
      << ",\"world\":" << (o.world ? "true" : "false");
    if(o.country_selection)s<<",\"country_selection\":true,\"germany\":"<<(o.germany?"true":"false")<<",\"england\":"<<(o.england?"true":"false");
    s<<'}';
    return s.str();
}
bmh::world12::GameScope setup_game_scope(const SetupOptions& options) {
    if(!options.country_selection || options.league!=(options.germany||options.england))throw FormatError("setup: country scope requires the 1.2 selection controls");
    bmh::world12::GameScope scope{options.germany,options.england,options.world};scope.validate();return scope;
}

}  // namespace bmh::ui
