// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/reality_reporter.hpp"

#include <algorithm>
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr int kX=66,kOrigin=46,kWidth=189,kStep=7;
char folded(char c){return c>='A'&&c<='Z'?char(c+32):c;} // StartsFolded, ASCII only
bool starts_folded(std::string_view word,std::string_view pattern) {
    if(word.size()<pattern.size())return false;
    for(size_t i=0;i<pattern.size();++i)if(folded(word[i])!=folded(pattern[i]))return false;
    return true;
}
}
RealityReporterPanel::RealityReporterPanel(const std::filesystem::path& data)
    : font_(load_font(asset_path(data/"F","BMH3.FNT"))),exe_(load_le(asset_path(data,"BMH.exe"))),
      vowels_(exe_.cstring(::bmh::layout::GameEnd_SourceHyphenator_ExecutableField)) {
    if(font_.height!=6||font_.baseline!=5||exe_.cstring(::bmh::layout::RealityReporter_Font_ExecutableField)!=" "||exe_.cstring(::bmh::layout::Fltd_Layout_ExecutableField6)!="-")
        throw FormatError("Reality reporter: unsupported Original font/separators");
    for(uint32_t i=0;i<30;++i)clusters_.push_back(exe_.string_at_pointer(::bmh::layout::GameEnd_SourceHyphenator_RuntimeField+i*4));
}
bool RealityReporterPanel::split_allowed(std::string_view word,size_t split,size_t end) const {
    const auto vowel=[&](char c){return vowels_.find(c)!=std::string::npos;};
    // RefusesSingleCharacterSideEvenIfIt refuses a single-character side, even if it is a vowel.
    const auto has_vowel=[&](std::string_view part){return part.size()>1&&std::any_of(part.begin(),part.end(),vowel);};
    if(!has_vowel(word.substr(0,split+1))||!has_vowel(word.substr(split+1,end-split)))return false;
    if(split&&vowel(word[split+1]))return false;
    // Int8's forward scan has no side effects and its count is overwritten
    // by each cluster length. The backward cluster/vowel walk decides the split.
    for(int i=int(split);i>=0;--i) {
        if(vowel(word[size_t(i)]))return true;
        for(const auto& cluster:clusters_)
            if(int(split)+1<int(cluster.size())+i&&starts_folded(word.substr(size_t(i)),cluster))return false;
    }
    return false;
}
int RealityReporterPanel::hyphenate(std::string& word,int pending_width) const {
    // Draw2 tightens the right limit by four, and uses strict '<'. Its
    // caller's pending_width counts the NUL glyph as well (Int).
    int end=int(word.size())-1;
    for(int split=end;split>=0;--split) {
        if(!split_allowed(word,size_t(split),size_t(end)))continue;
        if(pending_width+font_.text_width(std::string_view(word).substr(0,size_t(split+1)))<kWidth-4) {
            if(word[size_t(split)]=='c'&&word[size_t(split+1)]=='k')word[size_t(split)]='k';
            return split;
        }
        // Draw2 shrinks the considered right part after a legal but too
        // wide split; later candidates do not test vowels in the whole suffix.
        end=split;
    }
    return -1;
}
void RealityReporterPanel::draw(Image& image,std::string_view text,int baseline,bool gradient) const {
    const int width=font_.text_width(text),top=baseline-font_.baseline;
    if(kX+width>image.width||top<0||baseline>image.height)return; // DrawnFirstBy bounds
    if(!gradient){font_.draw(image,kX,top,text,0x5f);return;}
    int ramp=0;while(ramp<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2Unsigned+uint32_t(ramp))!=0x5f)++ramp;
    if(ramp==32)throw FormatError("Reality reporter: missing Source color ramp");
    Image mask(std::max(1,width),font_.height);font_.draw(mask,0,0,text,1);
    for(int y=0;y<font_.height;++y)for(int x=0;x<width;++x)
        if(mask.at(x,y))image.at(kX+x,top+y)=exe_.u8(::bmh::layout::Advertising_Text_RuntimeFieldUnsigned+uint32_t(ramp*6+y));
}
void RealityReporterPanel::next_line(Image& image,RealityReporterLayoutState& state,RealityReporterLine& line) const {
    state.next_baseline+=kStep;
    if(state.next_baseline>93) {
        // FormatError2: same surface, src(x66,y47), dst(x66,y40), 189x50.
        // Upward traversal preserves overlap and also copies the blank bottom.
        for(int y=0;y<96-kOrigin;++y)for(int x=0;x<kWidth;++x)
            image.at(kX+x,kOrigin-6+y)=image.at(kX+x,kOrigin+1+y);
        state.next_baseline-=kStep;line.scrolled=true;
    }
}
std::vector<RealityReporterLine> RealityReporterPanel::append(Image& image,RealityReporterLayoutState& state,
    const std::vector<std::string>& messages,bool gradient) const {
    if(image.width!=320||image.height!=240||state.next_baseline<kOrigin||state.next_baseline>88||
       (state.next_baseline-kOrigin)%kStep||state.pending.size()>=127||state.word_ends.size()>127)
        throw FormatError("Reality reporter: invalid owned Source layout state");
    std::vector<RealityReporterLine> painted;
    if(messages.empty())return painted; // InitializeRealityReporter does not flush silent actions
    for(const auto& message:messages) {
        size_t at=0;
        while(at<message.size()&&message[at]) {
            std::string word;
            while(at<message.size()&&message[at]&&message[at]!=' '&&uint8_t(message[at])!=0xa0)word+=message[at++];
            if(word.size()>=52)throw FormatError("Reality reporter: word exceeds Source52-byte buffer");
            std::string candidate=state.pending+(state.pending.empty()?"":" ")+word;
            while(font_.text_width(candidate)>kWidth) {
                const int split=hyphenate(word,font_.text_width(state.pending)+font_.glyph(0).advance);
                const bool hyphen=split>=0;
                if(hyphen) {
                    if(!state.pending.empty()){state.pending+=' ';state.word_ends.push_back(int(state.pending.size()-1));}
                    state.pending+=word.substr(0,size_t(split+1));state.pending+='-';
                    word.erase(0,size_t(split+1));
                } else if(state.pending.empty()) {
                    // The DOS routine loops indefinitely on an unsplittable oversized
                    // word; surface the unsupported input, do not fabricate a split.
                    throw FormatError("Reality reporter: unsplittable oversized Source word");
                }
                if(!state.word_ends.empty())state.word_ends.pop_back();
                size_t gap=0;
                while(!state.word_ends.empty()&&font_.text_width(state.pending)<kWidth) {
                    const int position=state.word_ends[gap];
                    if(position<0||position>int(state.pending.size()))throw FormatError("Reality reporter: invalid Source word boundary");
                    state.pending.insert(size_t(position),1,' ');
                    for(size_t i=gap+1;i<state.word_ends.size();++i)++state.word_ends[i];
                    if(++gap>=state.word_ends.size())gap=0;
                }
                RealityReporterLine line{state.next_baseline,state.pending,true,hyphen,false};
                if(font_.text_width(state.pending)<=kWidth+1)draw(image,state.pending,state.next_baseline,gradient);
                next_line(image,state,line);painted.push_back(std::move(line));
                state.pending.clear();state.word_ends.clear();candidate=word;
            }
            if(!state.pending.empty())state.pending+=' ';
            state.pending+=word;state.word_ends.push_back(int(state.pending.size()));
            if(at<message.size()&&(message[at]==' '||uint8_t(message[at])==0xa0))++at;
        }
    }
    // Mode2 flush: no justification; always advance even for empty output.
    RealityReporterLine last{state.next_baseline,state.pending,false,false,false};
    draw(image,state.pending,state.next_baseline,gradient);next_line(image,state,last);painted.push_back(std::move(last));
    state.pending.clear();state.word_ends.clear();return painted;
}
} // namespace bmh::ui
