// SPDX-License-Identifier: GPL-3.0-only
// Implementation of TransferMarketScreen and the market projection (transfer_market.hpp): list drawing and sorting, quote preview, view switching and request construction.
#include "formats/badge_policy.hpp"
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
// Layout constants of the market projection: index of the first market pool word in the
// squad-slot array, number of list entries, base address and stride of one entry in the
// in-memory transfer list, and the byte size of one manager record.
constexpr size_t kPoolFirst = 0xb70;
constexpr int kMarketEntries = 40;
constexpr uint32_t kTransferTable = ::bmh::layout::EditorList;
constexpr uint32_t kTransferEntrySize = 0x58;
constexpr size_t kManagerRecordSize = 0xb0c;

// Fills a rectangle with one palette index, clipped to the image.
void rect(Image& out,int x,int y,int w,int h,uint8_t color) {
    for(int yy=std::max(0,y);yy<std::min(out.height,y+h);++yy)
        for(int xx=std::max(0,x);xx<std::min(out.width,x+w);++xx)out.at(xx,yy)=color;
}
// Copies a w*h block from a source image to the output at (x,y), clipped to the output.
// Pixels equal to `key` are skipped (transparent); throws if the source rectangle is invalid.
void copy(Image& out,const Image& source,int sx,int sy,int x,int y,int w,int h,int key=-1) {
    if(sx<0||sy<0||w<0||h<0||sx+w>source.width||sy+h>source.height)
        throw FormatError("transfer market: Original asset rectangle outside source");
    for(int yy=0;yy<h;++yy)for(int xx=0;xx<w;++xx) {
        const auto pixel=source.pixels.at(size_t(sy+yy)*size_t(source.width)+size_t(sx+xx));
        if(pixel!=key&&x+xx>=0&&x+xx<out.width&&y+yy>=0&&y+yy<out.height)out.at(x+xx,y+yy)=pixel;
    }
}
// Decimal text of n, left-padded to `width` with '%' characters (used to align number columns).
std::string padded(uint64_t n,int width=2) {
    auto text=std::to_string(n);if(int(text.size())<width)text.insert(0,size_t(width-int(text.size())),'%');return text;
}
// Formats an amount with '.' as thousands separator (e.g. 1.234.567) and a leading '-' if negative.
std::string money(int64_t value) {
    const bool negative = value < 0;
    uint64_t n = uint64_t(negative ? -value : value);
    std::string digits = std::to_string(n);
    for (int at = int(digits.size()) - 3; at > 0; at -= 3) digits.insert(size_t(at), ".");
    if (negative) digits.insert(digits.begin(), '-');
    return digits;
}
// Short German position label: TW, ABW, MIT, STU.
const char* position_name(int position) {
    switch (position) {
    case 0: return "TW";
    case 1: return "ABW";
    case 2: return "MIT";
    case 3: return "STU";
    default: return "?";
    }
}
// Reads a little-endian 32-bit signed value from a byte vector (bounds-checked).
int32_t rd32(const std::vector<uint8_t>& v, size_t at) {
    return int32_t(uint32_t(v.at(at)) | uint32_t(v.at(at + 1)) << 8 |
                   uint32_t(v.at(at + 2)) << 16 | uint32_t(v.at(at + 3)) << 24);
}
// Label of an offer-form field: 0 = fee, 1 = monthly salary, 2 = contract years, 3.. = numbered bonuses.
std::string field_name(int index) {
    if (index == 0) return "Ablöse";
    if (index == 1) return "Monatsgehalt";
    if (index == 2) return "Laufzeit (Jahre)";
    return "Prämie " + std::to_string(index - 2);
}
// Parses typed digits to a number; returns 0 for empty or malformed input.
uint32_t parse_input(std::string_view input) {
    uint32_t value = 0;
    if (input.empty()) return 0;
    const auto result = std::from_chars(input.data(), input.data() + input.size(), value);
    if (result.ec != std::errc{} || result.ptr != input.data() + input.size()) return 0;
    return value;
}
} // namespace

