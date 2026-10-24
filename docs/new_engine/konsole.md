# Modern in der Konsole spielen

Bis die neue Oberfläche angebunden ist, lässt sich die Modern-Engine mit einer Modern-Welt in der Textkonsole spielen: `openbmh_modern_cli`. Es ist dieselbe Konsole wie `openbmh_cli` (Befehle siehe [CLI-Konzept](../CLI_CONCEPT/README.md)), übersetzt gegen die Modern-Session.

```
openbmh_modern_cli --console --mod <Diamond_2026-27_Modern.obw> --saves <Ordner> --seed <n>
```

Beispiel (jeder Befehlsblock endet mit `go.`):

```text
NewGame(5, 3, 1, 3, true, true)
go.
SelectTeam("Name", club:3, Bundesliga, Middle, 2)
go.
Answer(1, Yes)
go.
Kickoff
go.
Run(End)
go.
Open(LeagueTable)
go.
```

Auch der JSON-Modus (`--json`, Befehle `new`, `kickoff`, `save`, `load`, …) und mehrere Sitze (`--serve`, `--seats`) funktionieren wie bei `openbmh_cli`.

## Grenzen

- Nur deutsche Vereine sind wählbar (0.3); die Zusatz-Reiter der Auswahl (Amateure) und Auslandsvereine werden abgelehnt.
- Die Konsole zeigt die Namen der Engine (Großbuchstaben, Einzelbyte-Zeichensatz); UTF-8-Namen, Vornamen, Almanach und Spielstatistik liefert die Engine über `ModernSession::read` für die neue Oberfläche.
- Spielstände sind Modern-Spielstände (`.obs`, nennen ihre Welt); sie laden nur mit derselben Welt.

## Technik

- `src/app/session.hpp`: Mit `OPENBMH_MODERN_SESSION` heißt `app::CareerSession` die Modern-Session, und deren Typen sind unter `bmh::app` sichtbar; ohne den Schalter bleibt alles beim Classic-Stand.
- Die Konsole und die Desk-/Fenster-Aktionen (`desktop_actions`, `window_actions`) werden für `openbmh_modern_cli` ein zweites Mal übersetzt. Zurückrollen nach einer gescheiterten Aktion sichert den ganzen Session-Zustand (`app::SessionState`), damit Modern-Daten (Namen, Almanach, Toto-Spalten) erhalten bleiben.
- `src/cli/main.cpp` öffnet im Modern-Build eine Welt wie der Sim (`open_world_package`, `set_current_world`); der Spielstandordner wird nach dem SHA-256 der Welt-ID benannt.
- Vereinsauswahl: Die Auswahl der Classic-Oberfläche arbeitet mit den Stamm-IDs der Original-Datenbank. Vereine, die eine Welt hinzufügt, bekommen dort freie IDs; bestätigen lassen sich nur deutsche Vereine.
