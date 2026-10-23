# UI-Schnittstelle · Modern-Engine

Stand: 23. Oktober 2026. **Einstieg für die Arbeit an der Modern UI.** Hier steht,
was die Modern-Engine der Oberfläche liefert, ab welcher Phase es verfügbar ist
und womit die UI bis dahin arbeitet.

## Grundsatz

- Die Modern UI (RmlUi) spricht ausschließlich mit der **`ModernSession`** (`src/modern/app/modern_session.hpp`, ab M1) und deren Projektionen. Sie liest nie direkt `GameState` und rechnet keine Spielregeln nach (Preise, Grenzen, Tabellen).
- Projektionen sind unveränderliche Momentaufnahmen mit einer **Revision**. Aktionen geben die neue Revision oder einen Fehlercode mit deutscher und englischer Meldung zurück; abgelehnte Aktionen ändern nichts.
- Identitäten sind die stabilen OBW-IDs (`club:…`, `player:…`, `comp:…`, `camp:…`), nicht Laufzeitindizes.
- Bilder werden über Asset-Schlüssel referenziert (`asset:club/<id>/crest`, `asset:player/<id>/photo`, `asset:comp/<id>/trophy`, `asset:camp/<id>/image`). Auflösung und Rückfallbilder (Silhouette, generisches Wappen) übernimmt die UI über das Asset-Verzeichnis des Datensatzes.
- Texte aus der Engine (Post, Meldungen) kommen als fertige UTF-8-Zeichenketten in der gewählten Sprache.

## Verzeichnisaufteilung

| Pfad | Inhalt | Zuständig |
|---|---|---|
| `src/modern/game/` | Saisonlogik (Abzweig von Classic) | Engine |
| `src/modern/content/` | Laden der Modern-Welt, Spielstände | Engine |
| `src/modern/app/` | `ModernSession`, Projektionen, Aktionen | Engine |
| `src/modern/ui/` | RmlUi-Fenster, Ansichten, Modell der Oberfläche | UI |

Der bestehende POC (`modern::Model` mit `Snapshot`, Branch `assist/codex-0.2-modern-poc`) kann weitergeführt werden. Er hängt künftig an der `ModernSession` statt an der Classic-`CareerSession`. Wird der POC übernommen, wandert er nach `src/modern/ui/`.

## Projektionen und Aktionen

| Bereich | Projektion | Aktionen | Ab Phase |
|---|---|---|---|
| Sitzung | `SessionInfo`: Datensatz-Titel, Datenrevision, Sprache, Währung | Neue Karriere, Laden, Speichern | M3 (Laden/Speichern M6) |
| Karrierestart | `ClubChoice[]`: nur deutsche Vereine in 0.3, mit Liga, Name, Kurzname, Wappen | Manager anlegen, Verein wählen | M3 |
| Statusleiste | `DeskStatus`: Verein, Manager, Termin bzw. Saisonvorbereitung, Saison, Guthaben | – | M3 |
| Ligen | `LeagueInfo[]`: ID, Name, Land, Stufe, Anzahl Vereine (12–20), Spieltage | – | M2 |
| Tabelle | `LeagueTable`: bis 20 Zeilen mit Verein, Spiele, S/U/N, Tore, Punkte, Platzierung | – | M2 |
| Spielplan | `Fixtures`: je Liga und Spieltag die Paarungen und Ergebnisse | – | M2 |
| Kader | `Squad`: bis 24 Plätze; je Spieler ID, Vorname, Nachname, Position, Alter, Werte, Vertrag, Porträt | Aufstellung tauschen | M3 |
| Spieler | `PlayerCard`: Detailwerte, Marktwert, Gehalt, Statistik | – | M3 |
| Trainingslager | siehe unten | Schwerpunkte setzen, buchen | M4 |
| Post | `Mail[]`: Datum, Absender, Betreff, Text | Gelesen markieren | M3 |
| Spieltag | Fortschritt und Ereignisse über den bestehenden Match-Kern | Weiter, Eingriffe | M3 |

Alles, was hier noch fehlt (Transfers, Finanzen, Stadion, Umfeld und weitere
Menüs), folgt nach demselben Muster. Jedes neue Menü der UI bekommt in diesem
Dokument eine Zeile, sobald seine Projektion festgelegt ist.

## Trainingslager

Projektion `TrainingCampView`:

- `camps[]`: ID, Region (`de`, `eu`, `me`), Name, Ort, Land, Position, vier Qualitätswerte (0–6), Basispreis, Bildschlüssel,
- `price`: Preis für den aktuellen Kader (Basispreis + 5000 × Spieler) je Lager,
- `stays_this_season` (0–3), `booked_this_week`, `affordable` je Lager,
- `focus`: Kondition, Technik, Spiel (0–15) und die abgeleitete Erholung.

Aktionen:

- `set_camp_focus(kondition, technik, spiel)`: Fehler, wenn ein Wert außerhalb 0–15 liegt.
- `book_camp(camp_id)`: Fehler `camp_week_used`, `camp_season_limit`, `camp_unaffordable` oder `camp_unknown`; bei Erfolg bucht die Engine den Preis, wendet die Wirkung an und liefert die neue Revision.

Die bisherige POC-Vormerkung bleibt ein reiner UI-Zustand; erst `book_camp`
löst eine Buchung aus. Regeln und 6-Sterne-Wirkung: [Trainingslager](trainingslager.md).

## Bis die Engine liefert

- Die UI arbeitet mit dem POC-Zustand oder mit Beispieldaten in genau den oben genannten Formen. Beispieldaten sind fiktiv und liegen bei den UI-Tests, nicht im Datensatz.
- Weicht eine Projektion von dieser Beschreibung ab, wird zuerst dieses Dokument angepasst, dann der Code.
- Fragen zur Schnittstelle als GitHub-Issue mit dem Label `modern-engine`.