// Builds the screen's data projection from the game state: balance, the 40-entry market list
// (listing seller, asking fee, bids), the manager's own squad rows, borrowed players and team
// averages. Called whenever a fresh projection is needed (screen open, after each acknowledged request).
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

    // Marks which players already appear in the market list (used for the squad's already_listed flag).
    std::vector<bool> listed_players(g.players.size(), false);
    for (int i = 0; i < kMarketEntries; ++i) {
        // Market pool word: low 12 bits are the player id; 0x0fff means the entry is empty.
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
        // Seller word: 0xffff means no club (listing without a seller); otherwise the low 15 bits are the club.
        const uint16_t seller_raw = g.memory.u16(entry + 8);
        if (seller_raw == 0xffff) {
            item.seller = -1;
            item.seller_name = g.club_name(0x0fff);
        } else {
            item.seller = seller_raw & 0x7fff;
            item.seller_name = g.club_name(item.seller);
            item.own_listing = item.seller == state.runtime_club;
        }
        // Asking fee as stored in the list entry, rounded down to whole 10000 as the original does (sub_3C940 of Diamond 3.7:
        // value/10000 times 10000; the older build scaled it by 0x30d4 = 1.25); must fit in int32.
        const uint64_t fee = uint64_t(g.memory.u32(entry + 0x2c) / 10000) * 10000;
        if (fee > uint64_t(std::numeric_limits<int32_t>::max())) throw FormatError("transfer market: asking fee out of range");
        item.asking_fee = int32_t(fee);
        item.salary_reference = g.memory.u32(entry + 0x30);
        item.salary_demand_percent = g.memory.u8(entry + 0x55);
        // Each entry holds up to four bids; 0xffff marks a free bid slot. A bid by the active manager's own club
        // is kept in full so the offer form can be pre-filled.
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
                // Stored term is in months (low 7 bits); converted to whole years, rounded up.
                own.contract_years=uint8_t((int(g.memory.u8(entry+0x34+uint32_t(bid))&0x7f)-1)/12+1);
                for(size_t bonus=0;bonus<own.bonuses.size();++bonus)
                    own.bonuses[bonus]=g.memory.u8(entry+0x38+uint32_t(bid)*7+uint32_t(bonus));
                item.own_bid=own;
            }
        }
        listed_players.at(size_t(player_id)) = true;
        state.items.push_back(std::move(item));
    }

    // View flags of the original screen (position filters, Komplett, loan/purchase mode);
    // default 0x1f0 (all position filters on) when the saved field is not available.
    state.source_view_flags=g.memory.has(::bmh::layout::TransferMarket_ListedPlayers_RuntimeField+uint32_t(manager)*2)?g.memory.u16(::bmh::layout::Savestate_Layout_ExecutableField3+uint32_t(manager)*2):uint16_t(0x1f0);
    state.gradient_text=!g.memory.has(::bmh::layout::TextGradientDisabled)||g.memory.u8(::bmh::layout::TextGradientDisabled)==0;
    state.decorated_background=!g.memory.has(::bmh::layout::PlainBackground)||g.memory.u8(::bmh::layout::PlainBackground)==0;
    state.textured_panes=!g.memory.has(::bmh::layout::SolidPanelBackground)||g.memory.u8(::bmh::layout::SolidPanelBackground)==0;
    state.source_market_mode=g.memory.has(::bmh::layout::GameMode)?g.memory.u8(::bmh::layout::GameMode):uint8_t(0);
    // The search profile and the two suggestions of "Vorschlag" (a national coach has no market, so no profile).
    if(state.runtime_club!=kNoTeam) {
        state.search_profile=search_profile(g,manager);
        for(int whole=0;whole<2;++whole)state.search_suggestion[size_t(whole)]=suggest_search_profile(g,manager,whole!=0);
    }
    if(g.memory.has(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField)&&g.memory.has(::bmh::layout::ClubPlayerDetail_ClubPlayerDetailFromGame_RuntimeField2))state.currency=std::pair<int32_t,int32_t>{int32_t(g.memory.u32(::bmh::layout::CurrencyMultiplier)),int32_t(g.memory.u32(::bmh::layout::CurrencyDivisor))};
    // Roster block shown on the left side of the screen (same data as the club info screen).
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
    // Collect loan slots (up to 12) whose borrowing manager is the active one; these players are
    // shown at the bottom of the roster in addition to the ordinary squad rows.
    std::vector<int> borrowed;
    if(g.memory.has(::bmh::layout::Career_CareerManagersEnd_RuntimeField+23)&&g.memory.has(::bmh::layout::ClubInfo_FormatError_RuntimeField2+11))
        for(int loan=0;loan<12;++loan)if(g.memory.u16(::bmh::layout::Career_CareerManagersEnd_RuntimeField+uint32_t(loan)*2)!=0x0fff&&g.memory.u8(::bmh::layout::ClubInfo_FormatError_RuntimeField2+uint32_t(loan))==manager)borrowed.push_back(loan);
    if(borrowed.size()>range.count)throw FormatError("transfer market: borrowed roster exceeds Source squad range");
    const size_t ordinary_count=range.count-borrowed.size();
    // Ordinary squad rows first; rendered_row counts only non-empty rows and drives the alternating row colours.
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
        if (p.manager_board && p.player_id >= 0) p.statistics = player_club_statistics(g, state.runtime_club, p.player_id, *p.manager_board);
        state.roster.players.push_back(std::move(p));
    }
    // Borrowed players are appended after the ordinary rows and also get a snapshot preview for quotes.
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
        p.statistics = player_club_statistics(g, state.runtime_club, p.player_id, *p.manager_board);
        state.roster.players.push_back(std::move(p));
    }
    // Compact squad list used for selling: only the active manager's own (non-borrowed) players.
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

