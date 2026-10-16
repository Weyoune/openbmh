// SPDX-License-Identifier: GPL-3.0-only
#include "formats/executable_layout.hpp"
#include "ui/sportschau.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "formats/binary.hpp"
#include "formats/font.hpp"
#include "formats/le.hpp"
#include "formats/rule_base.hpp"

using namespace bmh;
using namespace bmh::ui;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

uint8_t pixel(const Image& image, int x, int y) {
    return image.pixels.at(size_t(y) * size_t(image.width) + size_t(x));
}

Image load_kb(const std::filesystem::path& data, const char* file) {
    return load_vga(data / "KB" / file);
}

void report_tests(const std::filesystem::path& data) {
    using namespace bmh::ui;
    const Image room = load_kb(data, "57.VGA");
    const Image panels = load_kb(data, "58.VGA");
    const Image art = load_kb(data, "14.VGA");
    const Image default_badge = load_kb(data, "0.VGA");
    const Image badge0 = load_vga(data / "W" / "0.VGA");
    const Image badge1 = load_vga(data / "W" / "1.VGA");
    const LeImage exe = load_le(data / "BMH.exe");
    SportschauFixtureView fixture;
    fixture.group = 1;
    fixture.fixture = 0;
    fixture.home_team = 0;
    fixture.away_team = 1;
    fixture.home_badge_word = 0;
    fixture.away_badge_word = 8;
    fixture.home_name = {'A'};
    fixture.away_name = {'B'};
    fixture.home_short_name = {'A'};
    fixture.away_short_name = {'B'};
    fixture.home_short_name_available = true;
    fixture.away_short_name_available = true;
    fixture.source_footer_variant = 0;
    fixture.home_name_requires_font_fit = false;
    fixture.away_name_requires_font_fit = false;
    fixture.score_known = true;
    fixture.home_goals = 9; // The visible score must be replayed from the shown log prefix.
    fixture.source_played_minute = 30;
    SportschauReportContext options;
    options.solid_panel_background = true;
    options.gradient_text = false;
    SportschauMatchReport report(data, fixture, options);
    const Image empty = report.render_report(0);
    check(empty.width == 320 && empty.height == 240, "report is a full logical VGA frame");
    check(pixel(empty, 6, 32) == 0x46 && pixel(empty, 313, 56) == 0x40 && pixel(empty, 7, 33) == 0x42,
          "three report panels use Source bounds, fill and border colors");
    check(pixel(empty, 7, 184) == pixel(art, 0, 0), "normal report footer begins at Source origin");
    check(pixel(empty, 63, 8) == pixel(badge0, 1, 2) && pixel(empty, 219, 8) == pixel(badge1, 1, 2),
          "crest words select Source W atlas and 38-pixel cell");
    check(pixel(empty, 300, 20) == pixel(room, 300, 20), "report retains the Source room outside overlays");
    check(pixel(empty, 125, 201) == pixel(art, 63, 41),
          "empty event logs still advance progress to the Source played-minute byte");

    SportschauFixtureView unavailable_abbreviation = fixture;
    unavailable_abbreviation.home_short_name.clear();
    unavailable_abbreviation.home_short_name_available = false;
    SportschauFixtureView empty_abbreviation = fixture;
    empty_abbreviation.home_short_name.clear();
    SportschauMatchReport unavailable_abbreviation_report(data, unavailable_abbreviation, options);
    SportschauMatchReport empty_abbreviation_report(data, empty_abbreviation, options);
    check(unavailable_abbreviation_report.render_report(0).pixels ==
              empty_abbreviation_report.render_report(0).pixels,
          "unavailable Original club abbreviation omits that label without aborting the report");

    options.solid_panel_background = false;
    SportschauMatchReport textured(data, fixture, options);
    const Image textured_frame = textured.render_report(0);
    check(pixel(textured_frame, 7, 33) == pixel(panels, 6, 13),
          "textured panels use the Source inset KB58 crop");
    options.solid_panel_background = true;

    fixture.source_footer_variant = 1;
    SportschauMatchReport extra_time(data, fixture, options);
    const Image extra_footer = extra_time.render_report(0);
    const int extra_width = 245 + int(int8_t(exe.u8(::bmh::layout::FixtureSportschauOutdoorFooterBias)));
    check(extra_width == 306 && pixel(extra_footer, 300, 185) == pixel(art, 293, 1),
          "extra-time report uses the signed Source footer width");
    fixture.source_footer_variant = 2;
    SportschauMatchReport indoor(data, fixture, options);
    const Image indoor_footer = indoor.render_report(0);
    check(245 + int(int8_t(exe.u8(::bmh::layout::FixtureSportschauIndoorFooterBias))) == 145 && pixel(indoor_footer, 300, 185) == pixel(room, 300, 185),
          "indoor footer ends at its Source-specific width");
    fixture.source_footer_variant = 0;

    SportschauFixtureView foreign = fixture;
    foreign.home_team = 168;
    foreign.away_team = 169;
    foreign.home_badge_word = foreign.away_badge_word = 0xffff;
    foreign.home_name = {'N','o','r','t','h',' ', 'C','o','u','n','t','r','y'};
    foreign.away_name = {'S','o','u','t','h',' ', 'C','o','u','n','t','r','y'};
    foreign.home_short_name.clear();
    foreign.away_short_name.clear();
    foreign.home_name_requires_longest_word = true;
    foreign.away_name_requires_longest_word = true;
    SportschauMatchReport foreign_report(data, foreign, options);
    const Image foreign_frame = foreign_report.render_report(0);
    check(pixel(foreign_frame, 63, 8) == pixel(default_badge, 9, 2),
          "foreign fixtures use Source legacy routine's KB0 fallback badge");

    SportschauLogEvent goal;
    goal.group = fixture.group;
    goal.fixture = fixture.fixture;
    goal.event_ordinal = 0;
    goal.source_code_player_word = 4;
    goal.source_team_value_word = uint16_t((uint16_t(fixture.home_team) << 7) | 12);
    goal.source_code = 0;
    goal.player_id = 4;
    goal.team_id = fixture.home_team;
    goal.source_value = 12;
    goal.player_name = {'A'};
    goal.consumes_following_pair = true;
    fixture.source_played_minute = 12;
    fixture.log_events.push_back(goal);
    SportschauMatchReport with_goal(data, fixture, options);
    const Image goal_frame = with_goal.render_report(1);
    check(pixel(goal_frame, 89, 201) == pixel(art, 27, 41),
          "Source timeline progress uses the displayed log minute, not final score metadata");
    check(goal_frame.pixels != empty.pixels, "a visible log prefix changes only the report projection");
    const Image partial_goal = with_goal.render_report(0);
    const Image chance_goal = with_goal.render_report(SportschauReportStep{1, 11, false, true});
    bool completed_score_differs = false;
    for (int y = 33; y < 45; ++y) for (int x = 135; x < 185; ++x) {
        check(pixel(chance_goal, x, y) == pixel(partial_goal, x, y),
              "selected chance preview keeps the pre-event header score until Source finalization");
        completed_score_differs = completed_score_differs || pixel(goal_frame, x, y) != pixel(chance_goal, x, y);
    }
    check(completed_score_differs, "completed goal score is revealed after the transient chance frame");
    SportschauFixtureView transient_miss = fixture;
    transient_miss.log_events[0].source_code = 1;
    transient_miss.log_events[0].source_code_player_word = 0x1004;
    const SportschauMatchReport miss_preview(data, transient_miss, options);
    const auto preview = miss_preview.render_report(SportschauReportStep{1, 11, false, true});
    const auto restored = miss_preview.render_report(1);
    bool miss_row_differs = false;
    for (int y = 60; y < 67; ++y) for (int x = 8; x < 148; ++x)
        miss_row_differs = miss_row_differs || pixel(preview, x, y) != pixel(restored, x, y);
    check(miss_row_differs, "selected miss row is temporarily visible, then restored out of the report");
    SportschauFixtureView early_minute = fixture;
    early_minute.source_played_minute = 1;
    early_minute.log_events.clear();
    SportschauMatchReport early_report(data, early_minute, options);
    SportschauFixtureView zero_minute = early_minute;
    zero_minute.source_played_minute = 0;
    SportschauMatchReport zero_minute_report(data, zero_minute, options);
    check(pixel(partial_goal, 62, 201) == pixel(zero_minute_report.render_report(0), 62, 201) &&
          pixel(early_report.render_report(0), 62, 201) == pixel(art, 0, 41),
          "partial prefix and empty zero-minute reports preserve Source progress-loop boundaries");

    SportschauFixtureView progressive = fixture;
    SportschauLogEvent later;
    later.group = progressive.group;
    later.fixture = progressive.fixture;
    later.source_word_index = 2;
    later.source_code_player_word = 0x2fff;
    later.source_team_value_word = uint16_t((uint16_t(progressive.away_team) << 7) | 15);
    later.source_code = 2;
    later.player_id = 0x0fff;
    later.team_id = progressive.away_team;
    later.source_value = 15;
    progressive.log_events.push_back(later);
    progressive.source_played_minute = 30;
    SportschauMatchReport progressive_report(data, progressive, options);
    const Image between_events = progressive_report.render_report(1);
    check(pixel(between_events, 62 + 25, 201) == pixel(art, 25, 41) &&
          pixel(between_events, 62 + 26, 201) != pixel(art, 26, 41),
          "between-event progress stops at the minute immediately before the next loop cursor");

    SportschauFixtureView shootout = fixture;
    SportschauLogEvent shootout_row;
    shootout_row.group = shootout.group;
    shootout_row.fixture = shootout.fixture;
    shootout_row.source_code_player_word = 0x2fff;
    shootout_row.source_team_value_word = uint16_t((uint16_t(shootout.home_team) << 7) | 121);
    shootout_row.source_code = 2;
    shootout_row.player_id = 0x0fff;
    shootout_row.team_id = shootout.home_team;
    shootout_row.source_value = 121;
    shootout.log_events = {shootout_row};
    shootout.source_played_minute = 90;
    SportschauMatchReport shootout_report(data, shootout, options);
    const Image shootout_frame = shootout_report.render_report(1);
    check(pixel(shootout_frame, 305, 201) == pixel(art, 243, 41) &&
          pixel(shootout_frame, 306, 201) == pixel(room, 306, 201),
          "full Source progress retains the final event minute when descriptor end is earlier");

    Font small_font = load_font(data / "F" / "BMH3.FNT");
    Image expected_running = partial_goal;
    small_font.draw(expected_running, 8, 66 - small_font.baseline, "1:0", 0x5f);
    for (int y = 60; y < 67; ++y) for (int x = 8; x < 22; ++x)
        check(pixel(goal_frame, x, y) == pixel(expected_running, x, y),
              "goal rows include the Source running score at the fixture-side origin");

    SportschauFixtureView long_heading = fixture;
    Font heading_font = load_font(data / "F" / "BMH2.FNT");
    long_heading.home_name.assign(1, 'A');
    while (heading_font.text_width(std::string(long_heading.home_name.begin(), long_heading.home_name.end())) <= 156)
        long_heading.home_name.push_back('A');
    const std::string long_heading_text(long_heading.home_name.begin(), long_heading.home_name.end());
    check(heading_font.text_width(long_heading_text) < 312,
          "controlled long heading falls outside its column but inside the viewport");
    SportschauMatchReport long_heading_report(data, long_heading, options);
    SportschauFixtureView blank_heading = fixture;
    blank_heading.home_name = {' '};
    SportschauMatchReport blank_heading_report(data, blank_heading, options);
    const Image rejected_heading = long_heading_report.render_report(0);
    const Image blank_heading_frame = blank_heading_report.render_report(0);
    check(rejected_heading.pixels == blank_heading_frame.pixels,
          "Source rejects the whole off-viewport-centered string rather than clipping it to a column");

    SportschauReportContext gradient_options = options;
    gradient_options.gradient_text = true;
    SportschauMatchReport gradient_report(data, fixture, gradient_options);
    const Image gradient_frame = gradient_report.render_report(0);
    for (int y = 36; y < 55; ++y) for (int x = 0; x < 320; ++x)
        check(pixel(gradient_frame, x, y) == pixel(empty, x, y),
              "Source text gradient applies to FNT3 only, leaving FNT2 headings and score unchanged");
    bool small_gradient_differs = false;
    for (int y = 190; y < 201; ++y) for (int x = 10; x < 59; ++x)
        small_gradient_differs |= pixel(gradient_frame, x, y) != pixel(empty, x, y);
    check(small_gradient_differs, "Source FNT3 club abbreviations use the enabled palette gradient");

    SportschauFixtureView miss_then_event = fixture;
    SportschauLogEvent miss = goal;
    miss.source_code_player_word = 0x1004;
    miss.source_code = 1;
    miss.source_word_index = 0;
    miss.consumes_following_pair = true;
    miss.has_following_pair = true;
    miss.following_code_player_word = 0x2fff;
    miss.source_value = 13;
    miss.source_team_value_word = uint16_t((uint16_t(fixture.home_team) << 7) | 13);
    SportschauLogEvent event_row;
    event_row.group = fixture.group;
    event_row.fixture = fixture.fixture;
    event_row.event_ordinal = 1;
    event_row.source_word_index = 2;
    event_row.source_code_player_word = 0x2fff;
    event_row.source_team_value_word = uint16_t((uint16_t(fixture.home_team) << 7) | 14);
    event_row.source_code = 2;
    event_row.player_id = 0x0fff;
    event_row.team_id = fixture.home_team;
    event_row.source_value = 14;
    SportschauLogEvent next_event = event_row;
    next_event.event_ordinal = 2;
    next_event.source_word_index = 4;
    next_event.source_team_value_word = uint16_t((uint16_t(fixture.home_team) << 7) | 15);
    next_event.source_value = 15;
    miss_then_event.log_events = {miss, event_row, next_event};
    SportschauMatchReport miss_report(data, miss_then_event, options);
    const Image miss_frame = miss_report.render_report(3);
    const int event_icon_x = 250 + exe.u8(::bmh::layout::Sportschau_FormatError_RuntimeField + 2);
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) {
        const uint8_t source = pixel(art, event_icon_x + x, 41 + y);
        check(pixel(miss_frame, 10 + x, 61 + y) ==
                  (source == 0 ? pixel(empty, 10 + x, 61 + y) : source),
              "Source miss companion does not consume an extra report row");
    }

    SportschauFixtureView card = fixture;
    SportschauLogEvent card_event;
    card_event.group = card.group;
    card_event.fixture = card.fixture;
    card_event.source_word_index = 0;
    card_event.source_code_player_word = 0x2fff;
    card_event.source_team_value_word = uint16_t(uint16_t(card.home_team) << 7);
    card_event.source_code = 2;
    card_event.player_id = 0x0fff;
    card_event.team_id = card.home_team;
    card_event.has_following_word = false;
    card.log_events = {card_event};
    card.source_played_minute = 0;
    SportschauMatchReport card_report(data, card, options);
    const Image card_frame = card_report.render_report(1);
    const int card_icon_x = 250 + exe.u8(::bmh::layout::Sportschau_FormatError_RuntimeField + 2);
    bool saw_zero = false, saw_ink = false;
    for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) {
        const uint8_t source = pixel(art, card_icon_x + x, 41 + y);
        saw_zero |= source == 0;
        saw_ink |= source != 0;
        check(pixel(card_frame, 10 + x, 61 + y) ==
                  (source == 0 ? pixel(empty, 10 + x, 61 + y) : source),
              "Source event icon uses transparent zero and preserves the report underlay");
    }
    check(saw_zero && saw_ink, "literal event icon fixture covers both transparent and drawn pixels");

    // legacy routine uses local_28, which starts at 1 and only advances. Timeline
    // symbols therefore do not use the raw minute for zero/out-of-order logs.
    auto timeline_event = [&](int team, uint8_t minute, uint16_t word) {
        SportschauLogEvent event = card_event;
        event.source_code = 3;
        event.source_code_player_word = 0x3fff;
        event.team_id = team;
        event.source_value = minute;
        event.source_team_value_word = uint16_t((uint16_t(team) << 7) | minute);
        event.source_word_index = word;
        return event;
    };
    auto timeline_stamp = [&](Image& image, int x, int side) {
        const int atlas_x = 250 + exe.u8(::bmh::layout::Sportschau_FormatError_RuntimeField + 3);
        const int dest_y = 100 + int(int8_t(exe.u8(::bmh::layout::Sportschau_FormatError_RuntimeField3 + 1 + side * 2)));
        for (int dy = 0; dy < 5; ++dy) for (int dx = 0; dx < 5; ++dx) {
            const uint8_t ink = pixel(art, atlas_x + dx, 41 + dy);
            if (ink && x + dx < image.width) image.at(x + dx, dest_y + dy) = ink;
        }
    };
    auto check_timeline = [&](const Image& actual, const Image& expected, const char* message) {
        for (int y = 0; y < 240; ++y) for (int x = 62; x < 320; ++x) {
            // Only the timeline rows; report text is a separate Source layer.
            bool timeline_row = false;
            for (int side = 0; side < 2; ++side) {
                const int top = 100 + int(int8_t(exe.u8(::bmh::layout::Sportschau_FormatError_RuntimeField3 + 1 + side * 2)));
                timeline_row |= y >= top && y < top + 5;
            }
            if (timeline_row) check(pixel(actual, x, y) == pixel(expected, x, y), message);
        }
    };
    SportschauFixtureView zero_timeline = card;
    zero_timeline.log_events = {timeline_event(card.home_team, 0, 0)};
    Image expected_zero = card_report.render_report(0);
    timeline_stamp(expected_zero, 67, 0); // local_28 == 1, not raw minute zero.
    check_timeline(SportschauMatchReport(data, zero_timeline, options).render_report(1),
                   expected_zero, "zero-minute timeline uses Source cursor 1");

    SportschauFixtureView backwards_timeline = card;
    backwards_timeline.log_events = {timeline_event(card.home_team, 12, 0),
                                    timeline_event(card.away_team, 4, 2)};
    Image expected_backwards = card_report.render_report(0);
    timeline_stamp(expected_backwards, 89, 0);
    timeline_stamp(expected_backwards, 89, 1); // cursor remains 12 after the minute-4 record.
    check_timeline(SportschauMatchReport(data, backwards_timeline, options).render_report(2),
                   expected_backwards, "out-of-order timeline preserves Source cursor 12");

    SportschauFixtureView late_miss = card;
    late_miss.log_events.clear();
    for (uint16_t index = 0; index < 26; ++index) {
        SportschauLogEvent card_row = card_event;
        card_row.source_word_index = uint16_t(index * 2);
        late_miss.log_events.push_back(card_row);
    }
    SportschauLogEvent final_miss = miss;
    final_miss.source_word_index = 52;
    final_miss.consumes_following_pair = true;
    final_miss.has_following_pair = false;
    final_miss.source_value = 0;
    final_miss.source_team_value_word = uint16_t(uint16_t(late_miss.home_team) << 7);
    late_miss.log_events.push_back(final_miss);
    SportschauMatchReport late_miss_report(data, late_miss, options);
    check(late_miss_report.render_report(late_miss.log_events.size()).width == 320,
          "late Source miss restore outside the viewport is a safe no-op");

    SportschauFixtureView late_goals = fixture;
    late_goals.log_events.clear();
    late_goals.source_played_minute = 18;
    for (uint16_t ordinal = 0; ordinal < 18; ++ordinal) {
        const uint16_t word = uint16_t(ordinal * 4);
        const uint8_t minute = uint8_t(ordinal + 1);
        SportschauLogEvent late_goal;
        late_goal.group = late_goals.group;
        late_goal.fixture = late_goals.fixture;
        late_goal.event_ordinal = uint16_t(ordinal * 2);
        late_goal.source_word_index = word;
        late_goal.source_code_player_word = 0x0fff;
        late_goal.source_team_value_word = uint16_t((uint16_t(late_goals.home_team) << 7) | minute);
        late_goal.source_code = 0;
        late_goal.player_id = 0x0fff;
        late_goal.team_id = late_goals.home_team;
        late_goal.source_value = minute;
        late_goal.consumes_following_pair = true;
        late_goal.has_following_pair = true;
        late_goal.following_code_player_word = 0x2fff;

        SportschauLogEvent consumed;
        consumed.group = late_goals.group;
        consumed.fixture = late_goals.fixture;
        consumed.event_ordinal = uint16_t(ordinal * 2 + 1);
        consumed.source_word_index = uint16_t(word + 2);
        consumed.source_code_player_word = 0x2fff;
        consumed.source_team_value_word = uint16_t((uint16_t(late_goals.home_team) << 7) | minute);
        consumed.source_code = 2;
        consumed.player_id = 0x0fff;
        consumed.team_id = late_goals.home_team;
        consumed.source_value = minute;
        late_goals.log_events.push_back(late_goal);
        late_goals.log_events.push_back(consumed);
    }
    SportschauMatchReport late_goal_report(data, late_goals, options);
    const Image before_last_row = late_goal_report.render_report(34);
    const Image beyond_last_row = late_goal_report.render_report(36);
    for (int y = 184; y < 191; ++y) for (int x = 8; x < 30; ++x)
        check(pixel(before_last_row, x, y) == pixel(beyond_last_row, x, y),
              "Source suppresses running goal-row text after the report panel ends while score still advances");

    SportschauFixtureView missing_short = fixture;
    missing_short.home_short_name_available = false;
    SportschauMatchReport missing_short_report(data, missing_short, options);
    SportschauFixtureView empty_short_control = missing_short;
    empty_short_control.home_short_name_available = true;
    empty_short_control.home_short_name.clear();
    SportschauMatchReport empty_short_control_report(data, empty_short_control, options);
    check(missing_short_report.render_report(0).pixels == empty_short_control_report.render_report(0).pixels,
          "missing domestic Source abbreviation stays omitted without aborting the report");

    const Font minute_font = load_font(data / "F" / "BMH3.FNT");
    for (uint8_t minute_value : {uint8_t(1), uint8_t(12), uint8_t(120)}) {
        SportschauFixtureView minute_fixture = fixture;
        minute_fixture.source_played_minute = minute_value;
        SportschauLogEvent minute_event = card_event;
        minute_event.source_value = minute_value;
        minute_event.source_team_value_word = uint16_t((uint16_t(minute_fixture.home_team) << 7) | minute_value);
        minute_fixture.log_events = {minute_event};
        SportschauMatchReport minute_report(data, minute_fixture, options);
        const Image actual = minute_report.render_report(1);
        const Image same_fixture_empty = minute_report.render_report(0);
        Image expected = same_fixture_empty;
        std::string formatted = std::to_string(minute_value);
        if (formatted.size() < 3) formatted.insert(0, 3 - formatted.size(), '%');
        formatted += load_textd(data / "TEXTD.BLK")[(::bmh::layout::Sportschau_FormatError_TextPointer - ::bmh::layout::TextPointerTable) / 4];
        minute_font.draw(expected, 8 + 119, 66 - minute_font.baseline, formatted, 0x69);
        for (int y = 60; y < 73; ++y) for (int x = 127; x < 205; ++x)
            check(pixel(actual, x, y) == pixel(expected, x, y),
                  "Source minute field preserves literal percent padding and suffix placement");
    }

    bool rejected_too_many = false;
    try { (void)with_goal.render_report(2); }
    catch (const FormatError&) { rejected_too_many = true; }
    check(rejected_too_many, "renderer rejects a prefix past the immutable Source log");

    fixture.log_events[0].source_team_value_word = uint16_t((uint16_t(fixture.away_team) << 7) | 12);
    bool rejected_forged_team = false;
    try { SportschauMatchReport forged(data, fixture, options); (void)forged.render_report(1); }
    catch (const FormatError&) { rejected_forged_team = true; }
    check(rejected_forged_team, "renderer rejects event words whose team disagrees with the typed fixture");
}

