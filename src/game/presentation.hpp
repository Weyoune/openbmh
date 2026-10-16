// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// What the original shows during a matchday, as events for the UI (BMH.exe V2.24 + supported rule profile).
//
// The match engine reproduces the random numbers of the original's live desk, its message
// boxes and animations, but draws nothing. Where the original would show something it
// appends a ShowEvent (MatchEngine::show, when set) with the values the original computes
// for the picture - the UI renders from these and never draws random numbers itself, so
// rendering, hovering or replaying a frame cannot change the game.
//
// The event descriptions document the observed match activity; the
// screen coordinates are those of the 320x240 work screen PresentationRuntimeField unless noted.
//
//   Booking      EventImpl, layout field-layout field: the message box of a foul. Shown when
//                MatchEngineRuntimeField12 is set and the offender's team (or, for flag 0x80, the
//                opponent) is watched: team flag 0x80 (live desk; for a yellow card only
//                without button MatchEngine_EventImpl_ExecutableField) or 0x40 (ticker; button MatchEngine_EventImpl_ExecutableField2 for a
//                yellow, MatchEngine_EventImpl_ExecutableField3 otherwise switches it off). Text: LiveMatchday_FormatError_TextPointer2[kind]
//                (0 yellow, 1 red, 2 injury, 3 second yellow) + the player's name, for a
//                yellow card also his yellow cards of the season (Cstring); the side's
//                colours (Button(side | 0x40)), the scoreboard of the match flashes
//                (DcThat(6, group, match)), the text is drawn at (0x5f, 0x27)
//                (DrawnFirstBy), a whistle (PauseMusic(0x11), not for injuries), a delay
//                (AdvanceStep(3 + (kind != 0))) and the box waits for a click
//                (PatternRect). `shown` tells whether the box appears; the event is
//                appended for every foul so that a ticker can list it.
//   Substitution Injure after a foul: `player` leaves, `other` comes on.
//   Chance       the live desk after a chance (layout field-layout field, EndPhase): kind 0 open
//                play, 1 penalty / free kick, 2 follow-up, 3 corner; the shooter, the
//                assistant, the distance shown (MatchEngineRuntimeField15, kind 1), the goal. The text
//                is the shooter's name (+ LiveMatchday_FormatError_ExecutableField3), the distance (LiveMatchday_FormatError_TextPointer12 /
//                LiveMatchday_FormatError_TextPointer13 "m"); halved indoors (MatchEngine_Minute_ExecutableField4).
//   SetPiece     LiveSetPieceFrames(kind, distance) with MatchEngineRuntimeField6 = `side` (1 = the picture
//                is mirrored): the ball's spot (a, b = x, y in the scene's 1000-wide
//                coordinates), the kicker's run-up timer (c) and, for kind 3, the flight
//                time of the ball (d).
//   ShotOnGoal   SetPieceAnimation(miss, follow_up) + Range4: `side` (0 left / 1 right
//                lane of MatchEngineRuntimeField4), a = height of the shot, b = lane (x), c = the ball's
//                end height, d = the goalkeeper's first reaction frame and its delay
//                (d >> 8); `flag` bit 0 = follow-up picture, bit 1 = mirrored.
//   PenaltyKick  PenaltyShootout live: "<n>. Elfmeter" (LiveMatchday_FormatError_TextPointer4 / LiveMatchday_FormatError_TextPointer5) + the
//                taker's name at (0x49, 0x5f), the kick animated (SetPiece kind 2 and
//                ShotOnGoal follow-up), then the score of the shoot-out (a : b);
//                c = the round (1-5, then sudden death).
//   ShootoutEnd  PenaltyShootout live: the text LiveMatchday_FormatError_TextPointer6 at (0x49, 0x5f).

#include <cstdint>
#include <string>

namespace bmh {

enum class Show : uint8_t { Booking, Substitution, Chance, SetPiece, ShotOnGoal, PenaltyKick, ShootoutEnd };

struct ShowEvent {
    Show what;
    int16_t minute = 0;   // PresentationRuntimeField2 (0x79 = 121 for the shoot-out)
    uint8_t group = 0;    // match group (descriptor index) and match within it
    uint8_t match = 0;
    uint8_t side = 0;     // 0 home, 1 away (of the acting team; SetPiece: MatchEngineRuntimeField6)
    uint8_t kind = 0;     // Booking: 0 yellow, 1 red, 2 injury, 3 second yellow; Chance /
                          // SetPiece: the chance kind
    uint8_t flag = 0;     // Booking: shown in a box; Chance / PenaltyKick: goal; ShotOnGoal: see above
    int16_t team = -1;
    int16_t player = 0xFFF;  // offender, shooter, taker, player leaving
    int16_t other = 0xFFF;   // assistant, player coming on
    int32_t a = 0, b = 0, c = 0, d = 0;  // per kind, see above
    std::string text;     // the composed text as the original builds it (ISO 646-DE)
};

}  // namespace bmh