// Serialises the projection as JSON (items and squad) for diagnostics and tests.
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

// Builds the notice dialog (manager name, portrait, club badge) shown for the result of a poach attempt.
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
    out.badge_fallback=original_badge_fallback(team);
    if(!out.badge_fallback) {
        if(team<0 || size_t(team)*2+2>game.club_heap_78.size())throw FormatError("poach notice: missing badge selector");
        out.badge_sprite=(int(game.club_heap_78[size_t(team)*2])|int(game.club_heap_78[size_t(team)*2+1])<<8)&0x1ff;
    }
    out.compact_dialog_frame=game.memory.has(::bmh::layout::CompactDialogFrame)&&game.memory.u8(::bmh::layout::CompactDialogFrame)!=0;
    return out;
}

// Serialises a request as JSON, including the quote-preview fields and selected players.
std::string transfer_market_request_json(const TransferMarketRequest& r) {
    const char* kind = r.kind == TransferMarketRequestKind::submit_bid ? "submit_bid" :
        r.kind == TransferMarketRequestKind::list_player ? "list_player" :
        r.kind == TransferMarketRequestKind::open_offer ? "open_offer" :
        r.kind == TransferMarketRequestKind::preview_quote ? "preview_quote" :
        r.kind == TransferMarketRequestKind::open_poach ? "open_poach" :
        r.kind == TransferMarketRequestKind::review_offers ? "review_offers" :
        r.kind == TransferMarketRequestKind::store_search_profile ? "store_search_profile" : "none";
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
    out << "],\"search_profile\":[";
    for (size_t i = 0; i < r.search_profile.size(); ++i) { if (i) out << ','; out << unsigned(r.search_profile[i]); }
    out << "],\"search_cancelled\":" << (r.search_cancelled ? "true" : "false")
        << ",\"search_apply\":" << (r.search_apply ? "true" : "false");
    out << ",\"selected_players\":[";
    for(size_t n=0;n<r.selected_players.size();++n){if(n)out<<',';const auto& p=r.selected_players[n];out<<"{\"list_entry\":"<<p.list_entry<<",\"source_slot\":"<<p.source_slot<<",\"player\":"<<p.player<<",\"seller\":"<<p.seller<<",\"squad_row\":"<<p.squad_row<<'}';}
    out << "]}";
    return out.str();
}

// Loads all fonts, images and the executable data needed for drawing and validates the
// background (318x207) and frame (320x240) sizes and the row count.
TransferMarketScreen::TransferMarketScreen(const std::filesystem::path& data, TransferMarketState state)
    : state_(std::move(state)), font_(load_font(asset_path(data / "F", "BMH3.FNT"))),
      exe_(load_le(asset_path(data,"BMH.exe"))),texts_(load_textd(asset_path(data,"TEXTD.BLK"))),sprite_font_(data),
      background_(load_vga(asset_path(data / "KB", "56.VGA"))),
      frame_(load_vga(asset_path(data / "KB", "129.VGA"))),widgets_(load_vga(asset_path(data / "KB", "0.VGA"))),
      atlas_(load_vga(asset_path(data / "KB", "2.VGA"))),flags_(load_vga(asset_path(data / "KB", "24.VGA"))),
      market_widgets_(load_vga(asset_path(data / "KB", "30.VGA"))),
      search_panel_(load_vga(asset_path(data / "KB", "69.VGA"))) {
    if (search_panel_.width != 136 || search_panel_.height != 135)
        throw FormatError("transfer market: invalid search dialog asset");
    if (background_.width != 318 || background_.height != 207 || frame_.width!=320 || frame_.height!=240 || state_.manager_slot < 0 || state_.runtime_club < 0)
        throw FormatError("transfer market: invalid screen state or assets");
    if (state_.items.size() > 40) throw FormatError("transfer market: too many list rows");
    // Keep the saved filter bits (low 11) and force bit 0x800 on.
    view_flags_=uint16_t((state_.source_view_flags & 0x7ff) | 0x800);
    // The atlas image is 314 pixels wide; widen it to 320 so later copies can use fixed coordinates.
    if(atlas_.width==314){Image padded(320,atlas_.height);padded.blit(atlas_,0,0);atlas_=std::move(padded);}
}

// Draws UTF-8 text with the bitmap font; text that would not fit completely is not drawn.
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

// Stores the digits typed so far into the active offer field and clears the input buffer.
void TransferMarketScreen::commit_numeric_field() {
    if (!numeric_input_.empty()) offer_values_.at(size_t(active_field_)) = parse_input(numeric_input_);
    numeric_input_.clear();
}

