# Befund · Feste Classic-Größen im Code

Stand: 23. Oktober 2026, Branch `0.3`. Diese Liste zeigt, warum größere
internationale Ligen und Kader kein reiner Konstantentausch sind. Zeilenangaben
können sich verschieben; maßgeblich sind die genannten Funktionen.

## Kaderplätze

- `src/game/state.cpp` (`kBigSquadTeams = 90`, `kTeamsWithSquads = 168`, `squad_range`): Teams 0–89 haben 24 Kaderplätze, Teams 90–137 (Auslandsligen) 16 Plätze ab `team * 16 + 720`; alle anderen keine.
- Hinter den Kadern (Platz 2928 = 90 × 24 + 48 × 16) folgen Pools: Transferliste (44 Plätze), vereinslose Spieler (80), Auslandsangebote (20), Bereich ohne Verein ab 3072, insgesamt 3150 Plätze.
- Diese Grenzen stehen als Zahlen in `month.cpp`, `weekly.cpp`, `poach.cpp`, `national.cpp`, `club_player_detail.cpp`, `career.cpp`, `matchday_setup.cpp`, `season.cpp`, `import/obw.cpp` und vielfach in `app/career_session.cpp`.
- Auslandskader sind auf 15 Spieler begrenzt (Käufersuche in `weekly.cpp`), der Mindestkader liegt bei 14 (`poach.cpp`).

## Vereins-IDs

- 0–137 Ligavereine (90 deutsch, 48 international), 138–167 Amateure, 168–356 Pokal-Ausländer (27 Nationen × 7), 357 = `kNoTeam` (`state.hpp`), insgesamt 419.
- Abgeleitete Prüfungen: `season.cpp` (Amateur-Pool, Gesamtzahl), `match.cpp` (Pokal-Ausländer in Siebenerblöcken), `rating.cpp` und `other_games.cpp` (`team - 168`), `match_engine.cpp`, Wappenprüfungen in `app/controller.cpp`, `app/main.cpp`, `app/career_session.cpp`.
- 30 zusätzliche Ligavereine überschneiden sich mit dem Bereich der Pokal-Ausländer und mit `kNoTeam`.

## Ligen, Spielpläne, Kalender

- `set_league_sizes` (`schedule.cpp`) verlangt in Summe genau 138 Ligavereine.
- Alle Auslandsligen nutzen die Spielplantabelle von Liga 5; eine Liga mit 18 neben Ligen mit 20 Vereinen würde falsche Teams paaren.
- `set_fixture_tables` kennt nur Tabellen für 4, 6, 12, 16, 18 und 20 Vereine.
- Die Auslandsligen teilen sich ein Kalenderbit; der Classic-Kalender hat dort Termine für 22 Runden, nicht für 38.
- Unkritisch: `kResultsSize` reicht für 9 Ligen mit 20 Vereinen, Tabellen fassen 20 Einträge.

## Speicherabbild und Spielstand

- Mehrere Blöcke sind pro Verein fest dimensioniert (`formats/savestate.cpp`, `game/memory.hpp`): Spielhistorie der Manager (5 × 168 × 10), Kurznamen-Offsets (168), Trikotwahl (419), Stärke der Pokal-Ausländer (251), Saisondaten der Vereine (168 × 74 Byte), Ranghistorie (138 × 26), Zusatzbytes (168).
- Blöcke liegen an festen Adressen hintereinander und können nicht wachsen.
- `game/save.cpp` und `game/career.cpp` verwenden die Standardzählung (2574 Spieler, 419 Vereine).
- Importer und Classic-Spielstände prüfen genau 419 Vereine, 2574 Spieler und 168 Kader.

## Unkritisch

- Spieler-IDs sind in Kaderplätzen und Angeboten 12 Bit breit (bis 4094), Vereins-IDs in Leihen 10 Bit (bis 1023).
- Typisierte Vektoren (`players`, `squad_slots`, `team_morale`, `club_table` usw.) können wachsen.
- Namenspools nutzen 16-Bit-Offsets (64 KB); Modern führt Namen deshalb getrennt als UTF-8.

## Laden

- Pakete mit `editor:world-v1` werden heute bewusst abgewiesen (`content/package.cpp`, `import/obw.cpp`). Ein typisiertes Weltmodell gibt es nicht; der Editor arbeitet JSON-basiert (`src/editor/world.*`, `ArchiveStore`).

## Wirtschaft

- Computer-Vereine besitzen kein Geld; Buchungen (`book_money`) betreffen nur menschliche Manager.
- Startfinanzen entstehen beim Zuweisen eines Managers aus Spielstufe und Startliga (`career.cpp`). Neue Vereine, die kein Mensch übernimmt, brauchen deshalb keine Startfinanzen.
- Marktwert und Gehalt folgen aus Technik, Kondition, Alter, Position und Erfahrung (`market_value` in `month.cpp`).
