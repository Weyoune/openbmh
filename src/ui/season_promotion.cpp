// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/season_promotion.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

#include "formats/binary.hpp"
#include "formats/font.hpp"
#include "formats/textd.hpp"
#include "game/season.hpp"
#include "game/state.hpp"
#include "ui/manager.hpp"

namespace bmh::ui {
namespace {
constexpr std::array<int,38> kScanlineOrder{
    12,34,7,16,22,9,31,19,2,37,1,14,21,28,24,4,36,13,5,
    30,3,32,23,25,8,11,35,17,26,20,6,27,15,33,18,29,10,0};

void copy(Image& dst,const Image& src,int sx,int sy,int x,int y,int width,int height,
          bool transparent=true) {
    // FormatError2 rejects the complete rectangle instead of drawing a clipped
    // fragment. This also keeps the eight-entry Source list inside the surface.
    if(width<=0||height<=0||sx<0||sy<0||x<0||y<0||
       width>src.width-sx||height>src.height-sy||
       width>dst.width-x||height>dst.height-y)return;
    for(int yy=0;yy<height;++yy)for(int xx=0;xx<width;++xx) {
        const int dx=x+xx,dy=y+yy,source_x=sx+xx,source_y=sy+yy;
        if(dx<0||dy<0||dx>=dst.width||dy>=dst.height||source_x<0||source_y<0||
           source_x>=src.width||source_y>=src.height)continue;
        const uint8_t pixel=src.pixels[size_t(source_y)*size_t(src.width)+size_t(source_x)];
        if(!transparent||pixel!=0)dst.at(dx,dy)=pixel;
    }
}

void source_badge_frame(Image& out,int x,int y) {
    for(int xx=x-1;xx<=x+38;++xx)if(xx>=0&&xx<out.width&&y-1>=0&&y-1<out.height)out.at(xx,y-1)=0x46;
    for(int yy=y-1;yy<=y+38;++yy)if(yy>=0&&yy<out.height&&x-1>=0&&x-1<out.width)out.at(x-1,yy)=0x46;
    for(int xx=x-1;xx<=x+38;++xx)if(xx>=0&&xx<out.width&&y+38>=0&&y+38<out.height)out.at(xx,y+38)=0x40;
    for(int yy=y-1;yy<=y+38;++yy)if(yy>=0&&yy<out.height&&x+38>=0&&x+38<out.width)out.at(x+38,yy)=0x40;
}

int team_x(bool relegated) { return relegated?10:273; }
int team_y(bool relegated,int row) { return (relegated?23:63)+40*row; }
int name_x(bool relegated) { return relegated?111:206; }
uint8_t name_color(bool relegated) { return relegated?251:122; }
}

SeasonPromotionScreen::SeasonPromotionScreen(const std::filesystem::path& data_directory,
                                             SeasonPromotionProjection projection)
    : projection_(std::move(projection)),
      exe_(load_le(asset_path(data_directory,"BMH.exe"))),
      background_(load_vga(asset_path(data_directory/"KB","25.VGA"))),
      widgets_(load_vga(asset_path(data_directory/"KB","0.VGA"))),
      texts_(load_textd(asset_path(data_directory,"TEXTD.BLK"))),
      sprite_font_(data_directory),font_(load_font(asset_path(data_directory/"F","BMH3.FNT"))) {
    if(projection_.group<0||projection_.group>3||projection_.count>8||
       projection_.relegated.size()<projection_.count||projection_.promoted.size()<projection_.count||
       projection_.scanline_steps<0||projection_.scanline_steps>int(kScanlineOrder.size())||
       background_.width!=320||background_.height!=240||texts_.strings.size()<=1251)
        throw FormatError("season promotion: invalid projection or Original assets");
    for(size_t i=0;i<projection_.count;++i) {
        for(const auto* team:{&projection_.relegated[i],&projection_.promoted[i]}) {
            if(team->original_name.empty())throw FormatError("season promotion: empty Source club name");
            if(!team->badge_fallback) {
                const int atlas=(team->badge_selector&0x1ff)>>3;
                if(!badge_atlases_.contains(atlas))
                    badge_atlases_.emplace(atlas,load_vga(asset_path(data_directory/"W",std::to_string(atlas)+".VGA")));
            }
        }
    }
}

Image SeasonPromotionScreen::render() const { return render(projection_.scanline_steps); }

Image SeasonPromotionScreen::render(int scanline_steps) const {
    if(scanline_steps<0||scanline_steps>int(kScanlineOrder.size()))
        throw FormatError("season promotion: scanline step outside Source range");
    // SeasonPromotionScreen installs KB/25 as its non-null source background. The
    // PlainWindowFrameUsedBy non-null branch copies that full image independently of PlainBackground.
    Image out=background_;
    const std::string heading=original_to_utf8(texts_[1251]);
    const int heading_left=172+(282-172)/2-sprite_font_.text_width(heading)/2;
    sprite_font_.draw(out,heading_left,13,heading,5);
    sprite_font_.draw(out,5,235,original_to_utf8(projection_.league_footer),5);

    const bool gradient=projection_.gradient_text&&exe_.u8(::bmh::layout::TextGradientMode)==2;
    auto draw_name=[&](bool relegated,size_t row,const std::string& bytes) {
        const int center=name_x(relegated),baseline=201+6*int(row);
        const int width=font_.text_width(bytes),left=center-width/2,top=baseline-font_.baseline;
        const int source_baseline=exe_.u8(::bmh::layout::SeasonPromotion_FormatError_RuntimeFieldUnsigned+3u*4u);
        if(left<0||baseline-source_baseline<0||left+width+2>out.width||baseline+2>out.height)return;
        // DrawnFirstBy flags 0x40 draws the +2px shadow then Source text.
        font_.draw(out,left+2,top+2,bytes,0x40);
        const uint8_t color=name_color(relegated);
        int ramp=0;
        while(ramp<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2Unsigned+uint32_t(ramp))!=color)++ramp;
        if(!gradient||ramp==32){font_.draw(out,left,top,bytes,color);return;}
        Image mask(std::max(1,width),font_.height,0);
        font_.draw(mask,0,0,bytes,1);
        for(int yy=0;yy<font_.height;++yy)for(int xx=0;xx<width;++xx)
            if(mask.at(xx,yy))out.at(left+xx,top+yy)=exe_.u8(::bmh::layout::Advertising_Text_RuntimeFieldUnsigned+uint32_t(ramp*6+std::min(yy,5)));
    };