// Returns the indices of the visible market items: filtered by the position bits (bit 5 + position)
// of view_flags_ and sorted by position, then by descending strength.
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
// Returns (roster index, baseline y) for every roster row to draw. Rows start at y=36 with 7 pixels per
// row, 2 extra pixels where the status bits (0xc000) change; borrowed players are drawn from y=209 upwards.
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
// Draws text with the original font. If `right` is set the text is centred between x and right.
// The colour index may be replaced by a vertical gradient from the executable when gradient text is on.
void TransferMarketScreen::original_text(Image& out,int x,int baseline,std::string_view value,uint8_t color,int right) const {
    const int width=font_.text_width(value);if(right>=0)x+=(right-x)/2-width/2;
    if(x<0||x+width>out.width||baseline<5||baseline>=out.height)return;
    Image mask(std::max(1,width),6);font_.draw(mask,0,0,value,1);
    int gradient=0;while(gradient<32&&exe_.u8(::bmh::layout::Advertising_Text_RuntimeField2+gradient)!=color)++gradient;
    const bool shade=state_.gradient_text&&exe_.u8(::bmh::layout::TextGradientMode)==2;
    for(int y=0;y<6;++y)for(int xx=0;xx<width;++xx)if(mask.at(xx,y))
        out.at(x+xx,baseline-5+y)=shade?exe_.u8(::bmh::layout::Advertising_Text_RuntimeField+gradient*6+y):color;
}
// Draws a button at (x,y) with the given width. Wide buttons (>31) use a 31 pixel left part, repeated
// middle strips and a right end from the widget image; narrow ones are built from corner and edge tiles.
// Active buttons look pressed (the frame is drawn on the other side).
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
// Draws the left roster pane. `full` (Komplett) adds the extended columns; data_b_ switches between
// two data pages (the one with skill bars and statistics, and the one with attributes).
void TransferMarketScreen::draw_roster(Image& out,bool full) const {
    const auto header=texts_[text_id(data_b_?::bmh::layout::ClubInfo_Render_TextPointer4: ::bmh::layout::ClubInfo_Render_TextPointer3)];
    // LineupScreen renders the complete header, then19c1c restores the right
    // pane when Komplett is off. The same clipping is applied to data below.
    original_text(out,0,26,header,0x51);
    for(const auto [row,baseline]:roster_rows()) {
        const auto& p=state_.roster.players[size_t(row)];const auto& a=p.attributes;
        if(p.displayed_slot&0xc000) {
            const int jersey=(p.cards&31)+1;
            // Jersey number: colour depends on the slot status bits (0x1000, borrowed, 0x8000).
            const uint8_t color=p.displayed_slot&0x1000?0xf3:p.borrowed?0x75:p.displayed_slot&0x8000?0x6c:0x68;
            original_text(out,4,baseline,std::to_string(jersey%10),color);
            if(jersey>=10)original_text(out,0,baseline,exe_.cstring(::bmh::layout::ClubInfo_Render_TextPointer),color);
        }
        // Name colour: borrowed, pending offer, or alternating rows; 0x27 highlights the selected or marked player.
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
                const auto& b=*p.manager_board;stats+=padded(p.statistics ? p.statistics->appearances[0] : b[7],3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5)+padded(p.statistics ? p.statistics->appearances[2] : b[9],3)+exe_.cstring(::bmh::layout::ClubInfo_Render_ExecutableField5);
                if(full){Bytes raw(b.begin(),b.end());stats+=padded((p.statistics ? p.statistics->total : le16(raw,0x48)),3)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2)+padded((p.statistics ? p.statistics->goals : le16(raw,0x4a)),3)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField2)+padded((p.statistics ? p.statistics->assists : le16(raw,0x4c)),3)+exe_.cstring(::bmh::layout::ClubInfo_Raw_ExecutableField);}
            }
            original_text(out,165,baseline,stats,0x6c);
        }
    }
}
// Draws the roster plus, unless Komplett is on, the market list (six rows per page), the balance and
// quote preview, the strength bars and the filter buttons, then the bottom buttons.
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
        // Strength bars: shows the averages of the whole team (flag 0x400) or of the active players;
        // the bar height is the share of the overall average clamped to 91..109 percent.
        const auto& average=(view_flags_&0x400)?state_.team_strengths:state_.active_strengths;
        for(int n=0;n<4;++n) {
            const int height=std::clamp(average[4]?average[size_t(n)]*100/average[4]:0,91,109)-90,x=228+n*23;
            copy(out,market_widgets_,0,27,x,195,21,5,0);
            copy(out,market_widgets_,0,5,x,195-height,21,height,0);
            copy(out,market_widgets_,0,0,x,192-height,21,5,0);
            original_text(out,236+n*23,172,padded(average[size_t(n)]),0x6b);
        }
        original_text(out,201,183,padded(average[4]),0x6b);
        // Filter and mode buttons: positions and rectangles come from a table in the executable (6 bytes per entry).
        for(int n=4;n<13;++n)button(out,exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField + n*6),exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField2+n*6),exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField3+n*6),texts_[text_id(::bmh::layout::TransferMarket_Rect_TextPointer5)+size_t(n)],((view_flags_&(1u<<n))!=0)!=(pressed_button_==n));
    }
    button(out,1,215,51,texts_[text_id(data_b_?::bmh::layout::TransferMarket_Rect_TextPointer3: ::bmh::layout::TransferMarket_Rect_TextPointer4)],pressed_button_!=100);
    button(out,55,215,51,texts_[text_id(::bmh::layout::TransferMarket_Rect_TextPointer5)],full!=(pressed_button_==101));
    for(int n=1;n<4;++n)if(!full&&state_.source_market_mode==3)button(out,exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField + n*6),exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField2+n*6),exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField3+n*6),texts_[text_id(::bmh::layout::TransferMarket_Rect_TextPointer5)+size_t(n)],((view_flags_&(1u<<n))!=0)!=(pressed_button_==n));
}
// Formats an amount like the original: converted with the currency multiplier/divisor,
// left-padded to 9 characters and followed by the currency suffix.
std::string TransferMarketScreen::original_money(int32_t value) const {
    const int32_t numerator=state_.currency?state_.currency->first:int32_t(exe_.u32(::bmh::layout::CurrencyMultiplier));
    const int32_t denominator=state_.currency?state_.currency->second:int32_t(exe_.u32(::bmh::layout::CurrencyDivisor));
    if(denominator==0)throw FormatError("transfer market: invalid Original currency denominator");
    const auto converted=int64_t(value)*numerator/denominator;
    std::string result=money(converted);
    if(result.size()<9)result.insert(0,9-result.size(),'%');
    return result+exe_.cstring(::bmh::layout::CurrencySuffixText);
}
// Renders the complete 320x240 screen: frame (or plain bars), title, club name, the overview and a
// feedback line that replaces the club name line when set (shortened to fit 310 pixels).
Image TransferMarketScreen::render() const {
    Image out=state_.decorated_background?frame_:Image(320,240);
    if(!state_.decorated_background){rect(out,0,4,320,13,0x44);rect(out,1,5,318,11,0x39);rect(out,0,228,320,10,0x44);rect(out,1,229,318,8,0x39);}
    const auto title=original_to_utf8(texts_[text_id(::bmh::layout::TransferMarket_Render_TextPointer)]);
    sprite_font_.draw(out,SpriteFont::centered_title_x(sprite_font_.text_width(title)),13,title);
    sprite_font_.draw(out,5,235,original_to_utf8(state_.club_name));
    draw_market_overview(out);
    if (search_open_) draw_search(out);
    if (!feedback_.empty()) {
        rect(out, 1, 227, 318, 12, 0x39);
        auto line = feedback_;
        while (!line.empty() && sprite_font_.text_width(line) > 310) {
            size_t at = line.size() - 1;
            while (at > 0 && (uint8_t(line[at]) & 0xc0) == 0x80) --at;
            line.resize(at);
        }
        sprite_font_.draw(out, 5, 235, line);
    }
    return out;
}

