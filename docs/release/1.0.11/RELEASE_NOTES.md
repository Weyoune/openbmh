# OpenBMH Classic 1.0.11

This release lets the game run from the asset files alone, adds the transfer
market search dialog, fixes the "Angebot" button of the poaching dialog and the
selling of a listed player, and corrects the asking price shown in the market.
Extended development remains separate in
[OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## No BMH.EXE or FLTD.EXE needed

- The numbers of the Diamond 3.7 executable (tables, rates, positions, prices) and
  of the English Football Limited are built into the program. A game folder, ZIP
  or 7z with only the asset files (`TEXTD.BLK` and `M3D.MAN`, or `TEXTE.BLK`
  and `M3E.MAN`, plus the picture, font and sound folders) is a complete game.
  A `BMH.EXE` or `FLTD.EXE` that is there is used as before.
- The game texts of the original executables (live commentary, some messages) are
  in the file **openbmh.dat**, which ships next to `OpenBMH` and `openbmh_cli`
  in the release package. The game looks for it in the game folder, in the
  program folder and in the current folder (or where `OPENBMH_DAT` points to).
  Without the file those texts are empty and the game still runs.
- The default manager name is "OpenBMH" instead of "Nobody".
- A recorded session gives the same output as with the real `BMH.EXE`.

## Changes since 1.0.9 (1.0.10 was never published; its changes are part of this release)

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
- **Abwerben: "Angebot" button.** Two causes. The engine asks the selling
  manager and the buyer a yes/no question when a poaching offer is decided;
  the graphical client did not provide the question dialog. And the agreement
  of the poached player ("Ihr erstes Angebot wird angenommen!") had no message
  dialog, which refused the whole offer. Both exist now. A refused offer or
  transfer action also shows its reason (and writes it to the debug log)
  instead of a dead button.
- **Verkaufen on a listed player.** Clicking a player who is already on the transfer
  list and then VERKAUFEN did nothing when no offer had arrived yet. It now asks
  the question of the original, "Wollen Sie ... vom Transfermarkt holen?": yes takes
  the player off the list (he stays in the squad), no leads on to the offers.
- **Pressed buttons.** Suchen, Vorschlag, Abwerben and the other buttons of
  the market are drawn pressed while the mouse button is held.
- **Scouting message.** Confirming the search dialog first shows the message
  of the scouting department, then one box per fitting listing.
- **Asking price in the market list.** The list showed the asking fee scaled
  by 1.25, the factor of an older Diamond build. Diamond 3.7 rounds to whole
  10,000 (the offer editor and the weekly processing already did).
- **Tests.** The controller tests now drive the whole route of reviewing and
  accepting offers on an own listing, of relisting a player after a declined
  listing and of the search dialog.

## Measurement against Diamond 3.7

- The match engine of a recorded matchday in the real 3.7 (DOSBox-X) matches the
  port in all 94 minutes, with the random number state at kick-off, in every
  minute, at the final whistle and at the next desk.
- Two apparent deviations (from minute 10, and on the days after the match) were
  errors of the comparison tool, not of the engine, and are fixed or explained there.

## Open points

- **"Angebote prüfen" / listing again after a declined offer.** Reported by the
  Diamond author. The console and the controller tests show the expected
  behaviour. It could not be reproduced, so a debug log (`-debug`) or exact
  steps are still needed.
- The bid amounts of new offers in the weekly processing depend on stale data in
  the original and cannot be reproduced exactly (documented in the source).
- Only one matchday and the days after it were recorded from the real 3.7; the
  weekly processing, month changes and transfers are not yet compared.
- The graphical client has not yet been tried with asset files only.
- Earlier open points remain: "player card: original atlas bounds" (#77), sporadic
  double clicks (#58) and the macOS match scene speed (#70).

## Known limits

The compatibility and long-career limits documented for
[1.0.3](../1.0.3/RELEASE_NOTES.md) remain. Football Limited is experimental;
its original V2.20 save layout remains unsupported. Keep backups of saves.
macOS packages are unsigned terminal executables, not notarised app bundles.