    auto draw_team=[&](bool relegated,size_t row,const SeasonPromotionClub& team) {
        const int x=team_x(relegated),y=team_y(relegated,int(row));
        source_badge_frame(out,x,y);
        if(team.badge_fallback)copy(out,widgets_,8,0,x,y,38,38,false);
        else {
            const Image& atlas=badge_atlases_.at((team.badge_selector&0x1ff)>>3);
            copy(out,atlas,(team.badge_selector&7)*38,0,x,y,38,38,false);
        }
        draw_name(relegated,row,team.original_name);
    };
    for(size_t i=0;i<projection_.count;++i) {
        draw_team(true,i,projection_.relegated[i]);
        draw_team(false,i,projection_.promoted[i]);
    }

    // Source SeasonPromotionScreen selects these offsets from its EXE table and exchanges
    // one 38x1 stripe in each direction per presentation step. No timing, input,
    // RNG, or career state is owned by this renderer.
    for(int step=0;step<scanline_steps;++step) {
        const int offset=kScanlineOrder[size_t(step)];
        for(size_t row=0;row<projection_.count;++row) {
            const int down_y=team_y(true,int(row))+offset;
            const int up_y=team_y(false,int(row))+offset;
            if(down_y<0||up_y<0||down_y>=out.height||up_y>=out.height||
               team_x(true)+38>out.width||team_x(false)+38>out.width)continue;
            std::array<uint8_t,38> down{},up{};
            for(int x=0;x<38;++x) {
                down[size_t(x)]=out.pixels[size_t(down_y)*out.width+team_x(true)+x];
                up[size_t(x)]=out.pixels[size_t(up_y)*out.width+team_x(false)+x];
            }
            for(int x=0;x<38;++x) {
                out.at(team_x(true)+x,down_y)=up[size_t(x)];
                out.at(team_x(false)+x,up_y)=down[size_t(x)];
            }
        }
    }
    return out;
}

SeasonPromotionProjection season_promotion_from_game(
        const GameState& game,const SeasonPromotionEvent& event,
        const LeImage& exe,const TextDb& texts) {
    static constexpr std::array<int,4> first_league{0,1,2,4};
    static constexpr std::array<char,5> suffix{0,0,'A','B',0};
    if(event.group<0||event.group>=int(first_league.size())||event.count>8||
       event.relegated.size()<event.count||event.promoted.size()<event.count)
        throw FormatError("season promotion: invalid Source event");

    const int league=first_league[size_t(event.group)];
    int name_index=0;
    for(int i=0;i<league;++i)if(uint8_t(suffix[size_t(i+1)])<'B')++name_index;
    constexpr uint32_t text_base=::bmh::layout::TextPointerTable;
    constexpr uint32_t league_name_table=::bmh::layout::Achievements_FormatError_TextPointer3;
    const size_t footer_index=size_t((league_name_table-text_base)/4)+size_t(name_index);
    const size_t suffix_index=size_t((::bmh::layout::Manager_FormatError_TextPointer2-text_base)/4);
    if(footer_index>=texts.strings.size()||suffix_index>=texts.strings.size())
        throw FormatError("season promotion: missing Source league labels");
    // Reuse the game's Source LeagueName implementation when the live state is
    // attached to its TextDb; the fallback uses the supplied owned database.
    std::string footer;
    if(game.texts)footer=league_name(game,league);
    else {
        footer=texts[footer_index];
        if(suffix[size_t(league)]!=0) {
            footer+=texts[suffix_index];
            if(!footer.empty())footer.back()=suffix[size_t(league)];
        }
    }

    SeasonPromotionProjection out;
    out.group=event.group;out.count=event.count;out.league_footer=std::move(footer);
    out.gradient_text=(!game.memory.has(::bmh::layout::TextGradientDisabled)||game.memory.u8(::bmh::layout::TextGradientDisabled)==0)&&exe.u8(::bmh::layout::TextGradientMode)==2;
    auto project=[&](const std::vector<uint16_t>& ids,std::vector<SeasonPromotionClub>& rows) {
        rows.reserve(event.count);
        for(size_t i=0;i<event.count;++i) {
            const uint16_t id=ids[i]&0x0fff;
            const bool fallback=id==0x0fff||(id>0x59&&((id<0x8a||id>0xa7)&&id<0x165));
            uint16_t badge=0;
            if(!fallback) {
                const size_t at=size_t(id)*2;
                if(at+1>=game.club_heap_78.size())throw FormatError("season promotion: missing Source badge selector");
                badge=uint16_t(game.club_heap_78[at]|uint16_t(game.club_heap_78[at+1])<<8);
            }
            rows.push_back({id,badge,fallback,game.club_name(id)});
        }
    };
    project(event.relegated,out.relegated);project(event.promoted,out.promoted);
    return out;
}

} // namespace bmh::ui