// Validates the offer form (fee and salary non-zero, 1..4 years, bonuses 0..100, not an own player)
// and creates a submit_bid request; the screen then waits for the session's acknowledgement.
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

// Creates a request to list the selected squad player (sell or loan). If the player is already listed,
// opens the offer review instead.
TransferMarketRequest TransferMarketScreen::make_listing(bool loan) {
    if (pending_ || !selected_squad_) return {};
    const auto& item = state_.squad.at(size_t(*selected_squad_));
    if (item.already_listed) {
        const auto listing = std::find_if(state_.items.begin(), state_.items.end(),
            [&](const auto& row) { return row.player == item.player && row.own_listing; });
        if (listing != state_.items.end()) return make_review(*listing);
        feedback_ = "Spieler steht bereits auf der Liste"; return {};
    }
    TransferMarketRequest request;
    request.kind = TransferMarketRequestKind::list_player;request.offer_is_loan=loan;
    request.manager_slot = state_.manager_slot; request.runtime_club = state_.runtime_club; request.revision = state_.revision;
    request.squad_row = item.row; request.expected_player = item.player;
    request.selected_players.push_back({-1,state_.roster.squad_start_offset+item.row,item.player,state_.runtime_club,item.row});
    pending_ = request; feedback_.clear();
    return request;
}

