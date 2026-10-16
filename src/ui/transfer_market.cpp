// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/transfer_market.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>

#include "game/state.hpp"
#include "game/messages.hpp"
#include "ui/setup.hpp"

namespace bmh::ui {
namespace {
constexpr size_t kPoolFirst = 0xb70;
constexpr int kMarketEntries = 40;
constexpr uint32_t kTransferTable = ::bmh::layout::EditorList;
constexpr uint32_t kTransferEntrySize = 0x58;
constexpr size_t kManagerRecordSize = 0xb0c;

void rect(Image& out,int x,int y,int w,int h,uint8_t color) {
    for(int yy=std::max(0,y);yy<std::min(out.height,y+h);++yy)
        for(int xx=std::max(0,x);xx<std::min(out.width,x+w);++xx)out.at(xx,yy)=color;
}
void copy(Image& out,const Image& source,int sx,int sy,int x,int y,int w,int h,int key=-1) {
    if(sx<0||sy<0||w<0||h<0||sx+w>source.width||sy+h>source.height)
        throw FormatError("transfer market: Original asset rectangle outside source");
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx) {
        const auto pixel=source.pixels.at(size_t(sy+yy)*size_t(source.width)+size_t(sx+xx));
        if(pixel!=key&&x+xx>=0&&x+xx<out.width&&y+yy>=0&&y+yy<out.height)out.at(x+xx,y+yy)=pixel;
    }
}
std::string padded(int n,int width=2) {
    auto text=std::to_string(n);if(int(text.size())<width)text.insert(0,size_t(width-int(text.size())),'%');return text;
}
std::string money(int64_t value) {
    const bool negative = value < 0;
    uint64_t n = uint64_t(negative ? -value : value);
    std::string digits = std::to_string(n);
    for (int at = int(digits.size()) - 3; at > 0; at -= 3) digits.insert(size_t(at), ".");
    if (negative) digits.insert(digits.begin(), '-');
    return digits;
}
const char* position_name(int position) {
    switch (position) {
    case 0: return "TW";
    case 1: return "ABW";
    case 2: return "MIT";
    case 3: return "STU";
    default: return "?";
    }
}
int32_t rd32(const std::vector<uint8_t>& v, size_t at) {
    return int32_t(uint32_t(v.at(at)) | uint32_t(v.at(at + 1)) << 8 |
                   uint32_t(v.at(at + 2)) << 16 | uint32_t(v.at(at + 3)) << 24);
}
std::string field_name(int index) {
    if (index == 0) return "Ablöse";
    if (index == 1) return "Monatsgehalt";
    if (index == 2) return "Laufzeit (Jahre)";
    return "Prämie " + std::to_string(index - 2);
}
uint32_t parse_input(std::string_view input) {
    uint32_t value = 0;
    if (input.empty()) return 0;
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value);
    if (result.ec != std::errc{} || result.ptr != input.data() + input.size()) return 0;
    return value;
}
} // namespace

