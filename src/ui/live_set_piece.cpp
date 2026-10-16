// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/live_set_piece.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <numeric>

namespace bmh::ui {
namespace {

struct Figure {
    int16_t x = 0, y = 0;
    int8_t vx = 0, vy = 0;
    uint8_t motion = 255, timer = 0, animation = 0;
};

// RealityDirection: eight directions, with the Source's integer ratio boundary.
int direction(int x, int y) {
    int ax = std::abs(int(int16_t(x))), ay = std::abs(int(int16_t(y)));
    if (!(ay > 1 && ax > 1 && ax != ay)) {
        if (ay < ax) ay *= 2;
        else ax *= 2;
    }
    int d = y == 0 ? 3 : std::abs(ax / ay);
    if (d > 2) d = 3;
    if (d > 1) --d;
    if (y > 0 && d == 0) d = 4;
    if (x < 0 && d > 0 && d != 4) d += 4;
    if (y > 0 && (d == 1 || d == 5)) d += 2;
    return d;
}

struct Scene {
    const LiveSetPieceEvent& event;
    const LeImage& exe;
    std::array<Figure,25> figures{};
    std::array<bool,8> placed{};
    std::array<int,25> order{};
    size_t cursor = 0;
    int camera_x = 0, camera_y = 0;

    int byte(uint32_t address) const { return exe.u8(address); }
    int sign(uint32_t address) const { return int(int8_t(exe.u8(address))); }
    int word(uint32_t address) const { return int(int16_t(exe.u16(address))); }
    int draw(int low, int high) {
        if (cursor >= event.draws.size()) throw FormatError("live set piece: missing Source draw");
        const auto& d = event.draws[cursor++];
        if (d.low != low || d.high != high || d.value < low || d.value > high)
            throw FormatError("live set piece: Source draw mismatch at " + std::to_string(cursor) +
                " (expected " + std::to_string(low) + "," + std::to_string(high) + "; recorded " +
                std::to_string(d.low) + "," + std::to_string(d.high) + ")");
        return d.value;
    }
    int slope(int y) const { return (int16_t(y) - 23) * 61 / 150 * (event.source_side ? 1 : -1); }
    void put(int id, int x, int y) {
        figures.at(size_t(id)).x = int16_t(x - (event.source_side ? 677 : 0) - 16);
        figures.at(size_t(id)).y = int16_t(y - 77);
    }
    void motion(int id, int command, int timer, int animation) {
        auto& f = figures.at(size_t(id));
        int code = command;
        if (command & 0xc0) {
            if ((command & 0xc0) == 0xc0) command &= 63;
            const auto& target = figures.at(size_t((command & 63) - 20));
            const int dx = int16_t(target.x - f.x), dy = int16_t(target.y - f.y);
            code = direction(dx, dy);
            if (command & 0x80) code += 10;
            else if (command & 0x40) {
                f.vx = int8_t(std::clamp(dx,-1,1)); f.vy = int8_t(std::clamp(dy,-1,1));
            }
        }
        if (code < 8) code = byte(::bmh::layout::LiveSetPiece_Motion_ExecutableField2Unsigned + uint32_t(code));
        if (code > 9 && code < 18) code = byte(::bmh::layout::LiveSetPiece_Motion_ExecutableFieldUnsigned + uint32_t(code)) + 10;
        f.motion = uint8_t(code); f.timer = uint8_t(timer); f.animation = uint8_t(animation);
    }
    void camera(int x, int y) {
        // Clamp: the viewport follows the ball, with a dead zone and one-pixel easing.
        const int xx = x + 16, yy = y - 21;
        if (camera_y < yy - 30 || yy < camera_y + 30) camera_y = y - 51;
        if (camera_x < xx - 150) camera_x = x - 134;
        if (xx < camera_x + 30) camera_x = x - 14;
        const int dx = camera_x - xx + 96, dy = camera_y - yy + 50;
        camera_x += (dx < 0) - (dx > 0); camera_y += (dy < 0) - (dy > 0);
        camera_x = std::clamp(camera_x,0,128); camera_y = std::clamp(camera_y,0,57);
    }
    void wall(int ball_x, int ball_y, int close) {
        const int keeper = word(::bmh::layout::LiveSetPiece_Run_ExecutableFieldUnsigned + uint32_t(event.source_side*2));
        const int dx = int16_t(keeper-ball_x), dy = int16_t(119-ball_y);
        int x = ball_x + sign(::bmh::layout::LiveSetPiece_Wall_ExecutableFieldUnsigned+uint32_t(event.source_side))*100;
        int y = dy*(x-ball_x)/dx + ball_y - 12 - close*4;
        int previous = slope(y);
        for (int id = 12; id < 16+close*2; ++id) {
            put(id,x,y); motion(id,direction(ball_x-x,ball_y-y)+10,255,248);
            y += 8;
            const int next = slope(y);
            x += next + dy*-4/dx - previous;
            previous = next;
        }
    }

