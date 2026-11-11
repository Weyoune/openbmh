# OpenBMH Classic 1.0.13

This release returns the stadium to the size of the original and carries a large
internal clean-up. Apart from the stadium the game plays exactly as
[1.0.12](../1.0.12/RELEASE_NOTES.md): the same careers, turn by turn, and the same
pictures. Extended development remains separate in
[OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Stadium: 96,000 places as in the original

- **The block limits are those of the game edition again.** Versions 1.0.3 to 1.0.12
  allowed 14/24/14/20 thousand places per block, 144,000 in total. From 100,000
  spectators on the live match could not show the attendance correctly: the original
  display has room for five digits. The limits are now taken from the game data as
  supplied: Diamond 3.7 gives 8/18/8/14 thousand per block, twice, which is 96,000
  places. Building times follow the edition as well (30/37/30/25 weeks for a block
  built in one go).
- The documented deviation "larger stadium" is gone from
  [DIFFERENCES](../../DIFFERENCES.md); [COMPATIBILITY](../../COMPATIBILITY.md)
  describes the rule.

## Engine checks against the original

- **Indoor tournament strengths explained.** The team strengths at the kick-off of an
  indoor match differed from a recording of the real 3.7 in some turns. The cause is
  the weather the original keeps from the last chance of the previous match (rain or
  frost lowers players whose condition is below their technique), not the engine. The
  comparison tool now carries that weather over, and the turns agree.

## Internal

Nothing in this section changes the game. Every step was compared with the state
before it: nine careers of 110 turns each (state checksum per turn, German and
English data), 86 recorded turns of the real 3.7, a walk through the screens with a
checksum per picture, and the window program played by a script.

- **Smaller files with named functions.** The window program (`main.cpp`, 4,500
  lines), the controller's request function (2,000 lines) and the career session
  (9,000 lines) are split into files by topic with one named function per presenter
  or request.
- **Shared helpers.** The pixel loops of about forty screens (`ui/draw.hpp`), the
  text encoding (`formats/text_encoding.hpp`) and the check helper of the tests
  exist once instead of as copies.
- **World layout.** The dimensions of the game world (team id ranges, squad slots,
  league sizes, promotion ladder, record sizes) are described in one place
  (`game/world_layout.hpp`, `game/records.hpp`) instead of as numbers spread over
  the engine. Classic uses the numbers of the original; see "World layout" in
  [architecture](../../architecture.md).
- **Build.** On Windows the asset viewer no longer overwrites the game in the build
  directory (both were `openbmh.exe` to a file system that ignores case), and the
  app sources are compiled once instead of once per test.