TransferMarketState transfer_market_from_game(const GameState& g, uint64_t revision) {
    const int manager = g.memory.u8(::bmh::layout::ActiveManager);
    if (manager < 0 || size_t(manager) >= g.manager_teams.size())
        throw FormatError("transfer market: invalid active manager");
    const size_t record = size_t(manager) * kManagerRecordSize;
    if (record + 0x320 > g.manager_records.size() || g.squad_slots.size() < kPoolFirst + kMarketEntries)
        throw FormatError("transfer market: truncated manager or pool table");

    TransferMarketState state;
    state.manager_slot = manager;
    state.runtime_club = g.manager_teams.at(size_t(manager));
    state.revision = revision;
    state.balance = rd32(g.manager_records, record + 0x314);
    state.credit_line = rd32(g.manager_records, record + 0x31c);
    const auto manager_end = std::find(g.manager_records.begin() + record + 0xa8,
                                       g.manager_records.begin() + record + 0xa8 + 26, uint8_t(0));
    state.manager_name.assign(g.manager_records.begin() + record + 0xa8, manager_end);
    state.club_name = g.club_name(state.runtime_club);
    state.source = "engine_projection";

    std::vector<bool> listed_players(g.players.size(), false);
    for (int i = 0; i < kMarketEntries; ++i) {
        const int player_id = g.squad_slots.at(kPoolFirst + size_t(i)) & 0x0fff;
        if (player_id == 0x0fff) continue;
        if (size_t(player_id) >= g.players.size()) throw FormatError("transfer market: pool player is outside attributes");
        const uint32_t entry = kTransferTable + uint32_t(i) * kTransferEntrySize;
        TransferMarketItem item;
        item.list_entry = i;
        item.source_slot = int(kPoolFirst + size_t(i));
        item.player = player_id;
        const Player& player = g.players.at(size_t(player_id));
        item.name = g.player_name(player_id);
        item.age = player.age(); item.position = player.position(); item.strength = player.strength();
        item.attributes=player;item.source_word=g.squad_slots.at(kPoolFirst+size_t(i));
        item.nationality=uint8_t(player.nationality()+(size_t(player_id)<g.player_bonus.size()?((g.player_bonus[size_t(player_id)]&128)>>2):0));
        const uint16_t seller_raw = g.memory.u16(entry + 8);
        if (seller_raw == 0xffff) {
            item.seller = -1;
            item.seller_name = g.club_name(0x0fff);
        } else {
            item.seller = seller_raw & 0x7fff;
            item.seller_name = g.club_name(item.seller);
            item.own_listing = item.seller == state.runtime_club;
        }
        const uint64_t fee = uint64_t(g.memory.u32(entry + 0x2c) / 10000) * 0x30d4;
        if (fee > uint64_t(std::numeric_limits<int32_t>::max())) throw FormatError("transfer market: asking fee out of range");
        item.asking_fee = int32_t(fee);
        item.salary_reference = g.memory.u32(entry + 0x30);
        item.salary_demand_percent = g.memory.u8(entry + 0x55);
        for (int bid = 0; bid < 4; ++bid) {
            const uint16_t buyer = g.memory.u16(entry + uint32_t(bid) * 2);
            if (buyer == 0xffff) continue;
            ++item.offers;
            if (buyer == uint16_t(state.runtime_club)) {
                item.own_bid_pending = true;
                TransferBidRequest own;
                own.manager=manager; own.list_entry=i; own.player=player_id;
                own.source_slot=item.source_slot;
                own.fee=int32_t(g.memory.u32(entry+0x1c+uint32_t(bid)*4));
                own.annual_salary=int32_t(g.memory.u32(entry+0x0c+uint32_t(bid)*4));
                own.contract_years=uint8_t((int(g.memory.u8(entry+0x34+uint32_t(bid))&0x7f)-1)/12+1);
                for(size_t bonus=0;bonus<own.bonuses.size();++bonus)
                    own.bonuses[bonus]=g.memory.u8(entry+0x38+uint32_t(bid)*7+uint32_t(bonus));
                item.own_bid=own;
            }
        }
        listed_players.at(size_t(player_id)) = true;
        state.items.push_back(std::move(item));
    }

    state.source_view_flags=g.memory.has(::bmh::layout::TransferMarket_ListedPlayers_RuntimeField+uint32_t(manager)*2)?g.memory.u16(::bmh::layout::Savestate_Layout_ExecutableField3+uint32_t(manager)*2):uint16_t(0x1f0);
    state.gradient_text=!g.memory.has(::bmh::layout::TextGradientDisabled)||g.memory.u8(::bmh::layout::TextGradientDisabled)==0;
    state.decorated_background=!g.memory.has(::bmh::layout::PlainBackground)||g.memory.u8(::bmh::layout::PlainBackground)==0;
    state.textured_panes=!g.memory.has(::bmh::layout::SolidPanelBackground)||g.memory.u8(::bmh::layout::SolidPanelBackground)==0;
    state.source_market_mode=g.memory.has(::bmh::layout::GameMode)?g.memory.u8(::bmh::layout::GameMode):uint8_t(0);
    if(g.memory.has(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField)&&g.memory.has(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField2))state.currency=std::pair<int32_t,int32_t>{int32_t(g.memory.u32(::bmh::layout::CurrencyMultiplier)),int32_t(g.memory.u32(::bmh::layout::CurrencyDivisor))};
    state.roster.runtime_team=state.runtime_club;state.roster.original_club_name=state.club_name;
    state.roster.has_squad=true;state.roster.gradient_text=state.gradient_text;
    state.roster.decorated_background=state.decorated_background;
    const SquadRange range = squad_range(state.runtime_club);
    if (range.first + range.count > g.squad_slots.size()) throw FormatError("transfer market: squad range outside slots");
    state.roster.squad_start_offset=int(range.first);
    // Int3 reads24 physical SquadSlots words, first11 for Aktive or all
    // when Team is selected; this does not use the displayed borrowed rows.
    for(int all=0;all<2;++all) {
        std::array<uint16_t,5> count{},sum{};
        for(size_t row=0;row<size_t(all?24:11)&&range.first+row<g.squad_slots.size();++row) {
            const int id=int(g.squad_slots[range.first+row]&0x0fff);if(id==0x0fff)continue;
            if(size_t(id)>=g.players.size())throw FormatError("transfer market: team-average player outside Native attributes");
            const auto& player=g.players[size_t(id)];const size_t pos=size_t(player.position());
            ++count[pos];++count[4];sum[pos]=uint16_t(sum[pos]+player.strength());sum[4]=uint16_t(sum[4]+player.strength());
        }
        auto& averages=all?state.team_strengths:state.active_strengths;
        for(size_t n=0;n<5;++n)averages[n]=count[n]?uint16_t(sum[n]/count[n]):uint16_t(0);
    }
    std::vector<int> borrowed;
    if(g.memory.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField+23)&&g.memory.has(::bmh::layout::ClubInfo_FormatError_RuntimeField2+11))
        for(int loan=0;loan<12;++loan)if(g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(loan)*2)!=0x0fff&&g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2+uint32_t(loan))==manager)borrowed.push_back(loan);
    if(borrowed.size()>range.count)throw FormatError("transfer market: borrowed roster exceeds Source squad range");
    const size_t ordinary_count=range.count-borrowed.size();
    int rendered_row=0;
    for(size_t row=0;row<ordinary_count;++row) {
        ClubInfoPlayer p;p.row=int(row);p.physical_row=int(row);p.displayed_slot=g.squad_slots.at(range.first+row);
        const int id=int(p.displayed_slot&0x0fff);
        if(id!=0x0fff) {
            if(size_t(id)>=g.players.size())throw FormatError("transfer market: roster player outside Native attributes");
            p.player_id=id;p.rendered_row=rendered_row++;p.original_name=g.player_name(id);p.attributes=g.players[size_t(id)];
            if(size_t(id)<g.player_cards.size())p.cards=g.player_cards[size_t(id)];
            if(size_t(id)<g.player_bonus.size())p.bonus=g.player_bonus[size_t(id)];
            for(int loan=0;loan<200;++loan)if(g.memory.has(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(loan)*4+3)) {
                const uint32_t value=g.memory.u32(::bmh::layout::Career_RemoveJoinLoans_RuntimeField+uint32_t(loan)*4);
                if(value&&int(value&0x0fff)==id){p.ordinary_loan=true;break;}
            }
            const size_t board=size_t(manager)*0x840+row*0x58;
            if(board+0x58<=g.tactics.size()) {p.manager_board.emplace();std::copy_n(g.tactics.begin()+std::ptrdiff_t(board),0x58,p.manager_board->begin());}
            for(int offer=0;offer<4;++offer)if(g.memory.has(::bmh::layout::OfferSlots+uint32_t(offer)*4+3)) {
                const uint32_t word=g.memory.u32(::bmh::layout::OfferSlots+uint32_t(offer)*4);
                if(int(word&31)==manager&&int(word>>10&0xfff)==id)p.offer_name_color=true;
            }
        }
        state.roster.players.push_back(std::move(p));
    }
    for(size_t index=0;index<borrowed.size();++index) {
        const int loan=borrowed[index],id=g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(loan)*2);
        if(auto preview=transfer_market_snapshot_preview(g,manager,state.runtime_club,loan))state.snapshot_previews.push_back(*preview);
        if(id<0||size_t(id)>=g.players.size())throw FormatError("transfer market: borrowed player outside Native attributes");
        ClubInfoPlayer p;p.row=int(ordinary_count+index);p.physical_row=-1;p.human_loan_index=loan;p.borrowed=true;
        p.displayed_slot=uint16_t(id);p.player_id=id;p.rendered_row=rendered_row++;p.original_name=g.player_name(id);p.attributes=g.players[size_t(id)];
        if(size_t(id)<g.player_cards.size())p.cards=g.player_cards[size_t(id)];
        if(size_t(id)<g.player_bonus.size())p.bonus=g.player_bonus[size_t(id)];
        const uint32_t board=::bmh::layout::ClubInfo_FormatError_RuntimeField+uint32_t(loan)*0x58;
        if(!g.memory.has(board+0x57))throw FormatError("transfer market: borrowed board outside Native storage");
        p.manager_board.emplace();for(uint32_t n=0;n<0x58;++n)(*p.manager_board)[n]=g.memory.u8(board+n);
        state.roster.players.push_back(std::move(p));
    }
    for (size_t row = 0; row < ordinary_count; ++row) {
        const uint16_t slot = g.squad_slots.at(range.first + row);
        const int player_id = slot & 0x0fff;
        if (player_id == 0x0fff) continue;
        if (size_t(player_id) >= g.players.size()) throw FormatError("transfer market: squad player is outside attributes");
        const Player& player = g.players.at(size_t(player_id));
        state.squad.push_back({int(row), player_id, slot, g.player_name(player_id),
                               player.age(), player.position(), player.strength(), listed_players.at(size_t(player_id))});
    }
    return state;
}

