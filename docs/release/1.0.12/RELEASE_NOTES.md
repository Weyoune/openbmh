# OpenBMH Classic 1.0.12

This release makes the buttons of the dialogs behave like the original: a button
without a lasting state is drawn pressed while the mouse button is held. It also
carries everything of [1.0.11](../1.0.11/RELEASE_NOTES.md) (the game from asset
files plus `openbmh.dat`, the transfer market search, the poaching and selling
fixes). Extended development remains separate in
[OpenBMH-Extended](https://github.com/Weyoune/OpenBMH-extended).

## Pressed buttons

Many dialogs drew their buttons raised at all times, the original shows them
pressed for as long as the mouse button is down (the friendly match question
already did). Now pressed while held:

- **Bank:** the two buttons of stocks, property, savings and credit, the OK of
  its notices, Ja/Nein of the credit fee question and the keys of its calculator.
- **Spieler kaufen (offer editor):** Team-Info, Spieler-Info, Angebot, Abbruch and
  the calculator keys.
- **Aufstellung:** Daten A/B and Spielfeld; **Spielfeld page:** Daten and Kurz
  (the action starts after a short moment, as the pressed look has to be seen).
- **Contracts:** the buttons under the offer and the team premium panels
  (Team-Prämien), OK of the answer and the calculator keys.
- **Advertising:** Abbrechen/Annehmen of an offer, OK of its notices and the keys of
  its calculator.
- **Player card:** Schließen, Statistik and Zurück; **training camp:** the booking
  button and OK; **file dialog:** Ja/Nein of the overwrite question and the page
  button of a long list; **manager confirmation:** JA and NEIN; **transfer
  decision:** OK of the confirmation.
- **Start menu (game setup):** the minus and plus buttons show the original's
  darker pressed picture while the mouse button is held (also in the joining
  manager's setup). It is a different picture, not a shifted copy of the button,
  and was taken from a recording of the real game.
- The notices that wait for any click (Sportschau, cup draw, game end screens) have
  no button and are unchanged. Toggles that stay on (training, filters) keep their
  lasting state.

## Internal

- The drawing of the wide push button, of the calculator window (panel and keys) and
  of the dialog frame is now shared (`ui/button_frame.hpp`,
  `ui/calculator_widgets.hpp`, `ui/dialog_frame.hpp`) instead of being copied in
  about twenty dialogs. A test compares the shared drawing pixel for pixel with the
  former one. A few buttons whose former drawing differs by one pixel (statistics
  rankings, team statistics, opportunities, analytics) were left as they are.

## Open points

- The pressed look was checked by tests and by reading the drawing code, not yet
  against a recording of the real Diamond 3.7; the exact timing of the short press on
  the Spielfeld page is an estimate.
- **"Angebote prüfen" / listing again after a declined offer.** Could not be
  reproduced; a debug log (`-debug`) or exact steps are still needed.
- The graphical client has not yet been tried with `openbmh.dat` and asset files only.
- The weekly processing, month changes and transfers are not yet compared with the
  real 3.7 (only one matchday and the days after it were).
- Earlier open points remain: "player card: original atlas bounds" (#77), sporadic
  double clicks (#58) and the macOS match scene speed (#70).

## Known limits

The compatibility and long-career limits documented for
[1.0.3](../1.0.3/RELEASE_NOTES.md) remain. Football Limited is experimental;
its original V2.20 save layout remains unsupported. Keep backups of saves.
macOS packages are unsigned terminal executables, not notarised app bundles.
