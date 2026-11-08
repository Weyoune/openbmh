# OpenBMH Classic 1.0.10

This release adds the transfer market search dialog, fixes the "Angebot" button
of the poaching dialog and corrects the asking price shown in the market. It
requires the player's own game installation. Extended development remains
separate in [OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Changes since 1.0.9

- **Suchen and Vorschlag in the transfer market.** Both buttons did nothing.
  They now open the search profile dialog of the original (positions, age,
  strength, attributes, sides, price classes). "Suchen" opens the stored
  profile, "Vorschlag" the profile your squad asks for. Closing with the
  confirm button stores the profile and announces the listed players that
  fit, one message box each; the right mouse button or Esc closes it and
  switches the search off. The rules (closing rule, matching, suggestion) were
  compared with the Diamond 3.7 routines on ten savegames without a
  difference. The console has the same steps: `ShowSearchProfile`,
  `SuggestSearchProfile`, `SetSearchProfile`.
- **Abwerben: "Angebot" button.** The engine asks the selling manager and the
  buyer a yes/no question when a poaching offer is decided. The graphical
  client did not provide the question dialog, so the offer was refused
  silently and the dialog stayed open. It now shows the question.
- **Asking price in the market list.** The list showed the asking fee scaled
  by 1.25, the factor of an older Diamond build. Diamond 3.7 rounds to whole
  10,000 (the offer editor and the weekly processing already did).
- **Tests.** The controller tests now drive the whole route of reviewing and
  accepting offers on an own listing, of relisting a player after a declined
  listing and of the search dialog.

## Open points

- **"Angebote prüfen" / listing again after a declined offer.** Reported by the
  Diamond author. The console and the controller tests show the expected
  behaviour (offers can be reviewed and accepted before expiry; a player who
  left the list after a decline can be listed again). It could not be
  reproduced, so a debug log (`-debug`) or exact steps are still needed.
- **Measurement against Diamond 3.7.** The match engine (minute, duels, shots)
  and the whole weekly processing with the bid amounts are not yet compared
  with the real 3.7. The bid amounts of new offers depend on stale data in the
  original and cannot be reproduced exactly (documented in the source).
- Earlier open points remain: "player card: original atlas bounds" (#77), sporadic
  double clicks (#58) and the macOS match scene speed (#70).

## Known limits

The compatibility and long-career limits documented for
[1.0.3](../1.0.3/RELEASE_NOTES.md) remain. Football Limited is experimental;
its original V2.20 save layout remains unsupported. Keep backups of saves.
macOS packages are unsigned terminal executables, not notarised app bundles.