// Creates a quote-preview request for the selected market item (purchase), loan snapshot or squad player.
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
// Creates a request to review the bids on one of the player's own listings; refuses if there are none.
TransferMarketRequest TransferMarketScreen::make_review(const TransferMarketItem& item) {
    if (pending_) return {};
    if (item.offers == 0) { feedback_ = "Noch keine Angebote für diesen Spieler."; return {}; }
    TransferMarketRequest request;
    request.kind = TransferMarketRequestKind::review_offers;
    request.manager_slot = state_.manager_slot; request.runtime_club = state_.runtime_club;
    request.revision = state_.revision; request.bid.manager = state_.manager_slot;
    request.bid.list_entry = item.list_entry; request.bid.player = item.player;
    request.bid.source_slot = item.source_slot;
    pending_ = request; feedback_.clear(); return request;
}
// Creates a request to open the offer screen for the selected market item; own listings go to the review instead.
TransferMarketRequest TransferMarketScreen::make_open_offer(bool loan) {
    if(pending_||!selected_item_)return {};
    const auto& item=state_.items.at(size_t(*selected_item_));
    if(item.own_listing)return make_review(item);
    TransferMarketRequest request;request.kind=TransferMarketRequestKind::open_offer;request.offer_is_loan=loan;
    request.manager_slot=state_.manager_slot;request.runtime_club=state_.runtime_club;request.revision=state_.revision;
    if(item.own_bid)request.bid=*item.own_bid;
    request.bid.manager=state_.manager_slot;request.bid.list_entry=item.list_entry;request.bid.player=item.player;
    request.bid.source_slot=item.source_slot;
    request.selected_players.push_back({item.list_entry,item.source_slot,item.player,item.seller,-1});
    pending_=request;return request;
}
// Selection by position over the market rows (x 201..297, y 25..115, 15 px per row) or the roster
// names (x 9..60). A new selection requests a quote preview unless Komplett is on.
TransferMarketRequest TransferMarketScreen::move(int x,int y) {
    if(pending_||close_requested_||search_open_)return {};
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
// Handles a mouse click. Right click on the market pane turns the page, elsewhere closes the screen.
// Left click handles the Data/Komplett buttons, poach and filter buttons, marking of rows, and the action bar
// (y 202..212; the right part x>=254 means loan).
TransferMarketRequest TransferMarketScreen::click(int x,int y,bool right) {
    pressed_button_=-1;   // the release ends the held look of a button
    if(pending_||close_requested_)return {};
    if(search_open_)return click_search(x,y,right);
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
        // The two buttons before Abwerben (Source entries 1 and 2): Suchen opens the search dialog with the stored
        // profile, Vorschlag with the profile the squad asks for (Source sub_19409; the dialog is local UI state).
        for(int search_button=1;search_button<=2&&state_.source_market_mode==3;++search_button) {
            const int sx=exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField + search_button*6),sy=exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField2+search_button*6),sw=exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField3+search_button*6);
            if(x<sx||x>sx+sw||y<sy||y>sy+10)continue;
            open_search(search_button==1?state_.search_profile:state_.search_suggestion[(view_flags_&0x400)?1:0]);
            return {};
        }
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
                marked_squad_.clear();
                const int item=order[index];
                marked_items_.erase(std::remove_if(marked_items_.begin(),marked_items_.end(),[&](int n){return n!=item;}),marked_items_.end());
                auto marked=std::find(marked_items_.begin(),marked_items_.end(),item);
                if(marked==marked_items_.end())marked_items_.push_back(item);else marked_items_.erase(marked);
                return move(x,y);
            }
        }
        if(y>=202&&y<=212&&x>=185&&x<=318) {
            if(!marked_items_.empty()){selected_item_=marked_items_.back();selected_squad_.reset();selected_snapshot_.reset();return make_open_offer(x>=254);}
            if(!marked_squad_.empty()){selected_squad_=marked_squad_.back();selected_item_.reset();return make_listing(x>=254);}
            view_flags_^=0x1800;preview_.reset();return {};
        }
    }
    if(x>=9&&x<=60)for(const auto [row,baseline]:roster_rows())if(y>=baseline-7&&y<baseline+2) {
        const auto& player=state_.roster.players.at(size_t(row));
        if(player.borrowed)return {}; // Borrowed-player business remains Source-owned elsewhere.
        const auto found=std::find_if(state_.squad.begin(),state_.squad.end(),[&](const auto& item){return item.row==player.physical_row;});
        if(found==state_.squad.end())return {};
        const int index=int(found-state_.squad.begin());
        marked_items_.clear();
        marked_squad_.erase(std::remove_if(marked_squad_.begin(),marked_squad_.end(),[&](int n){return n!=index;}),marked_squad_.end());
        auto marked=std::find(marked_squad_.begin(),marked_squad_.end(),index);
        if(marked==marked_squad_.end())marked_squad_.push_back(index);else marked_squad_.erase(marked);
        return move(x,y);
    }
    return {};
}
// --- search dialog (Source sub_19409) --------------------------------------------------------------------------
// Opens the dialog on a copy of `profile` (without the "off" bit of byte 0, as the Source dialog starts).
void TransferMarketScreen::open_search(SearchProfile profile) {
    profile[0] &= 0x3F;
    search_edit_ = profile;
    search_open_ = true;
}

