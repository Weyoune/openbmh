#include "runtime_fixture_tables.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#pragma once

#include <array>
#include <filesystem>
#include <utility>

#include "formats/fbm.hpp"
#include "formats/le.hpp"
#include "formats/savestate.hpp"
#include "formats/textd.hpp"
#include "game/season.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace promotion_test {
using namespace bmh;

struct NativeState {
    LeImage exe;
    TextDb texts;
    GameState game;
    NativeState(const std::filesystem::path& data,const std::filesystem::path& fbm)
        : exe(bmh::test::load_executable(ui::asset_path(data,"BMH.exe"))),texts(load_textd(ui::asset_path(data,"TEXTD.BLK"))) {
        const auto file=load_savegame(fbm);
        if(!file.checksum_ok||file.version!="V2.24")throw FormatError("fixture FBM is not a valid V2.24 save");
        const DbCounts counts{exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField3),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField2),exe.u32(::bmh::layout::CareerSession_DataDirectory_RuntimeField4)};
        const SaveState parsed(file.stream,counts);
        game=GameState::from_save(parsed,counts);
        game.texts=&texts;
    }
};

inline SeasonPromotionEvent event_for(int group) {
    static const std::array<std::vector<uint16_t>,4> down{{
        {8,3,12},{19,18,29,34},{37,44,57,67},{81,78,80}}};
    static const std::array<std::vector<uint16_t>,4> up{{
        {24,27,35},{70,36,51,62},{73,87,79,77},{144,138,162}}};
    if(group<0||group>=4)throw FormatError("invalid promotion test group");
    return {group,down[size_t(group)].size(),down[size_t(group)],up[size_t(group)]};
}
}
