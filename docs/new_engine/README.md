# Modern-Engine · Plan und Stand für 0.3

Stand: 23. Oktober 2026. Dieses Verzeichnis beschreibt, wie die Modern-Engine
entsteht, was bereits umgesetzt ist und wo die neue Oberfläche ansetzen kann.
Grundlage ist der [Engine-Split](../ENGINE_SPLIT.md); die Oberfläche ist in
[new_ui](../new_ui/README.md) beschrieben.

| Dokument | Inhalt |
|---|---|
| [UI-Schnittstelle](ui-schnittstelle.md) | **Einstieg für die UI:** was die Modern-Session liefert und ab wann |
| [Phasen](phasen.md) | Arbeitsschritte M0–M7 mit Abnahmekriterien |
| [Befund](befund.md) | Feste Classic-Größen im Code, die Modern auflösen muss |
| [Trainingslager](trainingslager.md) | Katalog, Preis, Wirkung und die neue 6-Sterne-Stufe |
| [Datenformat](datenformat.md) | **Formatänderungen für Engine und Editor** (F1–F8) |
| [Agentenregeln](AGENTS.md) | Arbeitsregeln für diesen Bereich |

## Ziel

Modern ist **BMH im Kern**, aber auf einem eigenen, wachsenden Datensatz:
anfangs dieselben Regeln wie Classic, mit der Zeit neue Ligen und später
angepasste Wettbewerbe (etwa eine Gruppenphase im Landesmeister-Wettbewerb).
Bahnbrechende neue Spielsysteme sind nicht geplant. Die Modern-Engine lädt
**nur Modern-Datensätze**; jede Erweiterung von Engine und Datensatz geschieht
gemeinsam.

Der erste Modern-Datensatz (privat, Datenrevision 1.3) bringt gegenüber Classic:

- hochauflösende Wappen und Spielerporträts (Darstellung: UI),
- Groß- und Kleinschreibung sowie getrennte Vor- und Nachnamen,
- vier internationale Ligen mit 20, 20, 20 und 18 statt je 12 Vereinen,
- 20 Spieler pro internationalem Verein wie bei den deutschen Vereinen (insgesamt 3414 Spieler, 449 Teams, davon 168 Ligavereine),
- 18 Trainingslager in drei Regionen mit Qualitätswerten bis 6 Sterne.

Für 0.3 gilt außerdem:

- Der menschliche Manager übernimmt nur deutsche Vereine; die internationalen Ligen laufen voll simuliert mit.
- Internationale Wettbewerbe bleiben vorerst wie in Classic, auch wenn sie veraltet sind.
- Balance-Anpassungen an Spielerdaten und Wirtschaft sind ausdrücklich erlaubt und werden dokumentiert.

## Architektur-Entscheidung

**Modern ist ein eigener Saison-Kern, abgezweigt von Classic; Match-Kern und Formate bleiben gemeinsam.**

- **Classic bleibt unverändert** und bit-identisch zu seiner Regression.
- **Gemeinsam:** `src/formats`, `src/game/state.*` (mit einem `WorldLayout`, Standard = Classic), Zufallsgeneratoren und der Match-Kern (`match_engine`, `match`, `reality_*`, `rating`, `lineup`).
- **Abgezweigt nach `src/modern/`:** die Saisonlogik (Tagesablauf, Woche, Monat, Saison, Pokale, Spieltagsvorbereitung, Schreibtischaktionen, Karriere, Nationalteams, Transfers, Spielpläne) und eine eigene `ModernSession`. Nur dort werden feste Größen durch das Layout ersetzt und neue Regeln eingeführt.
- Damit kann sich Modern Schritt für Schritt von Classic entfernen, ohne Classic zu gefährden. Fehler im gemeinsamen Match-Kern wirken auf beide; Fehler in der Saisonlogik werden bei Bedarf in beide Kerne übernommen.
- Der typisierte Match-Vertrag aus dem Engine-Split bleibt das langfristige Ziel, ist aber keine Voraussetzung für 0.3.

## Phasen und Status

| Phase | Inhalt | Status |
|---|---|---|
| M0 | Classic-Referenz festhalten, Datensatz lokal bereitstellen | erledigt (Classic-Regressionen des öffentlichen Codes: Issue #14) |
| M1 | `WorldLayout`, Abzweig `src/modern/` | Abzweig und Layout erledigt; vereinsbezogene Speicherblöcke folgen mit M3 |
| M2 | Spielpläne je Liga, Kalender mit 38 Spieltagen | Engine-Teil erledigt (Tabelle je Ligagröße, Ligatermine, Aussetzen); Test mit echten Daten in M3 |
| M3 | Modern-Weltlader, Namen (Vor-/Nachname, UTF-8), Asset-Schlüssel | offen |
| M4 | Trainingslager: 18 Lager, drei Regionen, 6-Sterne-Wirkung | offen |
| M5 | Datenrevision 1.4 des Modern-Datensatzes (privat), Wirtschaftsprüfung | offen |
| M6 | Modern-Spielstände (`modern-career-v1`), Projektionen für die UI | offen |
| M7 | Balance-Läufe und Anpassungen | offen |

Details und Abnahmekriterien stehen in [Phasen](phasen.md).
