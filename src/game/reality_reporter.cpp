// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "game/reality_reporter.hpp"

#include <algorithm>
#include <cstdlib>
#include "game/messages.hpp"

namespace bmh {
namespace {
struct Node {
    char token = 0;
    int selector = 0, variants = 0;
    std::string literal;
    std::vector<std::vector<Node>> branches;
};
// Parse before evaluation: structural traversal cannot draw random numbers.
// The Source cursor skips entire unselected nested %x...#x expressions.
std::vector<Node> parse(std::string_view text,size_t& at,int depth) {
    if(depth>32)throw FormatError("Reality reporter: template nesting too deep");
    std::vector<Node> nodes;
    while(at<text.size()&&text[at]!='#') {
        Node n;
        if(text[at]!='%') {
            while(at<text.size()&&text[at]!='%'&&text[at]!='#')n.literal+=text[at++];
        } else {
            if(++at>=text.size())throw FormatError("Reality reporter: truncated token");
            n.token=text[at++];
            if(n.token=='x') {
                if(at+2>text.size()||text[at]<'0'||text[at]>'9'||text[at+1]<'0'||text[at+1]>'9')
                    throw FormatError("Reality reporter: invalid selector digits");
                n.selector=text[at]-'0';n.variants=text[at+1]-'0';at+=2;
                for(;;) {
                    n.branches.push_back(parse(text,at,depth+1));
                    if(at>=text.size()||text[at++]!='#')throw FormatError("Reality reporter: missing branch terminator");
                    if(at<text.size()&&text[at]=='x'){++at;break;}
                }
            }
        }
        nodes.push_back(std::move(n));
    }
    return nodes;
}
std::string pool_name(const Bytes& pool,size_t offset) {
    if(offset>=pool.size())throw FormatError("Reality reporter: missing referee name");
    std::string out;for(;offset<pool.size()&&pool[offset];++offset)out+=char(pool[offset]);return out;
}
int player(const RealitySetup& scene,int one_based) {
    if(one_based<1||one_based>22)throw FormatError("Reality reporter: missing selected figure");
    return scene.players[size_t(one_based-1)].player;
}
std::string evaluate(const std::vector<Node>& nodes,const GameState& game,const RealitySetup& scene,
    const LeImage& exe,const RealityReporterFrame& frame,const RealityReporterState& state,WatcomRandom& rand) {
    std::string out;
    for(const auto& node:nodes) {
        if(!node.token)out+=node.literal;
        else if(node.token=='x') {
            const int index=node.selector+10*node.variants;
            if(node.selector && index>=int(state.selectors.size()))throw FormatError("Reality reporter: selector outside Source context");
            const int choice=node.selector?state.selectors[size_t(index)]:rand.range(1,node.variants);
            const size_t branch=size_t(choice<2?0:choice-1);
            // Source walks to #x without output when the selector exceeds the branches.
            if(branch<node.branches.size())out+=evaluate(node.branches[branch],game,scene,exe,frame,state,rand);
        } else if(node.token=='e')break;
        else if(node.token=='a')out+=game.club_name(scene.teams[0]);
        else if(node.token=='b')out+=game.club_name(scene.teams[1]);
        else if(node.token=='c') {
            if(state.current_player<0||state.current_player==0xfff)throw FormatError("Reality reporter: missing speaker");
            out+=game.player_name(state.current_player);
        } else if(node.token=='d') {
            if(frame.receiver_player<0||frame.receiver_player==0xfff)throw FormatError("Reality reporter: missing receiver");
            out+=game.player_name(frame.receiver_player);
        } else if(node.token=='f')out+=game.player_name(player(scene,frame.line_selected[size_t(1-frame.player_side)]));
        else if(node.token=='z')out+=game.player_name(player(scene,frame.opponent));
        else if(node.token=='s') {
            if(!frame.referee)out+=message_text(game,::bmh::layout::RealityReporter_Evaluate_TextPointer2);
            else out+=message_text(game,::bmh::layout::RealityReporter_Evaluate_TextPointer)+pool_name(game.referee_names,le16(game.master_database_metadata,size_t(frame.referee)*2));
        } else if(node.token=='0') {
            int metres=(int(frame.distance_before)*25-12+rand.range(0,26))/10;
            uint8_t value=uint8_t(metres);if(value>20)value=uint8_t((value/5)*5);
            out+=format_number(game,value,1);
        }
        // All other Source percent tokens are consumed without output.
        if(out.size()>=500)throw FormatError("Reality reporter: expanded text exceeds Source buffer");
    }
    return out;
}
} // namespace

RealityReporterState initialize_reality_reporter(const LeImage& exe) {
    RealityReporterState state;
    for(size_t i=0;i<state.selectors.size();++i)state.selectors[i]=exe.u8(::bmh::layout::RealityReporter_InitializeRealityReporter_RuntimeField+uint32_t(i));
    return state;
}
std::string reality_reporter_text(const GameState& game,const RealitySetup& scene,const LeImage& exe,
    const RealityReporterFrame& frame,const RealityReporterState& state,std::string_view text,WatcomRandom& rand) {
    if(frame.player_side>1)
        throw FormatError("Reality reporter: template needs its actual Source player side");
    size_t at=0;const auto nodes=parse(text,at,0);
    return evaluate(nodes,game,scene,exe,frame,state,rand);
}
RealityReporterResult report_reality(const GameState& game,const RealitySetup& scene,const LeImage& exe,
    RealityReporterFrame& frame,RealityReporterState& state,int figure,int side,uint8_t code,WatcomRandom& rand) {
    RealityReporterResult out;
    if(figure==100){state.last_side=state.last_owner=state.last_player=state.last_action=-1;return out;}
    if(figure<0||figure>=22||side<0||side>1||frame.player_side>1||code>21)
        throw FormatError("Reality reporter: invalid figure/side/cue");
    state.current_player=scene.players[size_t(figure)].player;
    state.selectors[12]=frame.card_kind;
    auto cue=[&](int value){out.codes.push_back(uint8_t(value));};
    if(code)cue(code);
    else {
        const int area=reality_penalty_area(scene,exe,1-side,scene.grid.ball().first,scene.grid.ball().second);
        const auto ball=scene.grid.ball(),old=scene.grid.previous_ball();
        // InitializeRealityReporter uses signed chars for both differences; preserve byte wrapping.
        const int dx=int8_t(std::abs(ball.first-old.first)),dy=int8_t(std::abs(ball.second-old.second));
        if(dy<=dx&&!frame.incoming_bh)throw FormatError("Reality reporter: missing Source incoming BH");
        const int high=dy>dx?dy:int(int8_t(*frame.incoming_bh));
        // BL stays dx even when dy is smaller (verified at 69210..6926d).
        const int low=dx;
        state.selectors[2]=uint8_t(2-frame.airborne);
        state.selectors[3]=uint8_t((high>2)+1+(high>9)*2);
        state.selectors[4]=uint8_t(3-2*(low>=10&&dy<=4)+(dy>16));
        state.selectors[5]=uint8_t(2-area);
        state.selectors[6]=uint8_t(side==state.last_side?2-(frame.distance_before<frame.distance_after):2);
        state.selectors[7]=uint8_t((frame.distance_after>6)+1);
        state.selectors[8]=uint8_t((ball.first>42)+1+(ball.second<11||ball.second>13));
        state.selectors[9]=frame.scene_status;
        state.selectors[11]=uint8_t(2-int(frame.received));
        if(low>7&&state.selectors[4]==3)state.selectors[4]=2;
        if(frame.action==2&&frame.opponent)frame.action=uint8_t(frame.lost_duel?9:10);
        if(frame.action==0&&frame.received&&frame.distance_before<6&&rand.range(0,1)!=0)frame.action=16;
        if(!(frame.action!=0&&frame.action==state.last_action&&state.current_player==state.last_player)) {
            if(state.last_owner!=-1&&state.current_player!=state.last_owner){cue(7);state.selectors[1]=2;state.last_player=state.current_player;}
            state.selectors[1]=uint8_t(state.current_player==state.last_player&&state.selectors[1]!=1?1:2);
            if(frame.action==0&&frame.distance_before>34)frame.action=8;
            cue(frame.action);state.last_owner=state.current_player;
            if(frame.action==0&&frame.intercepted)cue(13);
            if(frame.action==1) {
                state.last_owner=frame.receiver_player;
                if(frame.blocked_attempt) {
                    if(!frame.inaccurate&&frame.line_selected[size_t(1-frame.player_side)])cue(6);
                    else if(frame.inaccurate&&rand.range(0,2)==0)cue(4);
                }
                if(!frame.inaccurate&&!frame.line_selected[size_t(1-frame.player_side)]&&frame.offside&&int(frame.distance_after)<int(frame.distance_before)-5)cue(5);
                if(!frame.leading_run&&frame.distance_before<10)cue(15);
                if(frame.distance_before<12) {
                    if(frame.receiver<0||frame.receiver>=22)throw FormatError("Reality reporter: missing pass target figure");
                    const auto target=scene.grid.position(frame.receiver);
                    const int enemy=scene.grid.occupant(target.first,target.second,1-side);
                    if(enemy){frame.opponent=uint8_t(enemy);cue(14);}
                }
            }
            if(frame.scene_status)cue((frame.scene_status==3)+11);
        }
    }
    if(code&&state.last_action==9){out.suppressed=true;return out;}
    // %a/%b are fixed home/away afaf2/afaf4, independent of possession side.
    for(uint8_t id:out.codes)out.text.push_back(reality_reporter_text(game,scene,exe,frame,state,exe.string_at_pointer(::bmh::layout::RealityReporter_FormatError_TextPointer+uint32_t(id)*4),rand));
    state.last_action=frame.action;state.last_player=state.current_player;
    if(frame.action==9){state.last_owner=player(scene,frame.opponent);state.last_player=state.last_owner;}
    state.last_side=side;
    return out;
}
} // namespace bmh
