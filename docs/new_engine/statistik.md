# Spielstatistik (Engine-Abfragen)

Datenquelle für den Button **Spielstatistik** der Modern-Oberfläche (nicht für den Almanach). Die Abfragen werten den [Almanach](almanach.md) der laufenden Karriere aus; der Almanach wird damit zur allgemeinen Datenquelle für Statistiken. Weitere Auswertungen kommen nach demselben Muster dazu.

Code: `src/modern/content/statistics.hpp/.cpp`, Aufruf über `ModernSession::read_statistics(request)`. Nur lesend, keine Wirkung auf Spielstand oder Zufallszahlen.

## Anfragen

| Anfrage | Antwort (zusätzlich zu `ok: true`) |
|---|---|
| `{kind: "competitions"}` | `competitions: [{id, name, type, seasons: ["2026/27", …]}]` – alle Ligen und Pokale mit Ergebnissen, Ligen zuerst |
| `{kind: "winners", competition?}` | `competitions: [{id, name, type, winners: [{season, year, edition, date, winner, runner_up}]}]` – Sieger nach Jahren; ohne `competition` alle Wettbewerbe |
| `{kind: "player_stats", competition, season?, sort?, limit?}` | `players: [{rank, player, club, appearances, goals, assists, yellow, second_yellow, red}]` |

- `competition`: Ligen mit der Liga-ID der Welt (z. B. `comp:de-1`), Pokale als `cup:<Pokal-ID>`, das Hallen-Masters als `cup:11:masters`.
- `season`: `"2026/27"`; fehlt oder `null` = alle Saisons (ewige Liste).
- `sort`: `goals` (Standard), `assists`, `yellow`, `red` (Rot und Gelb-Rot zusammen). Gleichstand: weniger Einsätze zuerst, dann Name. Gleiche Werte haben denselben Rang.
- `limit`: Anzahl Zeilen, Standard 50, `0` = alle. Es erscheinen nur Spieler mit einem Wert > 0 im Sortierkriterium.
- `club`: der Verein des letzten Spiels des Spielers in dieser Auswahl.
- `appearances`: Startelf plus Einwechslungen (nur Ligavereine haben Aufstellungen).
- `winner`/`runner_up`: `{id, name}`; `runner_up` ist `null`, wenn es keinen gibt (Masters, Länderturniere ohne Finale). `edition` zählt mehrere Austragungen einer Saison (Hallenturniere).
- Fehler: `{ok: false, error}` mit `invalid_statistics_request` (Form, Art, Wert) oder `statistics_unavailable`.

## Zuordnung

- Nachgeholte Ligaspiele (intern Wettbewerb 9 „Verlegte Spiele“) zählen zu ihrer Liga und ihrem ursprünglichen Spieltag.
- Freundschaftsspiele (Wettbewerb 10) stehen in `competitions`, haben aber keine Sieger.
- Wettbewerbsnamen: Ligen aus der Welt, Pokale aus den Texten der Ausgabe; die einzelnen Hallenturniere heißen „Hallenturnier“, das Masters „Hallen-Masters“.

## Prüfen

`MODERN_STATS=1 modern_season_sim <welt.obw> 60 0 1` zeigt am Laufende Beispielanfragen und ihre Antworten.
