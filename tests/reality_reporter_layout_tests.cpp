#include "test_inputs.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include "ui/reality_reporter.hpp"
using namespace bmh;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
Image background(){Image image(320,240);for(int y=0;y<240;++y)for(int x=0;x<320;++x)image.at(x,y)=uint8_t((x+3*y)%47);return image;}
}
int main(int argc,char** argv){try{
    check(argc==2,"usage: reality_reporter_layout_tests <Original data>");const auto data=std::filesystem::path(argv[1]);
    ui::RealityReporterPanel panel(data);const auto font=load_font(data/"F/BMH3.FNT");
    check(font.height==6&&font.baseline==5&&font.glyph(0).advance==6,"actual Source reporter font and NUL width");
    auto image=background();const auto before=image;ui::RealityReporterLayoutState state;
    const auto quiet=panel.append(image,state,{});check(quiet.empty()&&image.pixels==before.pixels&&state.next_baseline==46,"silent Source action draws and advances nothing");
    auto lines=panel.append(image,state,{bmh::test::expected_text("reality_reporter_layout_tests-external-9").c_str()},false);
    check(lines.size()==1&&lines[0].text==bmh::test::expected_text("reality_reporter_layout_tests-external-9").c_str()&&lines[0].baseline==46&&state.next_baseline==53,"unjustified group flush uses exact Source baseline and seven-pixel advance");
    auto expected=before;font.draw(expected,66,41,lines[0].text,0x5f);
    check(expected.pixels==image.pixels,"plain Source glyphs modify exactly Original positions and preserve background");
    const auto row=panel.append(image,state,{bmh::test::expected_text("reality_reporter_layout_tests-external-3").c_str(),bmh::test::expected_text("reality_reporter_layout_tests-external-2").c_str()},false);
    check(row.size()>=1&&row.back().text.find(bmh::test::expected_text("reality_reporter_layout_tests-external-8").c_str())!=std::string::npos,"multiple Source cue strings share the pending line before one flush");
    // Six lines in the genuine post1 reference, followed by Source scrolling.
    state={};image=background();
    const std::vector<std::vector<std::string>> groups={
        {bmh::test::expected_text("reality_reporter_layout_tests-external-6").c_str(),bmh::test::expected_text("reality_reporter_layout_tests-external-2").c_str()},
        {bmh::test::expected_text("reality_reporter_layout_tests-external-7").c_str()},
        {bmh::test::expected_text("reality_reporter_layout_tests-external-3").c_str()},{bmh::test::expected_text("reality_reporter_layout_tests-external-5").c_str()},{bmh::test::expected_text("reality_reporter_layout_tests-external-1").c_str()}};
    int line_count=0,scrolls=0;
    for(const auto& group:groups){for(const auto& line:panel.append(image,state,group,false)){++line_count;scrolls+=line.scrolled;}}
    check(line_count==6&&!scrolls&&state.next_baseline==88,"Original post1 grouping yields six lines and exact captured next-baseline88");
    const auto pre_scroll=image;const auto scrolled=panel.append(image,state,{bmh::test::expected_text("reality_reporter_layout_tests-external-9").c_str()},false);
    check(scrolled.size()==1&&scrolled[0].scrolled&&scrolled[0].baseline==88&&state.next_baseline==88,"seventh Source row scrolls immediately and retains next-baseline88");
    auto draw_before_scroll=pre_scroll;font.draw(draw_before_scroll,66,83,bmh::test::expected_text("reality_reporter_layout_tests-external-9").c_str(),0x5f);
    for(int y=0;y<50;++y)for(int x=0;x<189;++x)
        check(image.at(66+x,40+y)==draw_before_scroll.at(66+x,47+y),"scroll copies exact189x50 rectangle seven pixels upward including its blank bottom");
    for(int y=0;y<240;++y)for(int x=0;x<320;++x)
        if(x<66||x>=256||y<40||y>96)check(image.at(x,y)==before.pixels[size_t(y)*320+size_t(x)],"reporter never changes caller header, pitch or controls");
    state={};image=background();const auto justified=panel.append(image,state,{bmh::test::expected_text("reality_reporter_layout_tests-external-6").c_str(),bmh::test::expected_text("reality_reporter_layout_tests-external-2").c_str()},false);
    check(justified.size()==2&&justified[0].justified&&!justified[1].justified,"overflow line justifies but final line remains left-aligned");
    check(font.text_width(justified[0].text)>=189&&font.text_width(justified[0].text)<=190,"Source block justification fills to width189 or allowed190");
    check(justified[1].text==bmh::test::expected_text("reality_reporter_layout_tests-external-4").c_str(),"observed Original first group wraps at exact word boundary");
    state={};image=background();const auto hyphen=panel.append(image,state,{bmh::test::expected_text("reality_reporter_layout_tests-external-10").c_str()},false);
    bool split=false;for(const auto& line:hyphen){split|=line.hyphenated;std::printf("HYPHEN baseline=%d text=%s\n",line.baseline,line.text.c_str());}
    check(split,"Source vowel/cluster rules split a real football word");
    state={};image=background();panel.append(image,state,{[] { auto text = bmh::test::expected_text("reality_reporter_layout_tests-external-3"); text.at(text.find(' ')) = char(0xa0); return text; }()},false);
    auto spaces=background();ui::RealityReporterLayoutState normal;panel.append(spaces,normal,{bmh::test::expected_text("reality_reporter_layout_tests-external-3").c_str()},false);
    check(image.pixels==spaces.pixels,"Source byteA0 is a word separator, not a visible glyph");
    const auto gradient_before=background();auto gradient_image=gradient_before;state={};panel.append(gradient_image,state,{bmh::test::expected_text("reality_reporter_layout_tests-external-3").c_str()},true);
    const auto exe=load_le(data/"BMH.exe");int ramp=0;while(ramp<32&&exe.u8(::bmh::layout::Advertising_Text_RuntimeField2+unsigned(ramp))!=0x5f)++ramp;
    Image mask(320,240);font.draw(mask,66,41,bmh::test::expected_text("reality_reporter_layout_tests-external-3").c_str(),1);
    for(int y=0;y<240;++y)for(int x=0;x<320;++x)
        check(gradient_image.at(x,y)==(mask.at(x,y)?exe.u8(::bmh::layout::Advertising_Text_RuntimeField+unsigned(ramp*6+y-41)):gradient_before.pixels[size_t(y)*320+size_t(x)]),"Original six-row gradient shades glyphs only");
    state={};image=background();const auto empty=panel.append(image,state,{""});check(empty.size()==1&&state.next_baseline==53&&image.pixels==before.pixels,"non-silent empty Source cue still advances one flush row");
    bool refused=false;state={};try{panel.append(image,state,{std::string(51,'W')});}catch(const FormatError&){refused=true;}
    check(refused,"unsupported unsplittable Source overflow is explicit, never an invented split or infinite loop");
    std::puts("PASS: Original reporter grouping/baselines, block alignment, NUL-aware hyphenation, A0 spacing, gradients, exact overlap scroll and owned surfaces; no Main Reality claim");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