std::string transfer_market_state_json(const TransferMarketState& state) {
    std::ostringstream out;
    out << "{\"screen\":\"transfer_market\",\"source\":" << json_quote(state.source)
        << ",\"manager_slot\":" << state.manager_slot << ",\"runtime_club\":" << state.runtime_club
        << ",\"manager_name\":" << json_quote(original_to_utf8(state.manager_name))
        << ",\"club_name\":" << json_quote(original_to_utf8(state.club_name))
        << ",\"revision\":" << state.revision << ",\"balance\":" << state.balance
        << ",\"credit_line\":" << state.credit_line << ",\"items\":[";
    for (size_t i = 0; i < state.items.size(); ++i) {
        if (i) out << ',';
        const auto& item = state.items[i];
        out << "{\"list_entry\":" << item.list_entry << ",\"source_slot\":" << item.source_slot << ",\"player\":" << item.player
            << ",\"name\":" << json_quote(original_to_utf8(item.name)) << ",\"seller\":" << item.seller
            << ",\"seller_name\":" << json_quote(original_to_utf8(item.seller_name))
            << ",\"age\":" << item.age << ",\"position\":" << item.position
            << ",\"strength\":" << item.strength << ",\"asking_fee\":" << item.asking_fee
            << ",\"salary_reference\":" << item.salary_reference
            << ",\"salary_demand_percent\":" << unsigned(item.salary_demand_percent)
            << ",\"offers\":" << item.offers << ",\"own_listing\":" << (item.own_listing ? "true" : "false")
            << ",\"own_bid_pending\":" << (item.own_bid_pending ? "true" : "false") << '}';
    }
    out << "],\"squad\":[";
    for (size_t i = 0; i < state.squad.size(); ++i) {
        if (i) out << ',';
        const auto& p = state.squad[i];
        out << "{\"row\":" << p.row << ",\"player\":" << p.player
            << ",\"name\":" << json_quote(original_to_utf8(p.name)) << ",\"age\":" << p.age
            << ",\"position\":" << p.position << ",\"strength\":" << p.strength
            << ",\"already_listed\":" << (p.already_listed ? "true" : "false") << '}';
    }
    out << "]}";
    return out.str();
}

TaxDialogProjection poach_notice_from_game(const GameState& game,const PoachStart& attempt,std::string body) {
    const int manager=attempt.manager;
    const size_t base=size_t(manager)*kManagerRecordSize;
    if(manager<0 || manager>3 || size_t(manager)>=game.manager_teams.size() ||
       base+0x223>game.manager_records.size())throw FormatError("poach notice: invalid manager");
    TaxDialogProjection out;
    out.kind=TaxDialogKind::acknowledgement;out.body=std::move(body);
    // Reset uses57eca (interactive ACK) only for the on-loan
    // player. The remaining refusals are583f2 timed manager notices.
    out.source_manager_flags=uint8_t(manager)|(attempt.status==PoachStatus::on_loan?0x80:0x40);
    const size_t name=base+0xa8;
    size_t end=name;while(end<name+30 && game.manager_records[end]!=0)++end;
    out.manager_name.assign(reinterpret_cast<const char*>(game.manager_records.data()+name),end-name);
    out.portrait=game.manager_records[base+0x221];
    if(out.portrait>14)throw FormatError("poach notice: invalid Original portrait");
    const int team=game.manager_teams[size_t(manager)];
    out.badge_fallback=team==0x0fff || (team>89&&team<138) || (team>167&&team<357);
    if(!out.badge_fallback) {
        if(team<0 || size_t(team)*2+2>game.club_heap_78.size())throw FormatError("poach notice: missing badge selector");
        out.badge_sprite=(int(game.club_heap_78[size_t(team)*2])|int(game.club_heap_78[size_t(team)*2+1])<<8)&0x1ff;
    }
    out.compact_dialog_frame=game.memory.has(::bmh::layout::CompactDialogFrame)&&game.memory.u8(::bmh::layout::CompactDialogFrame)!=0;
    return out;
}

std::string transfer_market_request_json(const TransferMarketRequest& r) {
    const char* kind = r.kind == TransferMarketRequestKind::submit_bid ? "submit_bid" :
        r.kind == TransferMarketRequestKind::list_player ? "list_player" :
        r.kind == TransferMarketRequestKind::open_offer ? "open_offer" :
        r.kind == TransferMarketRequestKind::preview_quote ? "preview_quote" :
        r.kind == TransferMarketRequestKind::open_poach ? "open_poach" : "none";
    std::ostringstream out;
    out << "{\"kind\":\"" << kind << "\",\"manager_slot\":" << r.manager_slot
        << ",\"runtime_club\":" << r.runtime_club << ",\"revision\":" << r.revision
        << ",\"squad_row\":" << r.squad_row << ",\"expected_player\":" << r.expected_player
        << ",\"preview_source_slot\":" << r.preview_source_slot << ",\"preview_seller\":" << r.preview_seller
        << ",\"preview_is_purchase\":" << (r.preview_is_purchase?"true":"false")
        << ",\"preview_is_loan\":" << (r.preview_is_loan?"true":"false")
        << ",\"preview_snapshot_index\":" << r.preview_snapshot_index
        << ",\"offer_is_loan\":" << (r.offer_is_loan?"true":"false")
        << ",\"list_entry\":" << r.bid.list_entry << ",\"source_slot\":" << r.bid.source_slot << ",\"player\":" << r.bid.player
        << ",\"fee\":" << r.bid.fee << ",\"annual_salary\":" << r.bid.annual_salary
        << ",\"contract_years\":" << unsigned(r.bid.contract_years) << ",\"bonuses\":[";
    for (size_t i = 0; i < r.bid.bonuses.size(); ++i) { if (i) out << ','; out << r.bid.bonuses[i]; }
    out << "],\"selected_players\":[";
    for(size_t n=0;n<r.selected_players.size();++n){if(n)out<<',';const auto& p=r.selected_players[n];out<<"{\"list_entry\":"<<p.list_entry<<",\"source_slot\":"<<p.source_slot<<",\"player\":"<<p.player<<",\"seller\":"<<p.seller<<",\"squad_row\":"<<p.squad_row<<'}';}
    out << "]}";
    return out.str();
}

TransferMarketScreen::TransferMarketScreen(const std::filesystem::path& data, TransferMarketState state)
    : state_(std::move(state)), font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      exe_(load_le(asset_path(data,"BMH.exe"))),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),sprite_font_(data),
      background_(load_vga(asset_path(data / "KB", "56.VGA"))),
      frame_(load_vga(asset_path(data / "KB", "129.VGA"))),widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      atlas_(load_vga(asset_path(data / "KB", "2.VGA"))),flags_(load_vga(asset_path(data / "KB", "24.VGA"))),
      market_widgets_(load_vga(asset_path(data / "KB", "30.VGA"))) {
    if (background_.width != 318 || background_.height != 207 || frame_.width!=320 || frame_.height!=240 || state_.manager_slot < 0 || state_.runtime_club < 0)
        throw FormatError("transfer market: invalid screen state or assets");
    if (state_.items.size() > 40) throw FormatError("transfer market: too many list rows");
    view_flags_=uint16_t((state_.source_view_flags & 0x7ff) | 0x800);
    if(atlas_.width==314){Image padded(320,atlas_.height);padded.blit(atlas_,0,0);atlas_=std::move(padded);}
}

