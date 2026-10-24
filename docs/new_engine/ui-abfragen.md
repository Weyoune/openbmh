# Abfragen für die neue Oberfläche (M6)

Michaels Entscheidung: Die neue Oberfläche wird **je Menü** an die Modern-Engine angebunden. Jedes Menü bekommt feste Abfragen (und später Aktionen), die die Engine baut und hier dokumentiert; Codex stellt Menü für Menü vom Demo auf die Abfrage um. Die Demo bleibt nutzbar.

**Reihenfolge (Michael):** Zuerst wird die GUI mit Demodaten fertig gebaut. Danach legt Codex je Menü fest, welche Daten es braucht, und die Engine liefert die passende Abfrage. Später soll alles auf ein durchgehendes JSON-Format umgestellt werden; die Antworten verwenden deshalb schon jetzt überall dieselben Teilobjekte, damit sie sich zu einem gemeinsamen JSON-Zustand zusammensetzen lassen.

## Vertrag

- Einstieg: `ModernSession::read(request)`, Anfrage und Antwort sind JSON (`cli::Json`). Die Antwort ist `{ok: true, …}` oder `{ok: false, error}`.
- Nur lesend: keine Zufallszahlen, keine Änderung am Spiel, keine neue Revision.
- Abfragen für den Manager am Schreibtisch (`ActiveManager`).
- Gemeinsame Teilobjekte (in allen Antworten gleich):
  - `club`: `{id, name, short}` (Welt-ID, UTF-8)
  - `player`: `{id, name}` (stabile Spieler-ID, Vor- und Nachname)
  - `league`: `{index, id, name}`
  - Datum ISO `YYYY-MM-DD`, Saison `"2026/27"`, Geld in Euro als ganze Zahl.

## Grundabfragen (Stand 24.10.2026)

| Anfrage | Antwort |
|---|---|
| `{kind: "status"}` | `manager {index, name}`, `club` (oder `null`, z. B. am Bundestrainer-Schreibtisch), `league`, `place`, `date`, `season`, `money` |
| `{kind: "leagues"}` | `leagues: [{index, id, name, clubs, matchdays, played}]` |
| `{kind: "table", league}` | `league`, `played`, `rows: [{rank, club, played, won, drawn, lost, goals_for, goals_against, points}]` (aktueller Stand, mit Punktabzug) |
| `{kind: "fixtures", league, matchday?}` | `league`, `matchday`, `matches: [{home, away, result: [h, a] \| null, status: open \| played \| postponed}]`; ohne `matchday` der nächste |
| `{kind: "toto"}` | Tippschein des Spieltags und eigene Spalten, siehe [Toto](toto.md) |
| `{kind: "squad", club?}` | `club`, `players: [{slot, player, nation, position (gk/def/mid/fwd), age, technique, condition, form, strength, skills {teamplay, speed, shooting, passing, tackling, free_kick, corner}, lineup (start/bench/none), injured_weeks, banned_weeks}]`; ohne `club` der eigene Verein, sonst Welt-ID oder Laufzeitindex |
| `competitions`, `winners`, `player_stats` | siehe [Spielstatistik](statistik.md) |

`league` darf der Ligaindex (0–8) oder die Liga-ID der Welt sein. Fehler: `invalid_request`, `invalid_manager`, `invalid_league`, `invalid_matchday`, `invalid_club`, `unavailable`.

Code: `src/modern/content/projections.hpp/.cpp`. Prüfen: `MODERN_STATS=1 modern_season_sim <welt.obw> 20 0 1` zeigt Beispielanfragen und Antworten.

## Nächste Schritte

- Je Menü der fertigen GUI: Codex nennt die benötigten Felder (am einfachsten als Beispiel-JSON aus der Demo), die Engine baut die Abfrage, beide Seiten testen gegen dasselbe Beispiel.
- Aktionen (`ModernSession::apply`) nach demselben Muster, mit Revision wie bei den bestehenden Desk-Aktionen.
