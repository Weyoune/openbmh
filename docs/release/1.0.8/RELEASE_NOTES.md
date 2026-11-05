# OpenBMH Classic 1.0.8

This patch makes the bank credit annuity identical on every platform. It
requires the player's own game installation. Extended development remains
separate in [OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Changes since 1.0.7

- **Bank credit annuity.** The original computes the monthly payment with
  x87 floating point (64-bit mantissa at every step, one final store to a
  double). The host `long double` is only 64 bits wide on Windows (MSVC) and
  on Apple silicon, so the payment differed by 1 for roughly 0.2 % of loans of
  one to sixty million. The chain is now evaluated with integer arithmetic and
  gives the original's result on every platform.

## Validation and human QA

Release publication requires successful Windows x64, macOS ARM64 and Linux x64
builds, configured tests, product-version checks and package/source verification
for the exact tagged revision. Downloads include checksums, a platform test
report and corresponding source with pinned open-source dependencies.

This release follows a quality pass that ran single routines of the original
programme in an emulator and compared them with the engine on whole savegames,
with boundary values and a deliberately wrong result per rule that must fail.
Compared without a difference: the random generator, market value and salary,
the willingness of clubs to sell, the rating of transfer bids, club mood and
flags at the end of a season, the order of national cup ties, weekly advertising
offers, facility ageing and damage, offers to managers, weekly statistics,
familiarity decay, squad training and daily interest. The one difference found
is the annuity fixed above. Rules that depend on screens, dialogs or the master
database were not part of this pass.

Native Windows/macOS gameplay acceptance is not established by CI builds. Open
points from earlier releases remain: the "player card: original atlas bounds"
error (#77), sporadic double clicks (#58) and the macOS match scene speed (#70).

## Known limits

The compatibility and long-career limits documented for
[1.0.3](../1.0.3/RELEASE_NOTES.md) remain. Football Limited is experimental;
its original V2.20 save layout remains unsupported. Keep backups of saves.
macOS packages are unsigned terminal executables, not notarised app bundles.
