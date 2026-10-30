# OpenBMH 1.0.1

The automatic-lineup monitor now draws the selected formation from the actual manager formation table. Outdoor, indoor and custom formation slots retain their original coordinates and palette. Dragging a desktop element shows the original yellow outline, which disappears when the drag ends.

SDL video initialization now precedes the initial native file picker. On macOS this lets SDL establish the Cocoa application and keyboard activation policy before the picker creates or modifies NSApp. It addresses keyboard input missing throughout the game, including entering the manager name. A native macOS regression checks the activation policy after the picker initialization/shutdown lifecycle.

Native interactive Mac keyboard acceptance remains human QA; automated checks do not represent physical typing on a user's Mac.

---

# OpenBMH 1.0.0

Classic direct-installation runtime restored from v0.1.2. The game executable is OpenBMH / OpenBMH.exe; openbmh_cli remains available.

Reviewed backports include host filename/path handling, explicit quit confirmation, original message text installation in standalone tools, and clearing the calendar replay flag after undoing a reschedule. German CD-compatible BMH and verified Diamond editions are the release targets. Football Limited remains experimental; the old floppy executable is unsupported.

This community release focuses on the original game. See VALIDATION.md for the scope and limits of its checks. Build/test reports do not establish complete bit-for-bit parity with every original edition.

Diamond 3.7 foreign league crests use the supplied edition's original drawing gate, database selectors and W sheets. Older editions retain their original neutral foreign-club crest.

Transfer-market players now enter the season-boundary retirement process, preventing the five-bit age from wrapping from 47 to 16. Relocated free agents no longer leave a duplicate in their old market slot. Young club players retain their existing random-draw count.

The automatic lineup reads the original foreign-player limit switch: enabled editions enforce three foreign starters in domestic clubs and two in the foreign leagues, with original reserve replacement and partial-lineup handling. Editions that disable this rule retain unrestricted selection. New careers also initialize the original empty EM qualification placeholders with national-team IDs; existing nonzero seeds and loaded savegames are preserved.

Known limits: Football Limited remains experimental, the floppy executable is unsupported, and intentional implementation differences remain listed in docs/DIFFERENCES.md. The supplied original-defect catalogue is an ongoing compatibility reference, not a claim that all its entries have received independent runtime acceptance.

The season reset processes each player only once, including temporary offer references to an active club player. This prevents double ageing and regeneration of a still-owned player into a second club. The correction is covered by a synthetic cached-offer regression and long runs through eleven to twelve season-end desks.

The start year and weekday are read from the supplied edition for the desktop, engine and save-list headers. Original CD therefore starts in 1995 on Saturday; checked Diamond editions retain 2026 on Wednesday. The original date arithmetic remains intact.