void TransferMarketScreen::draw_text(Image& out, int x, int y, std::string_view value, uint8_t color) const {
    // User-facing strings and notices are UTF-8. The original bitmap font uses
    // the game's single-byte encoding, so convert only at this render boundary.
    std::string encoded;
    try { encoded = utf8_to_original(value); }
    catch (const FormatError&) {
        for (unsigned char c : value) encoded += c < 128 ? char(c) : '?';
    }
    const int width = font_.text_width(encoded);
    if (x < 0 || y < 0 || x + width > out.width || y + 6 > out.height) return;
    font_.draw(out, x, y, encoded, color);
}

void TransferMarketScreen::commit_numeric_field() {
    if (!numeric_input_.empty()) offer_values_.at(size_t(active_field_)) = parse_input(numeric_input_);
    numeric_input_.clear();
}

std::vector<int> TransferMarketScreen::market_order() const {
    std::vector<int> rows;std::vector<uint32_t> keys;
    for(size_t n=0;n<state_.items.size();++n) {
        const auto& item=state_.items[n];
        if(item.position<0||item.position>3||!(view_flags_&(1u<<(item.position+5))))continue;
        rows.push_back(int(n));keys.push_back(uint32_t(item.position*0x400+0x200-item.strength));
    }
    // Games's Hoare partition swaps equal keys too. Stable sorting would
    // silently change which tied Native players occur on each six-row page.
    const auto sort=[&](auto&& self,size_t first,size_t count)->void {
        while(count>1) {
            const uint32_t pivot=keys[first+count/2];int left=-1,right=int(count);
            for(;;) {
                do{++left;}while(keys[first+size_t(left)]<pivot);
                do{--right;}while(pivot<keys[first+size_t(right)]);
                if(right<=left)break;
                std::swap(keys[first+size_t(left)],keys[first+size_t(right)]);
                std::swap(rows[first+size_t(left)],rows[first+size_t(right)]);
            }
            self(self,first,size_t(left));first+=size_t(left);count-=size_t(left);
        }
    };
    sort(sort,0,rows.size());return rows;
}
std::vector<std::pair<int,int>> TransferMarketScreen::roster_rows() const {
    std::vector<std::pair<int,int>> rows;int baseline=36,loan=0;
    for(size_t n=0;n<state_.roster.players.size();++n) {
        const auto& player=state_.roster.players[n];
        if(player.borrowed){if(player.player_id>=0)rows.emplace_back(int(n),209-8*loan++);continue;}
        if(n&&(player.displayed_slot&0xc000)!=(state_.roster.players[n-1].displayed_slot&0xc000))baseline+=2;
        if(player.player_id<0)continue;
        rows.emplace_back(int(n),baseline);baseline+=7;
    }
    return rows;
}
void TransferMarketScreen::original_text(Image& out,int x,int baseline,std::string_view value,uint8_t color,int right) const {
    const int width=font_.text_width(value);if(right>=0)x+=(right-x)/2-width/2;
    if(x<0||x+width>out.width||baseline<5||baseline>=out.height)return;
    Image mask(std::max(1,width),6);font_.draw(mask,0,0,value,1);
    int gradient=0;while(gradient<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+gradient)!=color)++gradient;
    const bool shade=state_.gradient_text&&exe_.u8(::bmh::layout::TextGradientMode)==2;
    for(int y=0;y<6;++y)for(int xx=0;xx<width;++xx)if(mask.at(xx,y))
        out.at(x+xx,baseline-5+y)=shade?exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+gradient*6+y):color;
}
void TransferMarketScreen::button(Image& out,int x,int y,int width,std::string_view label,bool active) const {
    const int height=width>31?10:9;
    if(active){const uint8_t c=out.at(x,y+height);rect(out,x,y,width+1,2,c);rect(out,x,y,2,height+1,c);x+=2;y+=2;}
    else{rect(out,x+2,y+height,width,2,0x40);rect(out,x+width,y+2,2,height,0x40);}
    if(width>31) {
        const int sy=active?44:74;copy(out,widgets_,24,sy,x,y,31,10);int at=31,left=width-31;
        while(left>20){const int n=std::min(8,left-20);copy(out,widgets_,24,sy+10,x+at,y,n,10);at+=n;left-=n;}
        copy(out,widgets_,45-left,sy+20,x+at,y,left,10);
    } else {
        for(int yy=4;yy<height-3;++yy)for(int xx=4;xx<width-3;++xx)
            out.at(x+xx,y+yy)=exe_.u8(::bmh::layout::Advertising_PatternRect_RuntimeField+(yy-4)%16*16+(xx-4)%16);
        copy(out,widgets_,0,0,x,y,4,4);copy(out,widgets_,4,0,x+width-4,y,4,4);
        copy(out,widgets_,0,4,x,y+height-4,4,4);copy(out,widgets_,4,4,x+width-4,y+height-4,4,4);
        copy(out,widgets_,0,104,x+4,y,width-8,4);copy(out,widgets_,0,108,x+4,y+height-4,width-8,4);
        copy(out,widgets_,0,8,x,y+4,4,height-8);copy(out,widgets_,4,8,x+width-4,y+4,4,height-8);
    }
    const int mode=exe_.u8(::bmh::layout::Advertising_Button_RuntimeField + int(active));
    if(mode==3)original_text(out,x,y+height/2+2,label,0,x+width);
    else sprite_font_.draw(out,x+width/2-sprite_font_.text_width(original_to_utf8(label))/2,y+height/2+2,original_to_utf8(label),mode);
}
void TransferMarketScreen::draw_roster(Image& out,bool full) const {
    const auto header=texts_[text_id(data_b_?::bmh::layout::ClubInfo_Render_TextPointer4: ::bmh::layout::ClubInfo_Render_TextPointer3)];
    // LineupScreen renders the complete header, then19c1c restores the right
    // pane when Komplett is off. The same clipping is applied to data below.
    original_text(out,0,26,header,0x51);
    for(const auto [row,baseline]:roster_rows()) {
        const auto& p=state_.roster.players[size_t(row)];const auto& a=p.attributes;
        if(p.displayed_slot&0xc000) {
            const int jersey=(p.cards&31)+1;
            const uint8_t color=p.displayed_slot&0x1000?0xf3:p.borrowed?0x75:p.displayed_slot&0x8000?0x6c:0x68;
            original_text(out,4,baseline,std::to_string(jersey%10),color);
            if(jersey>=10)original_text(out,0,baseline,exe_.cstring(::bmh::layout::ClubInfo_Render_TextPointer),color);
        }
        uint8_t color=p.borrowed?0x65:p.offer_name_color?0x75:uint8_t(p.rendered_row%2?0x67:0x6b);
        if((selected_snapshot_&&state_.snapshot_previews.at(size_t(*selected_snapshot_)).snapshot_index==p.human_loan_index)||
           (selected_squad_&&state_.squad.at(size_t(*selected_squad_)).row==p.physical_row)||
           std::any_of(marked_squad_.begin(),marked_squad_.end(),[&](int n){return state_.squad[size_t(n)].row==p.physical_row;}))color=0x27;
        original_text(out,13,baseline,p.original_name,color);
        if(p.ordinary_loan)copy(out,atlas_,304,9,9,baseline-4,16,4,0);
        if(!data_b_) {
            original_text(out,66,baseline,texts_[text_id(::bmh::layout::ClubInfo_Render_TextPointer2)+size_t(a.position())],0x6c,82);
            for(int field=0;field<2;++field) {
                int v=int(a.w35c>>(field==0?24:21)&7);if(v>4)--v;
                copy(out,atlas_,208+v*16,field==0?17:22,field==0?88:103,baseline-5,16,5,0);
            }
            original_text(out,117,baseline,padded(a.condition())+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField)+padded(a.technique())+
                exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField)+padded(a.form())+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField2)+padded(a.strength())+
                exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4),0x6c);
            if(p.manager_board) {
                const auto& board=*p.manager_board;
                copy(out,atlas_,304,int(board[0xe]>>6)*3,179,baseline-4,16,3,0);
                if(full)original_text(out,232,baseline,padded(board[0xf]),board[0xf]<61?0x66:board[0xf]<71?0x27:0xfb);
            }
            if(full) {
                const int marker=int(a.w35c>>27),injury=int(a.w364>>16&31);
                if(p.borrowed)copy(out,atlas_,197,25,196,baseline-5,10,5);
                else if(marker==0&&injury==0)copy(out,atlas_,272,p.displayed_slot&0x8000?0:p.displayed_slot&0x4000?5:10,191,baseline-5,32,5,0);
                else copy(out,atlas_,197,((marker==0?5:marker==31?8:injury==0?4:3)-3)*6+1,196,baseline-5,10,5);
                const int nation=a.nationality()+((p.bonus&128)>>2);
                copy(out,flags_,nation%12*9,nation/12*6,217,baseline-6,9,6);
                original_text(out,247,baseline,padded(a.w364>>26)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4)+padded(a.w364>>2&63)+
                    exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4)+padded(a.w368>>18&63)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4)+padded(a.w368>>14&15)+
                    exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField4)+padded(a.w368>>24&7)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField),0x6c);
            }
        } else {
            original_text(out,65,baseline,padded(a.age())+texts_[text_id(::bmh::layout::ClubInfo_Render_TextPointer5)],0x6c);
            for(int skill=0;skill<7;++skill)copy(out,atlas_,256+a.skill(Player::Skill(skill))*16,32,87+skill*10,baseline-5,16,5,0);
            std::string stats=padded(p.bonus&127,3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5);
            if(p.manager_board) {
                const auto& b=*p.manager_board;stats+=padded(b[7],3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5)+padded(b[9],3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5);
                if(full){Bytes raw(b.begin(),b.end());stats+=padded(le16(raw,0x48),3)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2)+padded(le16(raw,0x4a),3)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2)+padded(le16(raw,0x4c),3)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField);}
            }
            original_text(out,165,baseline,stats,0x6c);
        }
    }
}
void TransferMarketScreen::draw_market_overview(Image& out) const {
    const bool full=(view_flags_&1)!=0;
    draw_roster(out,full);
    if(!full) {
        // Reset restores KB56's right pane over extended roster columns.
        if(state_.textured_panes)copy(out,background_,184,1,185,21,134,132);
        else rect(out,184,20,136,134,0x42);
        rect(out,184,20,1,134,0x46);rect(out,184,20,136,1,0x46);
        rect(out,184,153,136,1,0x40);rect(out,319,20,1,134,0x40);
        const auto order=market_order();
        const size_t first=order.empty()?0:std::min(market_page_,(order.size()-1)/6)*6;
        for(size_t n=first;n<order.size()&&n<first+6;++n) {
            const auto& item=state_.items[size_t(order[n])];const auto& a=item.attributes;
            const int baseline=30+int(n-first)*15;
            uint8_t color=item.own_listing?0xf7:uint8_t(n%2?0x6a:0x5f);
            const uint8_t name_color=selected_item_==order[n]||std::find(marked_items_.begin(),marked_items_.end(),order[n])!=marked_items_.end()?0x27:color;
            original_text(out,205,baseline,item.name,name_color);
            original_text(out,262,baseline,texts_[text_id(::bmh::layout::ClubInfo_Render_TextPointer2)+size_t(item.position)],color);
            for(int field=0;field<2;++field) {
                int v=int(a.w35c>>(field==0?24:21)&7);if(v>4)--v;
                copy(out,atlas_,208+v*16,field==0?17:22,field==0?283:293,baseline-4,16,5,0);
            }
            copy(out,flags_,int(item.nationality%12)*9,int(item.nationality/12)*6,193,baseline-2,9,6);
            original_text(out,205,baseline+7,padded(a.condition())+exe_.cstring(::bmh::layout::TransferMarket_Rect_TextPointer2)+padded(a.technique())+
                exe_.cstring(::bmh::layout::TransferMarket_Rect_TextPointer2)+padded(a.form())+exe_.cstring(::bmh::layout::TransferMarket_Rect_ExecutableField)+padded(item.strength)+exe_.cstring(::bmh::layout::TransferMarket_Rect_ExecutableField2)+
                padded(item.age)+texts_[text_id(::bmh::layout::TransferMarket_Rect_TextPointer6)],color);
        }
        copy(out,market_widgets_,0,32,208,125,35,23,0);
        original_text(out,250,132,original_money(state_.balance),0x75);
        if(preview_) {
            original_text(out,184,124,texts_[text_id(::bmh::layout::TransferMarket_Rect_TextPointer7)]+(preview_->purchase?state_.items.at(size_t(*selected_item_)).seller_name:state_.club_name),0x27,320);
            original_text(out,250,142,original_money(preview_->delta),0x6b);
            original_text(out,250,150,original_money(preview_->projected_balance),0x5f);
            original_text(out,242,150,exe_.cstring(::bmh::layout::TransferMarket_Rect_TextPointer),0x69);
        }
        const auto& average=(view_flags_&0x400)?state_.team_strengths:state_.active_strengths;
        for(int n=0;n<4;++n) {
            const int height=std::clamp(average[4]?average[size_t(n)]*100/average[4]:0,91,109)-90,x=228+n*23;
            copy(out,market_widgets_,0,27,x,195,21,5,0);
            copy(out,market_widgets_,0,5,x,195-height,21,height,0);
            copy(out,market_widgets_,0,0,x,192-height,21,5,0);
            original_text(out,236+n*23,172,padded(average[size_t(n)]),0x6b);
        }
        original_text(out,201,183,padded(average[4]),0x6b);
        for(int n=4;n<13;++n)button(out,exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField + n*6),exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField2+n*6),exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField3+n*6),texts_[text_id(::bmh::layout::TransferMarket_Rect_TextPointer5)+size_t(n)],(view_flags_&(1u<<n))!=0);
    }
    button(out,1,215,51,texts_[text_id(data_b_?::bmh::layout::TransferMarket_Rect_TextPointer3: ::bmh::layout::TransferMarket_Rect_TextPointer4)],true);
    button(out,55,215,51,texts_[text_id(::bmh::layout::TransferMarket_Rect_TextPointer5)],full);
    for(int n=1;n<4;++n)if(!full&&state_.source_market_mode==3)button(out,exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField + n*6),exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField2+n*6),exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField3+n*6),texts_[text_id(::bmh::layout::TransferMarket_Rect_TextPointer5)+size_t(n)],(view_flags_&(1u<<n))!=0);
}
std::string TransferMarketScreen::original_money(int32_t value) const {
    const int32_t numerator=state_.currency?state_.currency->first:int32_t(exe_.u32(::bmh::layout::CurrencyMultiplier));
    const int32_t denominator=state_.currency?state_.currency->second:int32_t(exe_.u32(::bmh::layout::CurrencyDivisor));
    if(denominator==0)throw FormatError("transfer market: invalid Original currency denominator");
    const auto converted=int64_t(value)*numerator/denominator;
    std::string result=money(converted);
    if(result.size()<9)result.insert(0,9-result.size(),'%');
    return result+exe_.cstring(::bmh::layout::CurrencySuffixText);
}
Image TransferMarketScreen::render() const {
    Image out=state_.decorated_background?frame_:Image(320,240);
    if(!state_.decorated_background){rect(out,0,4,320,13,0x44);rect(out,1,5,318,11,0x39);rect(out,0,228,320,10,0x44);rect(out,1,229,318,8,0x39);}
    const auto title=original_to_utf8(texts_[text_id(::bmh::layout::TransferMarket_Render_TextPointer)]);
    sprite_font_.draw(out,SpriteFont::centered_title_x(sprite_font_.text_width(title)),13,title);
    sprite_font_.draw(out,5,235,original_to_utf8(state_.club_name));
    draw_market_overview(out);
    return out;
}