void compare_source_frame(const Image& actual, const Image& backdrop, const Image& atlas,
                          unsigned atlas_frame) {
    // Independent literal atlas-cell oracle for legacy routine's four columns,
    // five rows, 70x47 crops (the atlas inventory's 72-pixel label is not used).
    constexpr std::array<int, 4> source_x{{0, 70, 140, 210}};
    constexpr std::array<int, 5> source_y{{0, 47, 94, 141, 188}};
    check(actual.width == 320 && actual.height == 240, "intro frame stays 320x240");
    for (int y = 0; y < 240; ++y) {
        for (int x = 0; x < 320; ++x) {
            uint8_t expected = backdrop.pixels[size_t(y) * backdrop.width + x];
            if (x >= 99 && x < 169 && y >= 46 && y < 93) {
                const int sx = source_x[atlas_frame % 4] + (x - 99);
                const int sy = source_y[atlas_frame / 4] + (y - 46);
                expected = atlas.pixels[size_t(sy) * atlas.width + sx];
            }
            if (actual.pixels[size_t(y) * actual.width + x] != expected)
                throw std::runtime_error("frame differs from literal Source crop");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: ui_sportschau_tests <BMH data>");
        const std::filesystem::path data(argv[1]);
        const Image backdrop = load_kb(data, "132.VGA");
        const Image atlas60 = load_kb(data, "60.VGA");
        const Image atlas61 = load_kb(data, "61.VGA");
        check(backdrop.width == 320 && backdrop.height == 240, "real KB/132 backdrop dimensions");
        check(atlas60.width == 288 && atlas60.height == 235 &&
              atlas61.width == 288 && atlas61.height == 235, "real Source atlas dimensions");

        SportschauIntro intro(data);
        check(SportschauIntro::frame_count == 40 && SportschauIntro::frames_per_atlas == 20,
              "Source intro is two 20-frame atlases");
        const Bytes raw_intro = read_file(data / "KB" / "132.VGA");
        const size_t palette_offset = 8 + size_t(le32(raw_intro, 4));
        check(raw_intro.size() == palette_offset + 768,
              "Source KB/132 carries exactly one embedded 256-color palette");
        Palette expected_intro_palette;
        for (size_t color = 0; color < expected_intro_palette.argb.size(); ++color) {
            const auto channel = [&](size_t component) {
                return uint32_t(((raw_intro[palette_offset + color * 3 + component] & 0x3f) * 255 + 31) / 63);
            };
            expected_intro_palette.argb[color] = 0xff000000u | channel(0) << 16 | channel(1) << 8 | channel(2);
        }
        check(intro.palette_override() && intro.palette_override()->argb == expected_intro_palette.argb,
              "intro exposes the literal KB/132 palette that legacy routine installs");
        const Palette global_palette = load_palette(data / "M3PC.COL");
        const uint8_t background_sample = pixel(backdrop, 0, 0);
        check(intro.palette_override()->argb[background_sample] != global_palette.argb[background_sample],
              "embedded intro colors differ from the global M3PC palette at a real backdrop pixel");
        for (unsigned index = 0; index < SportschauIntro::frame_count; ++index) {
            const Image frame = intro.render_frame(index);
            compare_source_frame(frame, backdrop, index < 20 ? atlas60 : atlas61, index % 20);
        }
        check(intro.render_frame(0).pixels == intro.render_frame(0).pixels,
              "rendering is deterministic and read-only");

        bool rejected_frame_40 = false;
        try { (void)intro.render_frame(SportschauIntro::frame_count); }
        catch (const FormatError&) { rejected_frame_40 = true; }
        bool rejected_uint32_max = false;
        try { (void)intro.render_frame(UINT32_MAX); }
        catch (const FormatError&) { rejected_uint32_max = true; }
        check(rejected_frame_40 && rejected_uint32_max,
              "out-of-range frame cannot alias an atlas or wrap");

        bool rejected_missing_assets = false;
        try { (void)SportschauIntro(data / "missing"); }
        catch (const std::exception&) { rejected_missing_assets = true; }
        check(rejected_missing_assets, "missing/empty asset directory fails closed");
        report_tests(data);
        std::cout << "Sportschau intro/report: literal 40-frame atlas and static Source report guards passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
