// SPDX-License-Identifier: GPL-3.0-only
// The football almanac (docs/new_engine/almanach.md, M8) on synthetic data: the binary
// season round trip, damaged input, the calendar dates and new player identities.
#include <cstdio>
#include <string>

#include "formats/binary.hpp"
#include "modern/game/almanac.hpp"
#include "modern/game/modern_state.hpp"

using namespace bmh;
using namespace bmh::modern;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

AlmanacSeason sample() {
    AlmanacSeason s;
    s.season = 3, s.start_year = 2029, s.display_start_year = 2026;
    s.leagues.push_back({0, "comp:bundesliga", "Bundesliga"});
    s.referees.push_back({});
    const uint16_t home = s.club({"club:sued", "FC Südstadt"});
    const uint16_t away = s.club({"club:nord", "SV Nordstadt"});
    const uint16_t a = s.player({"player:1", "Anton", "Beispiel", "nation:de"});
    const uint16_t b = s.player({"player:gen-7", "", "Größer", "nation:de"});
    AlmanacMatch m;
    m.date = 400, m.type = 2, m.stage = 5, m.round = 2, m.home = home, m.away = away;
    m.home_manager = s.manager({0, "Testmanager"});
    m.home_goals = 1, m.away_goals = 1, m.home_shootout = 5, m.away_shootout = 3, m.decided = 2;
    m.referee = s.referee("Schiri A");
    m.attendance = 75000, m.temperature = -3, m.rain = 6;
    m.lineup[0] = {a, b};
    m.events.push_back({AlmanacEventKind::Goal, 12, 0, 3, a, b});
    m.events.push_back({AlmanacEventKind::SecondYellow, 77, 1, 0, b, kAlmanacNone});
    m.events.push_back({AlmanacEventKind::Substitution, 80, 0, 0, b, a});
    m.events.push_back({AlmanacEventKind::ShootoutKick, 121, 1, 1, a, kAlmanacNone});
    s.matches.push_back(m);
    AlmanacTable t;
    t.league = 0;
    t.rows.push_back({home, 1, 34, 25, 5, 4, 80, 30, 77});
    t.rows.push_back({away, 2, 34, 20, 6, 8, 70, 40, -2});
    s.tables.push_back(t);
    s.winners.push_back({0, 0, 0xff, 0, 300, home, away});
    s.winners.push_back({12, 0xff, 4, 1, 310, away, 0xffff});
    return s;
}
}  // namespace

int main() {
    const AlmanacSeason s = sample();
    const std::string bytes = encode_almanac_season(s);
    const AlmanacSeason back = decode_almanac_season(bytes);
    check(back == s, "season round trip");
    check(encode_almanac_season(back) == bytes, "stable encoding");

    AlmanacSeason grown = back;  // the lookups survive decoding: no duplicate entries
    check(grown.club({"club:sued", "FC Südstadt"}) == 0 && grown.clubs.size() == 2, "club lookup after decode");
    check(grown.referee("Schiri A") == 1 && grown.referees.size() == 2, "referee lookup after decode");

    bool refused = false;
    try { decode_almanac_season(bytes.substr(0, bytes.size() - 3)); } catch (const FormatError&) { refused = true; }
    check(refused, "truncated season refused");
    refused = false;
    try { decode_almanac_season(bytes + "x"); } catch (const FormatError&) { refused = true; }
    check(refused, "trailing bytes refused");
    refused = false;
    try { decode_almanac_season("OBAL\x09\x00"); } catch (const FormatError&) { refused = true; }
    check(refused, "unknown version refused");

    // Day 0 is 1 July of the display start year; the calendar's first leap year comes after one year.
    const AlmanacDate d0 = almanac_date(0, 2026), d1 = almanac_date(183, 2026), d2 = almanac_date(365, 2026);
    check(d0.year == 2026 && d0.month == 7 && d0.day == 1, "date of day 0");
    check(d1.year == 2026 && d1.month == 12 && d1.day == 31, "date of day 183");
    check(d2.year == 2027 && d2.month == 7 && d2.day == 1, "date of day 365");

    ModernState g;
    g.players.resize(5);
    new_player_identity(g, 3);
    new_player_identity(g, 3);
    check(g.world_player_id.size() == 5 && g.world_player_id[3] == "player:gen-2" && g.almanac.generated_players == 2,
          "new player identities");

    if (failures == 0) std::printf("modern almanac tests passed\n");
    return failures == 0 ? 0 : 1;
}
