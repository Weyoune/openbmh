# OpenBMH Classic 1.0.7

This patch fixes national-team desk screens and two small interface details
reported by testers. It requires the player's own game installation. Extended
development remains separate in
[OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Changes since 1.0.6

- **National-team desk.** Other Games and Top Scorers open again; both screens
  are identified by the home club, so the desk is no longer rejected as stale
  or as a mismatched acknowledgement.
- **Computer tip.** After the results of a round are known the button is only
  pressed and released, as in the original, instead of raising an engine error.
- **Manager selection.** JA of the confirmation question is shown pushed in
  briefly after it is clicked.

## Validation and human QA

Release publication requires successful Windows x64, macOS ARM64 and Linux x64
builds, configured tests, product-version checks and package/source verification
for the exact tagged revision. Downloads include checksums, a platform test
report and corresponding source with pinned open-source dependencies.

The fixes were checked with the desktop, controller and manager tests; native
Windows/macOS gameplay acceptance is not established by CI builds. Open points
from 1.0.6 remain: the "player card: original atlas bounds" error (#77),
sporadic double clicks (#58) and the macOS match scene speed (#70).

## Known limits

The compatibility and long-career limits documented for
[1.0.3](../1.0.3/RELEASE_NOTES.md) remain. Football Limited is experimental;
its original V2.20 save layout remains unsupported. Keep backups of saves.
macOS packages are unsigned terminal executables, not notarised app bundles.
