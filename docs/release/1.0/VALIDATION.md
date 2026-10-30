# Classic 1.0 candidate validation

Scope: recorded automated/original-data checks. Exact final source revision and native platform build records accompany the published release.

- Linux x64 Release build: all targets compiled.
- CTest: 35 passed, 157 skipped, zero failures (192 registered tests). Skips require separately configured original-data or save fixtures; they are not acceptance passes.
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

## Additional edition rules

Targeted synthetic tests pass for the enabled/disabled foreign-player switch, domestic and foreign clubs, human assistant board and card-row movement, reserve shortages and switching back to an unrestricted edition. Separate season-boundary tests pass for zero EM placeholders becoming national teams and existing nonzero seeds remaining unchanged. Final full-build/platform acceptance for these additions is recorded with the release artifacts.

Long-run diagnostics with seed 4711 and the existing explicit contract-renewal policy reached 120 turns and two season-end desks in each of the three German data editions. Diamond 3.6/3.7 returned success. Original CD exceeded the simulator's statistical high-score threshold (two ordinary results above twelve goals); it is recorded as a QA finding, not hidden or counted as a successful simulation. The earlier no-presenter runs stopped at a contract expiry; enabling the existing opt-in presenter resolved that harness limitation without changing gameplay code.

## Extended season regression

After the final lineup, qualification and once-per-player season-reset corrections, each German edition completed 600 turns using seed 4711. Original CD reached twelve season-end desks; Diamond 3.6 and 3.7 reached eleven each. With the existing explicit contract-renewal and advertising policies enabled, all three simulator runs returned success, with no duplicate club players or other invariant failures. With contract renewals alone, all three still completed 600 turns without duplicate club players; only the test policy's excessive-debt warning remained. The latter is not presented as a successful financial strategy.

The duplicate-player defect was reproduced and captured before correction: a temporary offer referenced a player already owned by a club, then the extended market-retirement pass regenerated that player a second time. Frozen private before/after saves and a cached-reference regression support the correction. Save-envelope diagnostics use a separate RNG and do not consume either gameplay stream.

Calendar epoch regression covers the original CD and Diamond parameters, engine weekdays, desktop dates, save-header dates, year rollover and atomic rejection of invalid/unknown setup instructions. The original and checked Diamond date routines differ only in their weekday/year operands. After installing the original CD calendar parameters, the 600-turn run with contract and advertising policies completed eleven season-end desks without invariant failures.