// Draws the dialog: the 136x135 panel at (184,20), the seven bars (a dark stripe per row and the selected cells or the
// range in the highlight colour), the price estimate of the profile and the confirm button.
void TransferMarketScreen::draw_search(Image& out) const {
    copy(out, search_panel_, 0, 0, 184, 20, 136, 135);
    for (int row = 0; row < 7; ++row) {
        const uint32_t at = ::bmh::layout::TransferMarket_SearchRows_RuntimeField + uint32_t(row) * 5;
        const int y = exe_.u8(at), step = exe_.u8(at + 1), cells = exe_.u8(at + 2), slider = exe_.u8(at + 3), offset = exe_.u8(at + 4);
        rect(out, 217, y, 100, 2, 0x42);   // Source rectangle (217,y)-(316,y+1)
        if (slider) {
            const int low = 100 * search_edit_.at(size_t(offset)) / cells, high = 100 * search_edit_.at(size_t(offset) + 1) / cells;
            rect(out, 217 + low, y, high - low + 1, 2, 0xF7);
        } else {
            for (int cell = 0, x = 217; cell < cells; ++cell, x += step)
                if (search_edit_.at(size_t(offset) + size_t(cell))) rect(out, x + 3, y, step - 5, 2, 0xF7);
        }
    }
    // Price estimate: five times the (inverted) age midpoint plus five times the strength midpoint, limited to 12..620.
    int value = 5 * (31 - (search_edit_[4] + search_edit_[5]) / 2) + 5 * ((search_edit_[7] + search_edit_[6]) / 2);
    value = std::clamp(value, 12, 620);
    value = value * (value >> 2) * ((value >> 3) + 100) / 10;
    auto price = original_money(10000 * (value / 10000));
    price.erase(0, price.find_first_not_of('%'));
    original_text(out, 216, 148, price, 0x7A);
    button(out, 277, 140, 40, texts_[text_id(::bmh::layout::TransferMarket_SearchButton_TextPointer)], pressed_button_ == 200);
}

// A click inside the dialog: the confirm button closes it and applies the profile, the right button closes it and
// switches the search off, a click on a bar row edits that row (Source: a switch toggles its cell; on a slider the
// click moves the nearer end of the range, and the two ends never become equal).
TransferMarketRequest TransferMarketScreen::click_search(int x, int y, bool right) {
    if (right) return close_search(true);
    if (x >= 277 && x <= 317 && y >= 140 && y <= 150) return close_search(false);
    if (x < 216 || x > 317) return {};
    for (int row = 0; row < 7; ++row) {
        const uint32_t at = ::bmh::layout::TransferMarket_SearchRows_RuntimeField + uint32_t(row) * 5;
        const int row_y = exe_.u8(at), step = exe_.u8(at + 1), cells = exe_.u8(at + 2), slider = exe_.u8(at + 3), offset = exe_.u8(at + 4);
        if (y < row_y - 2 || y > row_y + 3) continue;
        int value = (x - 217) / step;
        if (slider) {
            auto& low = search_edit_.at(size_t(offset));
            auto& high = search_edit_.at(size_t(offset) + 1);
            if ((int(low) + int(high)) / 2 < value && value != 0) {
                if (value > cells - 1) value = cells - 1;
                high = uint8_t(value);
            } else {
                low = uint8_t(std::max(0, value));
            }
            if (low == high) { if (low) --low; else ++high; }
        } else if (value >= 0 && value < cells) {   // (the Source also toggles the byte after the row for a click on its last pixel column)
            search_edit_.at(size_t(offset) + size_t(value)) ^= 1;
        }
    }
    return {};
}

// Leaves the dialog and posts the profile to the session. Source sets view-flag bit 1 (the Suchen button) to whether
// the closed profile is an active search.
TransferMarketRequest TransferMarketScreen::close_search(bool cancelled) {
    search_open_ = false;
    view_flags_ = uint16_t((view_flags_ & ~2u) | (search_profile_active(search_edit_, cancelled) ? 2u : 0u));
    TransferMarketRequest request;
    request.kind = TransferMarketRequestKind::store_search_profile;
    request.manager_slot = state_.manager_slot; request.runtime_club = state_.runtime_club; request.revision = state_.revision;
    request.search_profile = search_edit_; request.search_cancelled = cancelled; request.search_apply = !cancelled;
    pending_ = request; feedback_.clear();
    return request;
}

