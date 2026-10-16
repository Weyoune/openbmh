// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/game_end.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

#include "formats/font.hpp"
#include "formats/le.hpp"
#include "ui/manager.hpp"
#include "ui/setup.hpp"
#include "ui/sprite_font.hpp"

namespace bmh::ui {
namespace {
void copy(Image& d,const Image& s,int sx,int sy,int x,int y,int w,int h,bool transparent=false) {
    for(int yy=0;yy<h;++yy) for(int xx=0;xx<w;++xx) {
        const auto p=s.pixels[size_t(sy+yy)*size_t(s.width)+size_t(sx+xx)];
        if(x+xx>=0&&y+yy>=0&&x+xx<d.width&&y+yy<d.height&&(!transparent||p)) d.at(x+xx,y+yy)=p;
    }
}
void fill(Image& im,int x,int y,int w,int h,uint8_t c) {
    for(int yy=std::max(0,y);yy<std::min(im.height,y+h);++yy)
        for(int xx=std::max(0,x);xx<std::min(im.width,x+w);++xx) im.at(xx,yy)=c;
}
Image source_base(const Image& original,bool plain_background,bool has_header_footer_text) {
    if(!plain_background) {
        Image out=original;
        if(!has_header_footer_text){fill(out,0,0,320,19,0x44);fill(out,0,228,320,11,0x44);}
        return out;
    }
    Image out(320,240,0);
    fill(out,0,4,320,13,0x44);fill(out,1,5,318,11,0x39);
    fill(out,0,228,320,10,0x44);fill(out,1,229,318,8,0x39);
    if(!has_header_footer_text){fill(out,0,0,320,19,0);fill(out,0,228,320,11,0);}
    return out;
}
void raw_text(Image& im,const Font& f,int x,int baseline,std::string_view bytes,uint8_t c) {
    const int top=baseline-f.baseline; if(top>=0&&top+f.height<=im.height) f.draw(im,x,top,bytes,c);
}
void source_text(Image& out,const Font& f,const LeImage& exe,int x,int baseline,
                 std::string_view bytes,uint8_t color,bool gradient) {
    const int top=baseline-f.baseline;
    if(top<0||top+f.height>out.height) return;
    if(!gradient||exe.u8(::bmh::layout::TextGradientMode)!=2||color==0x40){f.draw(out,x,top,bytes,color);return;}
    unsigned slot=0;while(slot<32&&exe.u8(::bmh::layout::Advertising_Text_RuntimeField2+slot)!=color)++slot;
    if(slot==32){f.draw(out,x,top,bytes,color);return;}
    Image mask(std::max(1,f.text_width(bytes)),f.height,0);
    f.draw(mask,0,0,bytes,1);
    for(int y=0;y<f.height;++y)for(int xx=0;xx<mask.width;++xx)
        if(mask.at(xx,y)&&x+xx>=0&&x+xx<out.width)
            out.at(x+xx,top+y)=exe.u8(::bmh::layout::Advertising_Text_RuntimeField+slot*6+std::min(y,5));
}
void source_text_shadow(Image& out,const Font& f,const LeImage& exe,int x,int baseline,
                        std::string_view bytes,uint8_t color,bool gradient) {
    // DrawnFirstBy rejects the complete label when shadow/ink exceed the viewport.
    if (x < 0 || x + 2 + f.text_width(bytes) > out.width || baseline + 2 > out.height) return;
    raw_text(out,f,x+2,baseline+2,bytes,0x40);
    source_text(out,f,exe,x,baseline,bytes,color,gradient);
}
class SourceHyphenator {
public:
    SourceHyphenator(const Font& font,const LeImage& exe):font_(font),exe_(exe),vowels_(exe.cstring(::bmh::layout::GameEnd_SourceHyphenator_ExecutableField)) {
        for(int i=0;i<30;++i) clusters_.push_back(exe_.cstring(exe_.u32(::bmh::layout::GameEnd_SourceHyphenator_RuntimeField+uint32_t(i)*4)));
    }
    std::vector<std::string> wrap(std::string_view text,int max_width,bool justify=false) const {
        if(max_width<=4)throw FormatError("Source paragraph: invalid width");
        std::vector<std::string> lines;std::string line;std::vector<size_t> ends;
        size_t at=0;
        while(at<text.size()&&text[at]) {
            std::string word;
            while(at<text.size()&&text[at]&&text[at]!=' '&&uint8_t(text[at])!=0xa0)word+=text[at++];
            std::string candidate=line+(line.empty()?"":" ")+word;
            while(font_.text_width(candidate)>max_width) {
                // FindBreak measures strlen(pending)+1, including glyph0.
                const int cut=find_break(word,max_width-4,
                    font_.text_width(line)+font_.glyph(0).advance);
                if(cut>=0) {
                    if(!line.empty()){line+=' ';ends.push_back(line.size()-1);}
                    std::string head=word.substr(0,size_t(cut+1));
                    if(head.back()=='c'&&word[size_t(cut+1)]=='k')head.back()='k';
                    line+=head;line+=exe_.cstring(::bmh::layout::Fltd_Layout_ExecutableField6);
                    word.erase(0,size_t(cut+1));
                } else if(line.empty()) {
                    // DOS cannot advance on an oversized indivisible word.
                    // Keep it intact for the caller's clipping, never hang or
                    // invent a syllable. Ordinary Source words take the path above.
                    line=word;word.clear();
                }
                if(!ends.empty())ends.pop_back();
                size_t gap=0;
                while(justify&&!ends.empty()&&font_.text_width(line)<max_width) {
                    // InsertsRealSpacesAtRecordedWordEnds inserts real spaces at recorded word ends,
                    // cycling left to right; the final line is not stretched.
                    line.insert(ends[gap],1,' ');
                    for(size_t i=gap+1;i<ends.size();++i)++ends[i];
                    if(++gap>=ends.size())gap=0;
                }
                lines.push_back(line);line.clear();ends.clear();candidate=word;
            }
            if(!line.empty())line+=' ';
            line+=word;ends.push_back(line.size());
            if(at<text.size()&&(text[at]==' '||uint8_t(text[at])==0xa0))++at;
        }
        if(!line.empty())lines.push_back(line);
        return lines;
    }
private:
    const Font& font_;const LeImage& exe_;std::string vowels_;std::vector<std::string> clusters_;
    bool vowel(char c) const { return vowels_.find(c)!=std::string::npos; }
    static char fold(char c) { const unsigned char u=static_cast<unsigned char>(c);return u>='A'&&u<='Z'?char(u+32):c; }
    int find_break(const std::string& word,int max_prefix_width,int preceding) const {
        int limit=int(word.size())-1;
        for(int cut=limit;cut>=0;--cut) {
            // Draw2 retains the previous over-width candidate as the
            // right-section limit; this is not always the end of the word.
            if(valid_break(word,cut,limit)) {
                const auto prefix=std::string_view(word).substr(0,size_t(cut+1));
                if(preceding+font_.text_width(prefix)<max_prefix_width)return cut;
                limit=cut;
            }
        }
        return -1;
    }
    bool has_vowel(std::string_view part) const {
        for(char c:part)if(vowel(c))return true;
        return false;
    }
    bool valid_break(const std::string& word,int cut,int limit) const {
        const int left_length=cut+1,right_length=limit-cut;
        if(left_length<=1||right_length<=1)return false;
        const auto left=std::string_view(word).substr(0,size_t(left_length));
        const auto right=std::string_view(word).substr(size_t(cut+1),size_t(right_length));
        if(!has_vowel(left)||!has_vowel(right))return false;
        if(cut!=0&&vowel(word[size_t(cut+1)]))return false;
        // Int8 rejects a dictionary cluster that crosses this split.
        for(int start=cut;start>=0&&!vowel(word[size_t(start)]);--start) {
            for(const auto& cluster:clusters_) {
                if(cluster.empty()||start+int(cluster.size())<=cut+1)continue;
                if(start+int(cluster.size())>int(word.size()))continue;
                bool equal=true;
                for(size_t j=0;j<cluster.size();++j)
                    if(fold(word[size_t(start)+j])!=fold(cluster[j])){equal=false;break;}
                if(equal)return false;
            }
        }
        return true;
    }
};
void frame(Image& o,const Image& a,const LeImage& exe,int x,int y,int w,int h,bool shadow) {
    if(shadow){fill(o,x+2,y+h,w,2,0x40);fill(o,x+w,y+2,2,h,0x40);}
    for(int yy=4;yy<h-3;++yy)for(int xx=4;xx<w-3;++xx)
        o.at(x+xx,y+yy)=exe.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+size_t((yy-4)&15)*16+size_t((xx-4)&15));
    copy(o,a,0,0,x,y,4,4);copy(o,a,4,0,x+w-4,y,4,4);copy(o,a,0,4,x,y+h-4,4,4);copy(o,a,4,4,x+w-4,y+h-4,4,4);
    for(int at=4;at<w-4;at+=74){int n=std::min(74,w-4-at);copy(o,a,0,104,x+at,y,n,4);copy(o,a,0,108,x+at,y+h-4,n,4);}
    for(int at=4;at<h-4;at+=96){int n=std::min(96,h-4-at);copy(o,a,0,8,x,y+at,4,n);copy(o,a,4,8,x+w-4,y+at,4,n);}
}
std::string source_number(int32_t value, unsigned minimum_digits) {
    const bool negative = value < 0;
    const auto magnitude = negative ? uint32_t(-int64_t(value)) : uint32_t(value);
    std::string digits = std::to_string(magnitude);
    const auto count = unsigned(digits.size());
    for (int at = int(digits.size()) - 3; at > 0; at -= 3) digits.insert(size_t(at), ".");
    const unsigned separators = (count - 1) / 3;
    const unsigned reserved = minimum_digits > 3 ? (minimum_digits - 4) / 3 + 1 : 0;
    std::string out = negative ? "-" : "";
    if (reserved > separators) out.append(reserved - separators, ' ');
    if (minimum_digits > count + unsigned(negative)) out.append(minimum_digits - count - unsigned(negative), '%');
    return out + digits;
}
void button(Image& o,const Image& a,const SpriteFont& sf,const LeImage& exe,int x,int y,int w,
            std::string_view label,bool selected=false){
    if(selected){const auto under=o.at(x,y+10);fill(o,x,y,w+1,2,under);fill(o,x,y,2,11,under);x+=2;y+=2;}
    else {fill(o,x+2,y+10,w,2,0x40);fill(o,x+w,y+2,2,10,0x40);}
    const int sy=selected?44:74;
    copy(o,a,24,sy,x,y,31,10);int used=31;
    while(w-used>20){int n=std::min(8,w-used-20);copy(o,a,24,sy+10,x+used,y,n,10);used+=n;}
    copy(o,a,45-(w-used),sy+20,x+used,y,w-used,10);
    sf.draw(o,x+w/2-sf.text_width(label)/2,y+7,label,exe.u8(::bmh::layout::Advertising_Button_RuntimeField + int(selected)));
}
std::string_view text_at(const TextDb& texts,uint32_t address) {
    constexpr uint32_t base=::bmh::layout::TextPointerTable;
    if(address<base||(address-base)%4)throw FormatError("game-end dialog: invalid TEXTD pointer");
    return texts[(address-base)/4];
}
uint32_t source_button_text(uint32_t table,uint8_t selector) {
    // These addresses are entries inside TextPointerTable, the loaded TEXTD
    // pointer table itself; reading the uninitialized EXE image yields zero.
    return table+uint32_t(selector)*4;
}
}
std::vector<std::string> wrap_source_text(const Font& font,const LeImage& exe,
    std::string_view text,int max_width,bool justify) {
    return SourceHyphenator(font,exe).wrap(text,max_width,justify);
}
std::vector<std::string> wrap_source_text(const std::filesystem::path& data,std::string_view text,int max_width) {
    const auto font=load_font(asset_path(data/"F","BMH3.FNT"));
    const auto exe=load_le(asset_path(data,"BMH.exe"));
    return wrap_source_text(font,exe,text,max_width);
}
struct GameEndHighscoreScreen::FontData { Font small; LeImage exe; SpriteFont sprite; explicit FontData(const std::filesystem::path& p):small(load_font(asset_path(p/"F","BMH3.FNT"))),exe(load_le(asset_path(p,"BMH.exe"))),sprite(p){} };
GameEndHighscoreScreen::GameEndHighscoreScreen(const std::filesystem::path& data,HighscoreProjection p,GameEndRenderOptions options)
 : projection_(std::move(p)),options_(options),background_(load_vga(asset_path(data/"KB","129.VGA"))),
   logo_(load_vga(asset_path(data/"KB","51.VGA"))),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),font_(std::make_shared<FontData>(data)) {}