TransferMarketRequest TransferMarketScreen::make_bid() {
    if (pending_ || !selected_item_) return {};
    if (offer_values_[0] == 0 || offer_values_[1] == 0 || offer_values_[2] < 1 || offer_values_[2] > 4) {
        feedback_ = "Ablöse, Gehalt und Laufzeit prüfen"; return {};
    }
    if (offer_values_[0] > uint32_t(std::numeric_limits<int32_t>::max()) ||
            offer_values_[1] > uint32_t(std::numeric_limits<int32_t>::max())) {
        feedback_ = "Betrag außerhalb des zulässigen Bereichs"; return {};
    }
    for (size_t i = 3; i < offer_values_.size(); ++i) if (offer_values_[i] > 100) {
        feedback_ = "Prämie muss 0..100 sein"; return {};
    }
    const auto& item = state_.items.at(size_t(*selected_item_));
    if (item.own_listing) { feedback_ = "Ein eigener Spieler kann nicht gekauft werden"; return {}; }
    TransferMarketRequest request;
    request.kind = TransferMarketRequestKind::submit_bid;
    request.manager_slot = state_.manager_slot; request.runtime_club = state_.runtime_club; request.revision = state_.revision;
    request.bid.manager = state_.manager_slot; request.bid.list_entry = item.list_entry; request.bid.player = item.player;
    request.bid.source_slot = item.source_slot;
    request.bid.fee = int32_t(offer_values_[0]); request.bid.annual_salary = int32_t(offer_values_[1]);
    request.bid.contract_years = uint8_t(offer_values_[2]);
    for (size_t i = 0; i < request.bid.bonuses.size(); ++i) request.bid.bonuses[i] = offer_values_[i + 3];
    pending_ = request; view_ = View::market; numeric_input_.clear();
    feedback_.clear();
    return request;
}

