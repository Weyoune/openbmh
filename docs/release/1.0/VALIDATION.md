# Classic 1.0 candidate validation

Status: implementation candidate, not final-release acceptance.

- Linux x64 Release build: all targets compiled.
- CTest: 33 passed, 157 skipped, zero failures (190 registered tests). Skips require separately configured original-data or save fixtures; they are not acceptance passes.
- Synthetic tests cover unchanged executable image loading, nine original strength ranges, atomic rejection of malformed ranges, unsupported environment guards, and legacy/extended/truncated crest gates.
- Public source scan: zero findings; generated differences catalogue matches source markers.
- Version output: both GUI and console identify 1.0.0-rc.1.
- Private crest probe: all 48 foreign league clubs in Diamond 3.7 resolve to original 38 × 38 W cells within atlas bounds. A native-size contact sheet was inspected; older Diamond data retain the neutral-crest gate.
- Author defect reference: original hash and all 1,084 listed original/changed bytes checked. All 33 defects are tracked privately; source inspection is not regression acceptance.

Still required: dedicated original-game/GUI/save roundtrips, completing the original-defect audit, per-edition gameplay review, Windows/macOS native checks and release-package acceptance. No final 1.0 tag is justified by these checks alone.

Source archives contain the audited current tree. The optional full-history audit is a separate check; published Git history has not been rewritten.

## Follow-up original-data checks

Twenty-six additional private-data checks passed across BMH CD and Diamond 3.6/3.7: exact loaded fixture tables, manager selection, standard dialogs, training camps, tactics, banking, settings and player cards. The Diamond 3.6 two-manager fixture also passed the human-buyer transfer and rescheduling suites. A currency-format fixture had assumed the Diamond resort price for all German editions; its fixed-price check now uses an explicit synthetic price, and the CD test passes without changing game data or product prices. These are automated render/interaction and engine checks, not a claim of complete interactive playthroughs.

The market-player retirement regression failed before the correction with an age wrap from 47 to 16 and passed afterward. It checks market boundaries, unique relocation, unaffected extra slots, new-career setup and unchanged random draw counts for a young club player.