Image GameEndHighscoreScreen::render() const {
    Image out=source_base(background_,options_.plain_background,true);
    if(logo_.width>=112&&logo_.height>=74){fill(out,102,99,112,2,0x40);fill(out,212,27,2,74,0x40);copy(out,logo_,0,0,100,25,112,74);}
    if(!projection_.ranked)return out;
    const auto& heading=texts_[893];
    font_->sprite.draw(out,SpriteFont::centered_title_x(font_->sprite.text_width(original_to_utf8(heading))),13,original_to_utf8(heading),5);
    for(size_t i=0;i<projection_.rows.size();++i){
        const auto& r=projection_.rows[i];if(!r.score)continue;
        const int baseline=120+int(i)*10;
        const std::string left=std::to_string(i+1)+". "+r.manager_name;
        const std::string right=source_number(r.score,5)+" ("+source_number(r.titles[0],1)+"/"+
            source_number(r.titles[1],1)+"/"+source_number(r.titles[2],1)+")";
        source_text_shadow(out,font_->small,font_->exe,33,baseline,left,0x5f,options_.gradient_text);
        source_text_shadow(out,font_->small,font_->exe,125,baseline,r.club_name,0x5f,options_.gradient_text);
        source_text_shadow(out,font_->small,font_->exe,230,baseline,right,0x5f,options_.gradient_text);
    }
    return out;
}
struct GameEndFinalScreen::FontData { Font final_font; explicit FontData(const std::filesystem::path& p):final_font(load_font(asset_path(p/"F","BMH2.FNT"))){} };
GameEndFinalScreen::GameEndFinalScreen(const std::filesystem::path& data,GameEndRenderOptions options)
 : background_(load_vga(asset_path(data/"KB","129.VGA"))),options_(options),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),font_(std::make_shared<FontData>(data)) {}
