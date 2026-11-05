# OpenBMH Classic 1.0.6

This patch fixes bugs reported by Windows and macOS testers and adds a `-debug`
mode. It requires the player's own game installation. Extended development
remains separate in [OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Changes since 1.0.5

- **Debug mode.** `-debug` writes a log and the redirected error output to a
  `debug` folder inside the save directory and stores a savegame after every
  kick-off, so a failing session can be reproduced from the files.
- **Settings.** Every unlocked switch of the settings screen is stored; only
  locked rows stay fixed.
- **Training.** The skill bars per player and the hovered row are drawn.
- **Line-up.** Set-piece takers can be picked and players placed in the shooter
  editor. The live line-up now also opens at the national-team desk.
- **Mouse pointer.** The original game pointer is used, at the correct size on
  high-DPI displays (macOS).
- **Calendar.** The calendar works after a kick-off and at the end of the
  season, shows the original colour gradient and uses the live season values.
- **Running matchday.** Table, Strengths and the results page no longer hang or
  reject clicks while a matchday is running.
- **National-team desk.** Save loading, Table, Strengths, Other Games, Top
  Scorers and the live line-up work for the national coach; the club badges on
  the desk follow the opponent of the day.
- **Joined managers.** A manager who joins a running career sees his next
  opponent instead of "no match".
- **Club information.** The Radio/Video setting of the club information screen
  persists, so an AI match set to Video appears live.
- **Stadium.** The yellow hover frame appears over seat blocks, investments and
  buildings; the panorama view is not framed.
- **Advertising.** ANNEHMEN is hidden when the offer cannot be accepted, as in
  the original. Settings screen shows the product name and version.
- **Table calculator.** The right mouse button closes the From/To calculator.

## Validation and human QA

Release publication requires successful Windows x64, macOS ARM64 and Linux x64
builds, configured tests, product-version checks and package/source verification
for the exact tagged revision. Downloads include checksums, a platform test
report and corresponding source with pinned open-source dependencies.

Several fixes were reproduced on testers' savegames and checked with test
programmes; native Windows/macOS gameplay acceptance is not established by CI
builds. Open points: the "player card: original atlas bounds" error (#77),
KI-Tipp after a match (#69), sporadic double clicks (#58) and the macOS match
scene speed (#70).

## Known limits

The compatibility and long-career limits documented for
[1.0.3](../1.0.3/RELEASE_NOTES.md) remain. Football Limited is experimental;
its original V2.20 save layout remains unsupported. Keep backups of saves.
macOS packages are unsigned terminal executables, not notarised app bundles.
