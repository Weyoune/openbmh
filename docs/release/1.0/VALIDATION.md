# Classic 1.0 candidate validation

Status: implementation candidate, not final-release acceptance.

- Linux x64 Release build: all targets compiled.
- CTest: 32 passed, 157 skipped, zero failures (189 registered tests). Skips require separately configured original-data or save fixtures; they are not acceptance passes.
- Synthetic tests cover unchanged executable image loading, nine original strength ranges, atomic rejection of malformed ranges, unsupported environment guards, and legacy/extended/truncated crest gates.
- Public source scan: zero findings; generated differences catalogue matches source markers.
- Version output: both GUI and console identify 1.0.0-rc.1.
- Private crest probe: all 48 foreign league clubs in Diamond 3.7 resolve to original 38 × 38 W cells within atlas bounds. A native-size contact sheet was inspected; older Diamond data retain the neutral-crest gate.
- Author defect reference: original hash and all 1,084 listed original/changed bytes checked. All 33 defects are tracked privately; source inspection is not regression acceptance.

Still required: dedicated original-game/GUI/save roundtrips, completing the original-defect audit, per-edition gameplay review, Windows/macOS native checks and release-package acceptance. No final 1.0 tag is justified by these checks alone.

## Source export versus retained history

The restored Classic tree passes the source-only guard. The retained 0.2/0.3 history still contains launcher artwork and editor QA screenshots, which the old Classic guard classifies as binary content; this is not a clean all-history audit. CI checks the current tree actually exported by the source-package tool. The stricter `check_public_tree.py --history` command remains available for a separate repository-history audit. No history has been rewritten and no historical assets are added to the Classic source archive.