Image GameEndFinalScreen::render() const {
    Image out=source_base(background_,options_.plain_background,false);
    for(const auto& [id,y]:{std::pair<size_t,int>{1172,100},std::pair<size_t,int>{1173,140}}){
        const auto& text=texts_[id];const int w=font_->final_font.text_width(text);
        raw_text(out,font_->final_font,8+(311-w)/2,y,text,0x5f);
    }
    return out;
}
struct GameEndTaxDialog::FontData { Font small,large;LeImage exe;SpriteFont sprite;explicit FontData(const std::filesystem::path& p):small(load_font(asset_path(p/"F","BMH3.FNT"))),large(load_font(asset_path(p/"F","BMH2.FNT"))),exe(load_le(asset_path(p,"BMH.exe"))),sprite(p){} };
struct ProgramErrorScreen::FontData {
    Font font,header; LeImage exe;
    explicit FontData(const std::filesystem::path& p)
        : font(load_font(asset_path(p/"F","BMH3.FNT"))),
          header(load_font(asset_path(p/"F","BMH2.FNT"))), exe(load_le(asset_path(p,"BMH.exe"))) {}
};
ProgramErrorScreen::ProgramErrorScreen(const std::filesystem::path& data,uint16_t code,GameEndRenderOptions options)
    : code_(code),options_(options),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),font_(std::make_shared<FontData>(data)) {}
