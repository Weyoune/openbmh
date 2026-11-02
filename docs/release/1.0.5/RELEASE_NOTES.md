# OpenBMH Classic 1.0.5

This patch fixes environment access with compatible older DIAMOND data and
English contract labels. It requires the player's own game installation.
Extended development remains separate in
[OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Changes since 1.0.4

- Environment management uses the common Diamond 3.7 monthly model for supported
  source-data editions. Different original programme instructions no longer
  block compatible numeric data. Six budget steps are validated and installed
  together; malformed layouts and artwork bounds remain guarded. Existing
  saved lump-sum academy budgets retain their accounting until edited.
- Football Limited translation supplies its actual environment geometry,
  numeric steps and suffixes. Selecting the original English installation
  rebuilds older translated caches automatically.
- Contract tabs use the installation's original text and font path, including
  English data, salary and team-bonus captions.
- English manager selection exposes the Original four starting levels. The
  fifth internal data league remains in the model, squads, rankings and fixture
  data. Hidden targets/clubs are rejected before fresh-manager mutation or RNG;
  existing saved league values are not generally rejected.

## Validation and human QA

Release publication requires successful Windows x64, macOS ARM64 and Linux x64
builds, configured tests, product-version checks and package/source verification
for the exact tagged revision. Downloads include checksums, a platform test
report and corresponding source with pinned open-source dependencies.

Local checks use an older primary DIAMOND installation, separately identified
Diamond 3.6 and 3.7 data, German CD data and translated Football Limited data.
They cover all six environment controls, transaction ownership and unchanged RNG,
source-language contract glyphs, valid manager selection and hidden-level refusal.
Native Linux before/after checks reproduce the older DIAMOND menu block and
verify the corrected environment screen and English contract captions. The older
package's precise edition name remains unconfirmed; the separately supplied
3.6 installation also passed the original controller check.

Issues [#55](https://github.com/Weyoune/openbmh/issues/55) and
[#56](https://github.com/Weyoune/openbmh/issues/56) remain open for human QA.
Native Windows/macOS gameplay acceptance is not established by CI builds.

## Known limits

The compatibility and long-career limits documented for
[1.0.3](../1.0.3/RELEASE_NOTES.md) remain. Football Limited is experimental;
its original V2.20 save layout remains unsupported. Keep backups of saves.
macOS packages are unsigned terminal executables, not notarised app bundles.

No original executables, databases, saves, graphics or audio are included.