    std::vector<LiveSetPieceFrame> run() {
        const int side = event.source_side, kind = event.kind;
        const int dir = sign(::bmh::layout::LiveSetPiece_Run_ExecutableField6Unsigned+uint32_t(side));
        const int spot = word(::bmh::layout::LiveSetPiece_Run_ExecutableField5Unsigned+uint32_t(side*2));
        const int keeper = word(::bmh::layout::LiveSetPiece_Run_ExecutableFieldUnsigned+uint32_t(side*2));
        int param = event.distance ? uint8_t(event.distance-15) : 0;
        int bx = 0, by = 0, receiver_x = 0, receiver_y = 0, flight = 255;
        bool corner_flight = false;
        if (kind < 3) {
            if (kind == 2) { bx = spot; by = 119; }
            else {
                by = draw(76,176); param = std::min(param,35);
                const int base = slope(by) + (side ? 765 : 239);
                bx = param ? base + (side ? -5 : 5)*param : side ? draw(686,base) : draw(base,310);
                const int close = (side ? base-bx : bx-base) < 20;
                wall(bx,by,close);
            }
            figures[1].vx = int8_t(dir*2);
            const int runup = draw(10,40);
            const int start = int16_t(bx-dir*runup);
            figures[1].vy = int8_t(draw(0,2)-1);
            const int timer = int16_t(runup)>>1;
            receiver_y = int16_t(by-figures[1].vy*(timer+1));
            const int facing = direction(bx-start,by-receiver_y);
            receiver_x = int16_t(start+sign(::bmh::layout::LiveSetPiece_Run_ExecutableField13Unsigned+uint32_t(facing)));
            put(1,receiver_x,receiver_y); motion(1,facing,timer,0);
            if (side) camera_x = 720;
        } else {
            by = draw(0,1) ? 230 : -20;
            bx = spot+slope(by)+dir*25;
            const int scatter = draw(-20,20);
            receiver_y = draw(109,139); flight = draw(8,12);
            const int vx = (int16_t(spot+scatter)-int16_t(bx))/flight;
            figures[22].vx = int8_t(vx); receiver_x = int16_t(bx+flight*vx);
            const int vy = (int16_t(receiver_y)-int16_t(by))/flight;
            figures[22].vy = figures[23].vy = int8_t(vy);
            if (draw(0,5)) figures[22].vy = int8_t(figures[22].vy-7);
            receiver_y = int16_t(by+vy*flight);
        }
        put(22,bx,by); put(23,bx,by+1);
        motion(22,9,flight,kind==3?0:255); motion(23,8,255,255);
        put(11,keeper,119); motion(11,170,3,248);
        if (kind == 3) {
            int vx = draw(1,4); if (side) vx = -vx;
            const int vy = draw(1,6)-3;
            const int start = int16_t(receiver_x-dir*9);
            put(1,vx*(flight+1)+start,vy*(flight+1)+receiver_y-1);
            put(23,start,receiver_y+1); motion(1,107,flight,0);
            corner_flight = true; put(23,bx,by+1);
            figures[1].vx = int8_t(figures[1].vx*std::abs(vx));
            figures[1].vy = int8_t(figures[1].vy*std::abs(vy));
            const int y = by==230 ? 138 : 100;
            put(16,keeper+slope(y)-dir*38,y); motion(16,171,255,0);
        }
        if (kind == 1 || kind == 3) {
            for (int i = 0; i < (kind==3?8:4); ++i) {
                if (kind != 3 && draw(0,1)==0) continue;
                const int x = int16_t(spot+draw(sign(::bmh::layout::LiveSetPiece_Run_ExecutableField11Unsigned+uint32_t(i))-10,sign(::bmh::layout::LiveSetPiece_Run_ExecutableField11Unsigned+uint32_t(i))+10));
                const int y = int16_t(draw(byte(::bmh::layout::LiveSetPiece_Run_ExecutableField4Unsigned+uint32_t(i))-10,byte(::bmh::layout::LiveSetPiece_Run_ExecutableField4Unsigned+uint32_t(i))+10));
                if (std::abs(x-bx)<100 && std::abs(y-by)<60) continue;
                const int id = byte(::bmh::layout::LiveSetPiece_Run_ExecutableField10Unsigned+uint32_t(i));
                put(id,x,y); motion(id,i<4?171:107,255,0); placed[size_t(i)] = true;
            }
            if (kind == 1) for (int j = 0; j < 2; ++j) {
                int x,low,high;
                if (j==0 && draw(0,1)) { x=bx;low=3;high=6; }
                else { x=int16_t(bx+draw(sign(::bmh::layout::LiveSetPiece_Run_ExecutableField9Unsigned+uint32_t(j))-10,sign(::bmh::layout::LiveSetPiece_Run_ExecutableField9Unsigned+uint32_t(j))+10));low=5;high=20; }
                x=int16_t(x+dir*draw(low,high));
                int y=int16_t(by-draw(4,10));
                if (draw(0,1)==0) y=int16_t(by+15+draw(0,10));
                put(j+2,x,y);
                int code=170;
                if (draw(0,1)==0 && x-keeper>bx-keeper) code=draw(0,1)?95:106;
                motion(j+2,code,255,0);
            }
            camera(figures[22].x,figures[22].y);
        }
        std::iota(order.begin(),order.end(),0);
        int phase = 0, flags = 0, animation_clock = 0, header = 0, records = 0;
        bool finished = false;
        std::vector<LiveSetPieceFrame> result;
        while (!finished) {
            if (++records > 0x576) throw FormatError("live set piece: Source entry limit exceeded");
            for (int id = 0; id < 25; ++id) {
                auto& f = figures[size_t(id)];
                if (f.motion == 255) continue;
                int y = f.y;
                if (id==22 && ((flags&2) || corner_flight)) {
                    if (f.vy<figures[23].vy || figures[23].y+1<figures[22].y) ++f.vy;
                    else f.vy=figures[23].vy;
                }
                int x=figures[22].x;
                if (id != 23) {
                    x=int16_t(f.x+f.vx);
                    if (f.animation<254) {
                        int wait=f.animation>>3;
                        if (std::abs(int(f.vx))<4 && std::abs(int(f.vy))<4 && wait<2) wait=2;
                        if (wait==0 || int8_t(animation_clock)%wait==0) {
                            int frame=(f.animation&7)+1;
                            if (id==22 && f.vx<1) {
                                frame=(f.animation&7)-1;
                                if (frame<0) frame=byte(::bmh::layout::LiveSetPiece_Run_ExecutableField3Unsigned+figures[22].motion);
                            } else if (byte(::bmh::layout::LiveSetPiece_Run_ExecutableField3Unsigned+f.motion)<frame) frame=0;
                            f.animation=uint8_t(wait*8+frame);
                        }
                    }
                }
                y=int16_t(y+f.vy);
                int vx=figures[1].vx;
                if (f.timer==0) {
                    figures[1].vx=int8_t(figures[1].vx>>1);
                    if (kind < 3) {
                        if (phase==0) { figures[1].vx=int8_t(vx);motion(11,235,255,250);vx=figures[1].vx; }
                        else if (phase==1) {
                            motion(1,24,0,0);
                            const int speed=draw(7,12);
                            figures[22].vx=int8_t(dir*speed); figures[22].vy=int8_t(kind==2?-3:-6);
                            motion(22,9,255,0); flags=draw(0,1); if (speed<9) flags=1;
                            figures[23].vy=int8_t((119-int16_t(by))/12);
                            figures[22].vy=int8_t(figures[22].vy+figures[23].vy);
                            motion(11,direction(figures[22].x-figures[11].x,by-119),255,255);
                            vx=figures[1].vx;
                            if (kind==1) {
                                if (draw(0,1)) { motion(12,107,255,0);figures[12].vx=int8_t(figures[12].vx*2); }
                                motion(2,95,255,0);
                                const int code=figures[3].vx==0 && draw(0,1)==0 ? 159 : 95;
                                motion(3,code,255,0); figures[2].vx=int8_t(figures[2].vx*2);
                                vx=figures[1].vx;
                            }
                        } else if (kind==2 || phase==3) finished=true;
                        else { figures[1].vx=int8_t(vx);motion(1,95,6,0);vx=figures[1].vx*3; }
                    } else {
                        if (phase==1) {
                            header=int8_t(figures[22].y-figures[23].y);
                            if (16-draw(0,2)<header) header=2;
                            else if (header<13) header=0;
                            else { header=1;figures[1].x=int16_t(figures[1].x+dir*10); }
                            animation_clock=0;motion(1,byte(::bmh::layout::LiveSetPiece_Run_ExecutableField12Unsigned+uint32_t(header)),3,8);
                            figures[1].vy=0;figures[22].vy=int8_t(figures[23].vy-2);vx=figures[1].vx;
                        } else if (phase==2) {
                            corner_flight=false;y=int16_t(y-figures[22].vy);figures[1].vx=int8_t(vx);
                            figures[22].vx=int8_t(dir*draw(8,10));figures[22].vy=0;
                            (void)draw(0,1);
                            // local_3c is 25 after each frame's sprite loop, hence nonzero.
                            figures[22].vy=int8_t(2-draw(3,5));motion(22,9,255,0);
                            figures[23].vy=int8_t((figures[11].y-figures[23].y)/12);
                            figures[22].vy=int8_t(figures[22].vy+figures[23].vy);
                            y=int16_t(y+figures[22].vy);vx=figures[1].vx;
                        } else if (phase==3) {
                            figures[1].vx=int8_t(vx);motion(1,header==1?23:95,1,header==1?254:0);
                            figures[1].vy=0;vx=0;
                        } else if (phase==4) finished=true;
                    }
                    figures[1].vx=int8_t(vx);++phase;
                    if (kind==3 || (kind==1 && phase==3)) for (int i=0;i<8;++i) if (placed[size_t(i)])
                        motion(byte(::bmh::layout::LiveSetPiece_Run_ExecutableField10Unsigned+uint32_t(i)),kind==3?(draw(0,1)?171:107):95,255,0);
                    if (kind==3) { motion(16,171,255,0);motion(11,171,255,0); }
                } else --f.timer;
                if (id==23 && y<=figures[22].y) figures[22].y=int16_t(y-1);
                f.x=int16_t(x);f.y=int16_t(y);
            }
            // Source insertion sort is stable and sorts figures 0..23; 24 remains last.
            std::stable_sort(order.begin(),order.begin()+24,[&](int a,int b){return figures[size_t(a)].y<figures[size_t(b)].y;});
            LiveSetPieceFrame frame;frame.phase=phase;
            for (const int id:order) {
                const auto& f=figures[size_t(id)];if (f.motion==255) continue;
                int animation=f.animation&7;if(animation==7) animation=0;if(animation==6) animation=1;
                const int bank=id>=sign(::bmh::layout::LiveSetPiece_Run_ExecutableField7Unsigned+uint32_t(side)) && id<=sign(::bmh::layout::LiveSetPiece_Run_ExecutableField8Unsigned+uint32_t(side)) ? 128 : 0;
                frame.sprites.push_back({f.x,f.y,id,uint8_t(bank|(byte(::bmh::layout::LiveSetPiece_Run_ExecutableField2Unsigned+f.motion)+animation))});
                if (++records>0x576) throw FormatError("live set piece: Source entry limit exceeded");
            }
            camera(figures[23].x,figures[23].y);frame.camera_x=camera_x;frame.camera_y=camera_y;
            result.push_back(std::move(frame));++flags;animation_clock=uint8_t(animation_clock+1);
        }
        if (cursor!=event.draws.size()) throw FormatError("live set piece: unused Source draws");
        return result;
    }
};

} // namespace

std::vector<LiveSetPieceFrame> live_set_piece_frames(const LiveSetPieceEvent& event,const LeImage& exe) {
    if (event.source_side<0 || event.source_side>1 || event.kind<1 || event.kind>3)
        throw FormatError("live set piece: invalid Source scene identity");
    return Scene{event,exe}.run();
}

} // namespace bmh::ui