Image ProgramErrorScreen::render() const {
    Image out(320,240,0);
    const uint8_t color=code_<150 && (code_<91 || code_>99)?0x75:0xf7;
    const auto label=[&](std::string_view text,int baseline){
        source_text(out,font_->font,font_->exe,159-font_->font.text_width(text)/2,
                    baseline,text,color,options_.gradient_text);
    };
    // TextAt selects font2 for the heading. DrawnFirstBy resets it to font3
    // after its first label; param7=-1 gives no shadow, header no gradient.
    const auto heading=text_at(texts_,::bmh::layout::GameEnd_Out_TextPointer);
    raw_text(out,font_->header,159-font_->header.text_width(heading)/2,21,heading,color);
    for(int i=0;i<5;++i){
        std::string line(text_at(texts_,::bmh::layout::GameEnd_Line_TextPointer+uint32_t(i)*4));
        if(i==0) {
            const auto number=std::to_string(code_);
            // Replace(...,0x45) uses Source's '%' padding glyph, whose
            // blank advance differs from an ordinary space in BMH3.FNT.
            line.replace(14,5,std::string(5-number.size(),'%')+number);
        }
        label(std::move(line),60+i*10);
    }
    for(int n=0;n<3;++n){
        fill(out,n,n,320-2*n,1,color);fill(out,n,33-n,320-2*n,1,color);
        fill(out,n,n,1,34-2*n,color);fill(out,319-n,n,1,34-2*n,color);
    }
    return out;
}
GameEndTaxDialog::GameEndTaxDialog(const std::filesystem::path& data,TaxDialogProjection p,GameEndRenderOptions options,std::optional<Image> restored_backdrop)
 : projection_(std::move(p)),options_(options),restored_backdrop_(std::move(restored_backdrop)),background_(load_vga(asset_path(data/"KB","129.VGA"))),widgets_(load_vga(asset_path(data/"KB","0.VGA"))),
   faces_(load_vga(asset_path(data/"KB","26.VGA"))),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),font_(std::make_shared<FontData>(data)) {
    if(!projection_.badge_fallback)
        badge_=load_vga(asset_path(data/"W",std::to_string(std::max(0,projection_.badge_sprite)>>3)+".VGA"));
}
Image GameEndTaxDialog::backdrop() const {
    if(projection_.compact_dialog_frame)return Image(320,240,0);
    if(restored_backdrop_&&restored_backdrop_->width==320&&restored_backdrop_->height==240)return *restored_backdrop_;
    return source_base(background_,options_.plain_background,false);
}
Image GameEndTaxDialog::render(std::optional<Button> pressed) const {
    Image out=backdrop();
    draw_source_dialog(out,{widgets_,faces_,&badge_,font_->small,font_->large,
        font_->exe,font_->sprite,texts_},projection_,options_.gradient_text,pressed);
    return out;
}
bool source_badge_fallback(int token) {
    const int club=token&0x0fff;
    return club==0x0fff || (club>89&&(club<138||club>167)&&club<357);
}
void draw_source_dialog(Image& out,const SourceDialogAssets& assets,const TaxDialogProjection& p,
    bool gradient,std::optional<GameEndTaxDialog::Button> pressed) {
    using Button=GameEndTaxDialog::Button;
    if(out.width!=320||out.height!=240||(!p.badge_fallback&&!assets.badge))
        throw FormatError("Source dialog: invalid backdrop or badge assets");
    if(p.compact_dialog_frame)out=Image(320,240,0);
    frame(out,assets.widgets,assets.exe,48,84,217,99,!p.compact_dialog_frame);
    const std::string_view header = p.source_system_identity
        ? text_at(assets.texts,::bmh::layout::SystemManagerNameText) : p.manager_name;
    const int nw=assets.heading.text_width(header);assets.heading.draw(out,105+105/2-nw/2,125-assets.heading.baseline,header,0x5f);
    const bool q=p.kind==TaxDialogKind::question;
    copy(out,assets.widgets,55,q?66:44,99,90,22,q?21:22);copy(out,assets.widgets,55,q?66:44,192,90,22,q?21:22);
    fill(out,53,131,207,1,105);fill(out,53,163,207,1,105);
    // Source passes param7=0x12 for both 42x42 wells: bevel tiles are copied,
    // with no saved-background shadow (param7&0x10 suppresses it).
    frame(out,assets.widgets,assets.exe,52,87,42,42,false);frame(out,assets.widgets,assets.exe,219,87,42,42,false);
    // SourceBadgeFallback/FormatError draw PaintsFourBorderEdges's inner inclusive bevel before art.
    for (int left : {53, 220}) {
        fill(out,left,88,1,40,0x46);fill(out,left,88,40,1,0x46);
        fill(out,left,127,40,1,0x40);fill(out,left+39,88,1,40,0x40);
    }
    if(p.badge_fallback)copy(out,assets.widgets,8,0,54,89,38,38);
    else copy(out,(*assets.badge),(p.badge_sprite&7)*38,0,54,89,38,38);
    const int portrait=std::max(0,p.portrait);
    if (portrait == 14) copy(out,assets.widgets,8,0,221,89,38,38);
    else copy(out,assets.faces,portrait%6*38,portrait/6*38,221,89,38,38);
    const auto lines=wrap_source_text(assets.small,assets.exe,p.body,170);
    int y=140;
    for(size_t i=0;i<lines.size();++i,y+=7){
        const auto& line=lines[i];const int width=assets.small.text_width(line);
        source_text(out,assets.small,assets.exe,73+170/2-width/2,y,line,i+1==lines.size()?0x5f:0x6c,gradient);
    }
    if(!q) {
        if((p.source_manager_flags&0xc0)!=0x40)
            button(out,assets.widgets,assets.sprite,assets.exe,95,166,125,
                   original_to_utf8(assets.texts[806]),pressed==Button::acknowledge);
    } else {
        const auto yes=original_to_utf8(text_at(assets.texts,source_button_text(::bmh::layout::GameEnd_Copy_TextPointer,
            uint8_t(p.source_flags&0xff))));
        const auto no=original_to_utf8(text_at(assets.texts,source_button_text(::bmh::layout::GameEnd_Copy_TextPointer2,
            uint8_t((p.source_flags>>8)&0xff))));
        button(out,assets.widgets,assets.sprite,assets.exe,62,166,75,yes,pressed==Button::yes);
        button(out,assets.widgets,assets.sprite,assets.exe,175,166,75,no,pressed==Button::no);
    }
}
GameEndTaxDialog::Button GameEndTaxDialog::hit_test(int x,int y)const{
    if(y<166||y>176)return Button::none;
    if(projection_.kind==TaxDialogKind::acknowledgement)
        return !source_timed_notice()&&x>=95&&x<=220?Button::acknowledge:Button::none;
    if(x>=62&&x<=137)return Button::yes;
    if(x>=175&&x<=250)return Button::no;
    return Button::none;
}
} // namespace bmh::ui