TransferMarketRequest TransferMarketScreen::make_listing(bool loan) {
    if (pending_ || !selected_squad_) return {};
    const auto& item = state_.squad.at(size_t(*selected_squad_));
    if (item.already_listed) { feedback_ = "Spieler steht bereits auf der Liste"; return {}; }
    TransferMarketRequest request;
    request.kind = TransferMarketRequestKind::list_player;request.offer_is_loan=loan;
    request.manager_slot = state_.manager_slot; request.runtime_club = state_.runtime_club; request.revision = state_.revision;
    request.squad_row = item.row; request.expected_player = item.player;
    for(size_t n=0;n<state_.squad.size();++n) {if(std::find(marked_squad_.begin(),marked_squad_.end(),int(n))==marked_squad_.end())continue;const auto& marked=state_.squad[n];request.selected_players.push_back({-1,state_.roster.squad_start_offset+marked.row,marked.player,state_.runtime_club,marked.row});}
    if(request.selected_players.empty())request.selected_players.push_back({-1,state_.roster.squad_start_offset+item.row,item.player,state_.runtime_club,item.row});
    pending_ = request; feedback_.clear();
    return request;
}

TransferMarketRequest TransferMarketScreen::make_preview(bool purchase) {
    if(pending_)return {};
    TransferMarketRequest request;request.kind=TransferMarketRequestKind::preview_quote;
    request.manager_slot=state_.manager_slot;request.runtime_club=state_.runtime_club;request.revision=state_.revision;
    request.preview_is_purchase=purchase;request.preview_is_loan=(view_flags_&0x1000)!=0;
    if(purchase) {
        if(!selected_item_)return {};
        const auto& item=state_.items.at(size_t(*selected_item_));
        request.bid.manager=state_.manager_slot;request.bid.list_entry=item.list_entry;request.bid.player=item.player;
        request.bid.source_slot=item.source_slot;request.expected_player=item.player;
        request.preview_source_slot=item.source_slot;request.preview_seller=item.seller;
    } else if(selected_snapshot_) {
        const auto& item=state_.snapshot_previews.at(size_t(*selected_snapshot_));
        request.squad_row=item.displayed_row;request.expected_player=item.player;
        request.preview_source_slot=item.source_slot;request.preview_seller=state_.runtime_club;
        request.preview_snapshot_index=item.snapshot_index;
    } else {
        if(!selected_squad_)return {};
        const auto& item=state_.squad.at(size_t(*selected_squad_));
        request.squad_row=item.row;request.expected_player=item.player;
        request.preview_source_slot=state_.roster.squad_start_offset+item.row;
        request.preview_seller=state_.runtime_club;
    }
    pending_=request;preview_.reset();return request;
}
TransferMarketRequest TransferMarketScreen::make_open_offer(bool loan) {
    if(pending_||!selected_item_)return {};
    const auto& item=state_.items.at(size_t(*selected_item_));
    if(item.own_listing)return {};
    TransferMarketRequest request;request.kind=TransferMarketRequestKind::open_offer;request.offer_is_loan=loan;
    request.manager_slot=state_.manager_slot;request.runtime_club=state_.runtime_club;request.revision=state_.revision;
    if(item.own_bid)request.bid=*item.own_bid;
    request.bid.manager=state_.manager_slot;request.bid.list_entry=item.list_entry;request.bid.player=item.player;
    request.bid.source_slot=item.source_slot;
    for(size_t n=0;n<state_.squad.size();++n){if(std::find(marked_squad_.begin(),marked_squad_.end(),int(n))==marked_squad_.end())continue;const auto& marked=state_.squad[n];request.selected_players.push_back({-1,state_.roster.squad_start_offset+marked.row,marked.player,state_.runtime_club,marked.row});}
    for(int n:market_order()) {if(std::find(marked_items_.begin(),marked_items_.end(),n)==marked_items_.end())continue;const auto& marked=state_.items.at(size_t(n));request.selected_players.push_back({marked.list_entry,marked.source_slot,marked.player,marked.seller,-1});}
    if(request.selected_players.empty())request.selected_players.push_back({item.list_entry,item.source_slot,item.player,item.seller,-1});
    pending_=request;return request;
}
TransferMarketRequest TransferMarketScreen::move(int x,int y) {
    if(pending_||close_requested_)return {};
    if(!(view_flags_&1)&&x>=201&&x<=297&&y>=25&&y<115) {
        const auto order=market_order();const size_t first=order.empty()?0:std::min(market_page_,(order.size()-1)/6)*6,index=first+size_t((y-25)/15);
        if(index<order.size()){if(selected_item_==order[index])return {};selected_item_=order[index];selected_squad_.reset();selected_snapshot_.reset();return make_preview(true);}
    }
    if(x>=9&&x<=60)for(const auto [row,baseline]:roster_rows())if(y>=baseline-7&&y<baseline+2) {
        const auto& p=state_.roster.players[size_t(row)];
        if(p.borrowed) {
            const auto found=std::find_if(state_.snapshot_previews.begin(),state_.snapshot_previews.end(),[&](const auto& item){return item.snapshot_index==p.human_loan_index&&item.displayed_row==p.row&&item.player==p.player_id;});
            if(found==state_.snapshot_previews.end()) {
                selected_squad_.reset();selected_snapshot_.reset();selected_item_.reset();preview_.reset();return {};
            }
            const int index=int(found-state_.snapshot_previews.begin());if(selected_snapshot_==index)return {};
            selected_squad_.reset();selected_item_.reset();selected_snapshot_=index;
            if(view_flags_&1)return {};return make_preview(false);
        }
        const auto found=std::find_if(state_.squad.begin(),state_.squad.end(),[&](const auto& item){return item.row==p.physical_row;});
        if(found==state_.squad.end())return {};
        const int index=int(found-state_.squad.begin());if(selected_squad_==index)return {};
        selected_squad_=index;selected_snapshot_.reset();selected_item_.reset();if(view_flags_&1)return {};return make_preview(false);
    }
    selected_item_.reset();selected_squad_.reset();selected_snapshot_.reset();preview_.reset();return {};
}
TransferMarketRequest TransferMarketScreen::click(int x,int y,bool right) {
    if(pending_||close_requested_)return {};
    if(right) {
        if(!(view_flags_&1)&&x>=185&&x<=319&&y>=21&&y<=163) {
            const auto rows=market_order();market_page_+=1;
            if(rows.empty()||market_page_*6>=rows.size())market_page_=0;
            selected_item_.reset();preview_.reset();return {};
        }
        close_requested_=true;return {};
    }
    if(x>=1&&x<=52&&y>=215&&y<=225){data_b_=!data_b_;return {};}
    if(x>=55&&x<=106&&y>=215&&y<=225){view_flags_^=1;selected_item_.reset();preview_.reset();return {};}
    if(!(view_flags_&1)) {
        // Reset's logical4 follows Data and the three leading market
        // buttons: its physical EXE/TransferMarket_Click_RuntimeField entry3 is Abwerben.
        const int px=exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField + 3*6),py=exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField2+3*6),pw=exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField3+3*6);
        if(state_.source_market_mode==3&&x>=px&&x<=px+pw&&y>=py&&y<=py+10) {
            TransferMarketRequest request;
            request.kind=TransferMarketRequestKind::open_poach;
            request.manager_slot=state_.manager_slot;request.runtime_club=state_.runtime_club;
            request.revision=state_.revision;request.bid.manager=state_.manager_slot;
            request.offer_is_loan=(view_flags_&0x1000)!=0;
            pending_=request;return request;
        }
        for(int button=4;button<=10;++button) {
            const int bx=exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField + button*6),by=exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField2+button*6),width=exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField3+button*6);
            if(x<bx||x>bx+width||y<by||y>by+10)continue;
            if(button<=8) {
                // Source ALL/position toggles always leave at least one class.
                const uint16_t before=view_flags_;view_flags_^=uint16_t(1u<<button);
                if(button==4)view_flags_|=0x1f0;
                if((view_flags_&0x1e0)==0)view_flags_=before;
                if(bool(view_flags_&0x10)!=bool((view_flags_&0x1e0)==0x1e0))view_flags_^=0x10;
                market_page_=0;selected_item_.reset();preview_.reset();
            } else view_flags_^=0x600;
            return {};
        }
        const auto order=market_order();
        const size_t first=order.empty()?0:std::min(market_page_,(order.size()-1)/6)*6;
        if(x>=201&&x<=297&&y>=25&&y<115) {
            const size_t row=size_t((y-25)/15),index=first+row;
            if(index<order.size()&&row<6) {
                const int item=order[index];auto marked=std::find(marked_items_.begin(),marked_items_.end(),item);
                if(marked==marked_items_.end())marked_items_.push_back(item);else marked_items_.erase(marked);
                return move(x,y);
            }
        }
        if(y>=202&&y<=212&&x>=185&&x<=318) {
            if(!marked_items_.empty()){selected_item_=marked_items_.front();selected_squad_.reset();selected_snapshot_.reset();return make_open_offer(x>=254);}
            if(!marked_squad_.empty()){selected_squad_=marked_squad_.front();selected_item_.reset();return make_listing(x>=254);}
            view_flags_^=0x1800;preview_.reset();return {};
        }
    }
    if(x>=9&&x<=60)for(const auto [row,baseline]:roster_rows())if(y>=baseline-7&&y<baseline+2) {
        const auto& player=state_.roster.players.at(size_t(row));
        if(player.borrowed)return {}; // Borrowed-player business remains Source-owned elsewhere.
        const auto found=std::find_if(state_.squad.begin(),state_.squad.end(),[&](const auto& item){return item.row==player.physical_row;});
        if(found==state_.squad.end())return {};
        const int index=int(found-state_.squad.begin());
        auto marked=std::find(marked_squad_.begin(),marked_squad_.end(),index);
        if(marked==marked_squad_.end())marked_squad_.push_back(index);else marked_squad_.erase(marked);
        return move(x,y);
    }
    return {};
}
TransferMarketRequest TransferMarketScreen::key(std::string_view key) {
    if(pending_||close_requested_)return {};
    if(key=="ESC"||key=="ESCAPE"){close_requested_=true;return {};}
    if(key=="B"&&selected_item_)return make_open_offer();
    if(key=="V"&&selected_squad_)return make_listing();
    if(key=="ENTER") {
        if(selected_item_)return make_open_offer();
        if(selected_squad_)return make_listing();
    }
    return {};
}