// The button under the pointer (same hit areas as click()): 100 Data, 101 Komplett, 1..3 the buttons before Abwerben (mode III),
// 4..10 the position and page buttons, 11/12 the action bar (purchase / loan), 200 the confirm button of the search dialog.
int TransferMarketScreen::button_at(int x, int y) const {
    if (search_open_) return x >= 277 && x <= 317 && y >= 140 && y <= 150 ? 200 : -1;
    if (x >= 1 && x <= 52 && y >= 215 && y <= 225) return 100;
    if (x >= 55 && x <= 106 && y >= 215 && y <= 225) return 101;
    if (view_flags_ & 1) return -1;
    for (int n = 1; n <= 10; ++n) {
        if (n <= 3 && state_.source_market_mode != 3) continue;
        const int bx = exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField + n * 6);
        const int by = exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField2 + n * 6);
        const int width = exe_.u16(::bmh::layout::TransferMarket_Click_RuntimeField3 + n * 6);
        if (x >= bx && x <= bx + width && y >= by && y <= by + 10) return n;
    }
    if (y >= 202 && y <= 212 && x >= 185 && x <= 318) return x >= 254 ? 12 : 11;
    return -1;
}

// Mouse DOWN: the button under the pointer is drawn pressed until the release (the action itself happens in click()).
bool TransferMarketScreen::press(int x, int y, bool right) {
    if (pending_ || close_requested_) return false;
    const int id = right ? -1 : button_at(x, y);
    const bool changed = id != pressed_button_;
    pressed_button_ = id;
    return changed;
}

// Keyboard shortcuts: Esc closes, B or Enter opens an offer for the selected market item,
// V or Enter lists the selected squad player.
TransferMarketRequest TransferMarketScreen::key(std::string_view key) {
    if(pending_||close_requested_)return {};
    if(search_open_) {   // the dialog only knows the mouse; Esc counts as the right button
        if(key=="ESC"||key=="ESCAPE")return close_search(true);
        return {};
    }
    if(key=="ESC"||key=="ESCAPE"){close_requested_=true;return {};}
    if(key=="B"&&selected_item_)return make_open_offer();
    if(key=="V"&&selected_squad_)return make_listing();
    if(key=="ENTER") {
        if(selected_item_)return make_open_offer();
        if(selected_squad_)return make_listing();
    }
    return {};
}

// Accepts typed digits (at most 10) while the offer form is shown; returns whether the input was used.
bool TransferMarketScreen::text(std::string_view value) {
    if (pending_ || close_requested_ || view_ != View::offer) return false;
    if (value.empty() || numeric_input_.size() + value.size() > 10 ||
        !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    numeric_input_.append(value); return true;
}

// Session acknowledgement of a request: replaces the state with a newer revision of the same manager,
// clears the pending request and all selections and shows the notice.
void TransferMarketScreen::acknowledge(TransferMarketState updated, std::string notice) {
    if (!pending_ || updated.manager_slot != state_.manager_slot || updated.runtime_club != state_.runtime_club ||
        updated.revision <= state_.revision) throw FormatError("transfer market: acknowledgement requires same manager and newer revision");
    state_ = std::move(updated); pending_.reset(); selected_item_.reset(); selected_squad_.reset();selected_snapshot_.reset();preview_.reset();
    view_ = View::market; numeric_input_.clear();marked_items_.clear();marked_squad_.clear();
    feedback_ = std::move(notice);
}
// Session acknowledgement when the request changed nothing: only releases the pending request.
void TransferMarketScreen::acknowledge_unchanged() {
    if (!pending_) throw FormatError("transfer market: no pending request to acknowledge");
    pending_.reset(); numeric_input_.clear(); feedback_.clear();
}
// Session acknowledgement of a quote preview: checks that the quote matches the pending request exactly
// (manager, club, slot, player, seller, kind), re-finds the selection in the new state and stores the quote.
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
// Takes a fresh state after the offer screen has been left; clears selections and marks.
void TransferMarketScreen::refresh_after_offer(TransferMarketState updated) {
    if (pending_ || updated.manager_slot != state_.manager_slot || updated.runtime_club != state_.runtime_club ||
        updated.revision < state_.revision)
        throw FormatError("transfer market: invalid editor-return projection");
    state_ = std::move(updated);
    selected_item_.reset(); selected_squad_.reset();selected_snapshot_.reset(); preview_.reset();
    marked_items_.clear(); marked_squad_.clear();
    feedback_.clear();
}
// Session rejection of the pending request: releases it and shows the reason as feedback.
void TransferMarketScreen::reject_action(std::string reason) {
    if (!pending_ || reason.empty()) throw FormatError("transfer market: rejection requires pending request/reason");
    pending_.reset(); feedback_ = std::move(reason); numeric_input_.clear();
}
// Serialises the screen state (view, selections, pending request, preview, visible rows) as JSON for tests.
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
        << ",\"pressed_button\":" << pressed_button_
        << ",\"search_open\":" << (search_open_ ? "true" : "false") << ",\"search_edit\":[";
    for (size_t i = 0; i < search_edit_.size(); ++i) out << (i ? "," : "") << unsigned(search_edit_[i]);
    out << "],\"preview\":";
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
