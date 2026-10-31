# Classic-plus-1 profile

This opt-in experimental profile keeps the Classic competition types and indexed artwork formats. It is not a DOS patch. The original executable is read only; verified dimension data and the expanded calendar are prepared in memory. Unmarked installations retain Classic dimensions and save identity.

## Discovery and dimensions

`OBMHPROF.INI` contains exactly:

```ini
[OpenBMH]
profile=classic-plus-1
```

Unknown keys/profiles are rejected. The German executable and calendar are required.

| Table | Classic | Classic-plus-1 |
| --- | ---: | ---: |
| League sizes, German divisions | 18/18/18/18/18 | 18/18/18/18/18 |
| Spain/England/France/Italy | 12/12/12/12 | 18/20/18/18 |
| League clubs | 138 | 164 |
| Cup-only clubs | 30 | 30 |
| League and cup records | 168 | 194 |
| Other foreign clubs | 189 | 189 |
| National teams | 62 | 62 |
| All club/country entries | 419 | 445 |
| Players | 2,574 | 3,334 |
| Clubs with 24 roster slots | 90 | 164 |
| First market slot | 2,928 | 3,936 |
| First national roster slot | 3,072 | 4,080 |
| First extra-player slot | 3,096 | 4,104 |
| Total runtime slots | 3,150 | 4,158 |

Player identifiers still use twelve bits, with 4,095 reserved for empty. **An absolute roster slot is not a player identifier**: extended auxiliary slots exceed 4,095 and must not be masked. Club identifiers fit existing packed club fields and crest selectors fit nine bits.

## Master database and artwork

The M3D table order and encoding remain unchanged. Kit selectors scale with all club/country entries; short-name offsets and auxiliary words scale with league-plus-cup entries. The squad master has four fewer rows than runtime, because temporary transfer rows are inserted during career initialization. String pools retain unsigned 16-bit offsets. Imported names use ISO 646-DE, with explicit transliteration of unsupported letters.

The preparation script keeps original player words and original IDs, appends supplemental players and records the source-to-runtime club mapping. Existing twelve-club foreign cores are retained; additional clubs are ranked by donor mean player strength. Missing-person data are not silently fetched or invented. Every supplied provenance/fictional-field note is retained in the private import report.

Crests are fitted into 38-by-38 cells of 304-by-38 VGA sheets, using the supplied VGA palette. No PNG/SVG decoder is needed at runtime. Transparency is index zero for these generated crest cells. Existing protected crest cells remain intact. Asset source and rights notes remain separate from the application's GPL license.

The offline preparation utility accepts local paths:

```sh
python3 -m venv asset-tools
asset-tools/bin/pip install Pillow CairoSVG
asset-tools/bin/python tools/poc/prepare_international.py \
  --source /path/to/supplied/german-game \
  --supplement /path/to/supplied/season.obw \
  --output /path/to/new/private-data
```

The destination must not exist. Inputs are never overwritten. Original content and private import reports must stay outside public source trees, issues and CI artifacts.

## Save and memory boundaries

Classic containers retain `V2.24`. Extended containers use `O1.1`, checked against the selected installation before state decoding. The table sequence remains familiar, with count-derived sizes for club records, manager/opponent histories, crests, kits and short-name offsets. Extended typed tables must not grow into adjacent globals in the guest-memory compatibility representation.

The result store already reserves nine leagues with 38 rounds and ten matches per round. Expanded scheduling uses each league's own fixture size. The shared foreign calendar supplies 38 dates; each league stops at its own 34 or 38 rounds. Domestic/cup/national flags are preserved. Dates are an experimental calendar policy, not a claim about real-world fixtures.

## Distribution

Public artifacts contain the implementation, build information and corresponding sources. The community data ZIP is prepared privately and separately; existing content provenance and rights are unchanged. The stable 1.0 release line is not replaced by this POC.
