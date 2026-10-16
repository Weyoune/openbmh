// SPDX-License-Identifier: GPL-3.0-only
#include "ui/game_end.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "formats/font.hpp"
#include "formats/vga.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
bool expect(bool ok,const char* name) {
    if(!ok)std::cerr<<"FAIL: "<<name<<'\n';
    return ok;
}
bool same_crop(const Image& rendered,int dx,int dy,const Image& source,int sx,int sy,int w,int h) {
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        if(rendered.pixels[size_t(dy+y)*rendered.width+dx+x]!=
           source.pixels[size_t(sy+y)*source.width+sx+x])return false;
    return true;
}
uint8_t pixel(const Image& image,int x,int y) {
    return image.pixels[size_t(y)*image.width+x];
}
}

int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"usage: ui_game_end_tests <Original data directory>\n";return 2;}
    if (!*argv[1]) { std::cout << "GameEnd native asset tests skipped: Original data not configured\n"; return 77; }
    const std::filesystem::path data=argv[1];
    bool ok=true;

    // Source wraps both ordinary spaces and the Original's NBSP byte as separators.
    const auto wrap_regular=wrap_source_text(data,"W W W W",19);
    const std::string nbsp_text=std::string("W")+char(0xa0)+"W W W";
    const auto wrap_nbsp=wrap_source_text(data,nbsp_text,19);
    const std::vector<std::string> wrap_expected={"W W","W W"};
    ok &= expect(wrap_regular==wrap_expected,"multiword Source wrap");
    ok &= expect(wrap_nbsp==wrap_expected,"NBSP uses Source word separator");
    ok &= expect(wrap_source_text(data,"Backofen",26)==std::vector<std::string>{"Bak-","kofen"},
                 "c|k Source spelling rewrite");
    ok &= expect(wrap_source_text(data,"Ackerbau",36)==std::vector<std::string>{"Acker-","bau"},
                 "dictionary split honors moving right-section limit");

    const auto paragraph_font=load_font(data/"F"/"BMH3.FNT");
    const auto paragraph_exe=load_le(data/"BMH.exe");
    ok &= expect(paragraph_font.glyph(0).advance==6,"Source pending-width includes the six-pixel NUL glyph");
    // W advances6, space2. Source stretches complete rows to width or width+1,
    // cycling the recorded gaps; it never stretches the final flush.
    ok &= expect(wrap_source_text(paragraph_font,paragraph_exe,"W W W W",19,true)==
        std::vector<std::string>{"W    W","W W"},"blocksatz allows Source one-pixel overshoot and leaves final row alone");
    ok &= expect(wrap_source_text(paragraph_font,paragraph_exe,"W W W W W",29,true)==
        std::vector<std::string>{"W   W   W","W W"},"blocksatz distributes spaces over actual word boundaries");
    ok &= expect(wrap_source_text(paragraph_font,paragraph_exe,"W W W W",19,false)==
        std::vector<std::string>{"W W","W W"},"centered mode retains ordinary word spaces");
    ok &= expect(wrap_source_text(paragraph_font,paragraph_exe,"Ackerbau",30)==
        std::vector<std::string>{"Ak-","kerbau"},"NUL width and moving suffix limit both participate in Source splitting");
    ok &= expect(wrap_source_text(paragraph_font,paragraph_exe,std::string("W W\0ignored",11),19,true)==
        std::vector<std::string>{"W W"},"Source paragraph ends at its NUL terminator");
    ok &= expect(wrap_source_text(paragraph_font,paragraph_exe,std::string(100,'W'),127,true)==
        std::vector<std::string>{std::string(100,'W')},"unbreakable oversized input remains intact for safe clipping rather than hanging DOS-style");

    ok &= expect(!source_badge_fallback(12)&&source_badge_fallback(90)&&!source_badge_fallback(138)&&
        source_badge_fallback(168)&&source_badge_fallback(356)&&!source_badge_fallback(357)&&
        source_badge_fallback(0x0fff),"Source badge fallback tests raw club ranges rather than sprite90..137");

    TaxDialogProjection ack_projection;
    ack_projection.manager_name="Nobody";
    ack_projection.body="Synthetic accounting notice: revenue 7200, contribution 3100.";
    ack_projection.badge_sprite=91;
    ack_projection.badge_fallback=false;
    GameEndTaxDialog ack(data,ack_projection,{false,false});
    const Image ack_image=ack.render();

    // legacy routine's inclusive rectangle: y=176 is included, y=177 is not.
    ok &= expect(ack.hit_test(95,166)==GameEndTaxDialog::Button::acknowledge,"ACK top-left inclusive");
    ok &= expect(ack.hit_test(220,176)==GameEndTaxDialog::Button::acknowledge,"ACK bottom-right inclusive");
    ok &= expect(ack.hit_test(221,176)==GameEndTaxDialog::Button::none,"ACK right edge outside");
    ok &= expect(ack.hit_test(220,177)==GameEndTaxDialog::Button::none,"ACK below Source hitbox");

    TaxDialogProjection question_projection=ack_projection;
    question_projection.kind=TaxDialogKind::question;
    GameEndTaxDialog question(data,question_projection,{false,false});
    ok &= expect(question.hit_test(62,166)==GameEndTaxDialog::Button::yes,"YES top-left inclusive");
    ok &= expect(question.hit_test(137,176)==GameEndTaxDialog::Button::yes,"YES bottom-right inclusive");
    ok &= expect(question.hit_test(175,166)==GameEndTaxDialog::Button::no,"NO top-left inclusive");
    ok &= expect(question.hit_test(250,176)==GameEndTaxDialog::Button::no,"NO bottom-right inclusive");
    ok &= expect(question.hit_test(250,177)==GameEndTaxDialog::Button::none,"NO below Source hitbox");

    // Exact Source bevel and shadow colors from legacy routine/legacy routine and
    // legacy routine. These are indexed pixels before palette conversion.
    ok &= expect(pixel(ack_image,53,88)==0x46&&pixel(ack_image,53,127)==0x40&&
                 pixel(ack_image,92,88)==0x40&&pixel(ack_image,53,88+39)==0x40,
                 "Source inner bevel light/dark edges");
    ok &= expect(pixel(ack_image,50,183)==0x40&&pixel(ack_image,265,86)==0x40,
                 "dialog Source shadow edges");
    ok &= expect(pixel(ack_image,97,176)==0x40&&pixel(ack_image,220,168)==0x40,
                 "ACK button Source shadow edges");

    // The selector is 91, so Source loads W/11.VGA and selects tile 3.
    const Image w11=load_vga(data/"W"/"11.VGA");
    ok &= expect(same_crop(ack_image,54,89,w11,3*38,0,38,38),
                 "badge selector 91 renders W11 tile when fallback is false");

    TaxDialogProjection fallback_projection=ack_projection;
    fallback_projection.badge_fallback=true;
    GameEndTaxDialog fallback(data,fallback_projection,{false,false});
    const Image fallback_image=fallback.render();
    const Image kb0=load_vga(data/"KB"/"0.VGA");
    ok &= expect(same_crop(fallback_image,54,89,kb0,8,0,38,38),
                 "runtime-club fallback uses KB/0 art independent of selector");

    Image restored(320,240,77);
    GameEndTaxDialog restored_dialog(data,ack_projection,{true,false},restored);
    const Image supplied=restored_dialog.backdrop();
    ok &= expect(supplied.width==320&&supplied.height==240&&
                 supplied.pixels.front()==77&&supplied.pixels.back()==77,
                 "non-compact_dialog_frame backdrop returns supplied restored framebuffer");
    ack_projection.compact_dialog_frame=true;
    GameEndTaxDialog black_dialog(data,ack_projection,{false,false},restored);
    const Image black=black_dialog.backdrop();
    ok &= expect(black.width==320&&black.height==240&&
                 std::all_of(black.pixels.begin(),black.pixels.end(),[](uint8_t p){return p==0;}),
                 "compact_dialog_frame black backdrop overrides supplied framebuffer");

    // Source centers in the 105px field as x + width/2 - text_width/2.
    // Check glyph ink at every expected pixel; this distinguishes integer
    // centering from centering the remaining width when the string is odd-width.
    const Font large=load_font(data/"F"/"BMH2.FNT");
    const int text_width=large.text_width(ack_projection.manager_name);
    const int text_x=105+105/2-text_width/2;
    Image glyphs(320,240,0);
    large.draw(glyphs,text_x,125-large.baseline,ack_projection.manager_name,0x5f);
    bool centered_ink=true;
    for(size_t i=0;i<glyphs.pixels.size();++i)
        if(glyphs.pixels[i]!=0&&ack_image.pixels[i]!=glyphs.pixels[i]){centered_ink=false;break;}
    ok &= expect(centered_ink,"manager label Source field centering and FNT2 pixels");

    // Source legacy routine(score,0x85): numeric grouping consumes the reserved
    // separator space above999; transparent percent glyphs retain fixed padding.
    const Font small=load_font(data/"F"/"BMH3.FNT");
    for (const auto& [score, label] : std::vector<std::pair<int,std::string>>{
        {763, " %%763 (0/0/0)"}, {1234, "%1.234 (0/0/0)"}, {12345, "12.345 (0/0/0)"}}) {
        HighscoreProjection projection;projection.ranked=true;projection.signature=135;
        projection.rows[0].score=score;
        GameEndHighscoreScreen scores(data,projection,{false,false});
        const auto rendered=scores.render();
        Image expected(320,240,0);
        small.draw(expected,232,122-small.baseline,label,0x40);
        small.draw(expected,230,120-small.baseline,label,0x5f);
        bool ink=true;
        for(size_t i=0;i<expected.pixels.size();++i)
            if(expected.pixels[i]&&rendered.pixels[i]!=expected.pixels[i]){ink=false;break;}
        ok &= expect(ink,"ranked Source grouped score/padding/shadow glyphs");
    }
    if(!ok)return 1;
    std::cout<<"GameEnd native-asset Source geometry and wrapping tests passed\n";
    return 0;
}