bool TransferMarketScreen::text(std::string_view value) {
    if (pending_ || close_requested_ || view_ != View::offer) return false;
    if (value.empty() || numeric_input_.size() + value.size() > 10 ||
        !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    numeric_input_.append(value); return true;
}

void TransferMarketScreen::acknowledge(TransferMarketState updated, std::string notice) {
    if (!pending_ || updated.manager_slot != state_.manager_slot || updated.runtime_club != state_.runtime_club ||
        updated.revision <= state_.revision) throw FormatError("transfer market: acknowledgement requires same manager and newer revision");
    state_ = std::move(updated); pending_.reset(); selected_item_.reset(); selected_squad_.reset();selected_snapshot_.reset();preview_.reset();
    view_ = View::market; numeric_input_.clear();marked_items_.clear();marked_squad_.clear();
    feedback_ = std::move(notice);
}
void TransferMarketScreen::acknowledge_unchanged() {
    if (!pending_) throw FormatError("transfer market: no pending request to acknowledge");
    pending_.reset(); numeric_input_.clear(); feedback_.clear();
}
void TransferMarketScreen::acknowledge_preview(TransferMarketState updated,TransferMarketPreview quote) {
    if(!pending_||pending_->kind!=TransferMarketRequestKind::preview_quote||
       updated.manager_slot!=state_.manager_slot||updated.runtime_club!=state_.runtime_club||updated.revision<state_.revision||
       quote.manager_slot!=state_.manager_slot||quote.runtime_club!=state_.runtime_club||quote.revision!=updated.revision||
       quote.source_slot!=pending_->preview_source_slot||quote.player!=pending_->expected_player||
       quote.seller!=pending_->preview_seller||quote.purchase!=pending_->preview_is_purchase||quote.loan!=pending_->preview_is_loan||
       quote.snapshot_index!=pending_->preview_snapshot_index)
        throw FormatError("transfer market: stale or mismatched Source preview quote");
    const auto request=*pending_;
    std::optional<int> selected;
    if(quote.purchase) {
        const auto found=std::find_if(updated.items.begin(),updated.items.end(),[&](const auto& item){return item.list_entry==request.bid.list_entry&&item.source_slot==quote.source_slot&&item.player==quote.player;});
        if(found==updated.items.end())throw FormatError("transfer market: preview listing identity changed");
        selected=int(found-updated.items.begin());
    } else if(request.preview_snapshot_index>=0) {
        const auto found=std::find_if(updated.snapshot_previews.begin(),updated.snapshot_previews.end(),[&](const auto& item){return item.snapshot_index==request.preview_snapshot_index&&item.displayed_row==request.squad_row&&item.source_slot==quote.source_slot&&item.player==quote.player;});
        if(found==updated.snapshot_previews.end())throw FormatError("transfer market: preview snapshot identity changed");
        selected=int(found-updated.snapshot_previews.begin());
    } else {
        const auto found=std::find_if(updated.squad.begin(),updated.squad.end(),[&](const auto& item){return item.row==request.squad_row&&item.player==quote.player;});
        if(found==updated.squad.end())throw FormatError("transfer market: preview squad identity changed");
        selected=int(found-updated.squad.begin());
    }
    state_=std::move(updated);pending_.reset();preview_=quote;
    if(quote.purchase)selected_item_=selected;else if(request.preview_snapshot_index>=0)selected_snapshot_=selected;else selected_squad_=selected;
}
void TransferMarketScreen::refresh_after_offer(TransferMarketState updated) {
    if (pending_ || updated.manager_slot != state_.manager_slot || updated.runtime_club != state_.runtime_club ||
        updated.revision < state_.revision)
        throw FormatError("transfer market: invalid editor-return projection");
    state_ = std::move(updated);
    selected_item_.reset(); selected_squad_.reset();selected_snapshot_.reset(); preview_.reset();
    marked_items_.clear(); marked_squad_.clear();
    feedback_.clear();
}
void TransferMarketScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("transfer market: rejection requires pending request/reason");
    pending_.reset(); feedback_ = std::move(reason); numeric_input_.clear();
}
std::string TransferMarketScreen::state_json() const {
    std::ostringstream out;
    out << "{\"screen\":\"transfer_market\",\"view\":\""
        << (view_ == View::market ? "market" : view_ == View::sell_squad ? "sell_squad" : "offer")
        << "\",\"selected_item\":" << (selected_item_ ? std::to_string(*selected_item_) : "null")
        << ",\"selected_squad\":" << (selected_squad_ ? std::to_string(*selected_squad_) : "null")
        << ",\"selected_snapshot\":" << (selected_snapshot_ ? std::to_string(*selected_snapshot_) : "null")
        << ",\"source_view_flags\":" << view_flags_ << ",\"data_page\":\"" << (data_b_?"B":"A") << "\""
        << ",\"market_page\":" << market_page_
        << ",\"active_field\":" << active_field_ << ",\"numeric_input\":" << json_quote(numeric_input_)
        << ",\"pending\":" << (pending_ ? transfer_market_request_json(*pending_) : "null")
        << ",\"feedback\":" << json_quote(feedback_)
        << ",\"preview\":";
    if (preview_) {
        out << "{\"source_slot\":" << preview_->source_slot << ",\"player\":" << preview_->player
            << ",\"seller\":" << preview_->seller << ",\"revision\":" << preview_->revision
            << ",\"delta\":" << preview_->delta << ",\"projected_balance\":" << preview_->projected_balance
            << ",\"purchase\":" << (preview_->purchase ? "true" : "false")
            << ",\"loan\":" << (preview_->loan ? "true" : "false") << '}';
    } else out << "null";
    out << '}';
    auto serialized=out.str();serialized.pop_back();serialized+=",\"visible_market_entries\":[";
    const auto ordered=market_order();const size_t first=ordered.empty()?0:std::min(market_page_,(ordered.size()-1)/6)*6;
    for(size_t n=first;n<ordered.size()&&n<first+6;++n){if(n!=first)serialized+=',';serialized+=std::to_string(state_.items[size_t(ordered[n])].list_entry);}
    serialized+="]}";return serialized;
}

} // namespace bmh::ui
