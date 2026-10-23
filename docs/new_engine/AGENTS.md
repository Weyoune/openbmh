# Modern-Engine · Regeln für Agenten

- Modern-Engine-Arbeit findet auf dem Branch `0.3` statt; Dokumentation liegt in `docs/new_engine/`.
- Code der Modern-Saisonlogik gehört ausschließlich nach `src/modern/` (Namespace `bmh::modern`).
- **Classic bleibt unverändert.** Gemeinsame Teile (`src/formats`, `src/game/state.*`, Zufall, Match-Kern) dürfen nur so geändert werden, dass Classic mit seinem Layout bit-identisch bleibt. Nach jeder solchen Änderung laufen Classic-Tests und Regression.
- Die Modern-Engine lädt nur Modern-Datensätze (`runtime:modern-v1`); Classic lädt keine Modern-Datensätze.
- UI-Code (RmlUi) liest Spielzustand nur über die Modern-Session und ihre Projektionen ([UI-Schnittstelle](ui-schnittstelle.md)), nie direkt aus `GameState`.
- Regeländerungen gegenüber Classic werden in [DIFFERENCES](../DIFFERENCES.md) als Modern-Abweichung festgehalten.
- Jede Änderung am OBW-Format (neue Felder, Wertebereiche, Prüfregeln) wird vor oder mit dem Code in [Datenformat](datenformat.md) eingetragen, mit einer Zeile „Editor“ für die nötige Editor-Anpassung.
- Mit jeder Phase den Status in [README](README.md) und die Details in [Phasen](phasen.md) nachführen.
- Private Datensätze, echte Namen, Wappen und Fotos gehören nicht in dieses Repository; Tests nutzen synthetische Welten.
- Diese Notizen sind deutsch; Code, Kommentare und Commits englisch.

## Arbeiten im Abzweig (seit M1)

- Der Abzweig ist mit Skripten aus Classic erzeugt worden; **seitdem wird `src/modern/` von Hand gepflegt**. Classic-Änderungen werden nicht automatisch übernommen.
- Modern-Saisonfunktionen nehmen `ModernState` (`src/modern/game/modern_state.hpp`), eine Ableitung von `GameState`. Modern-Code ruft sie **qualifiziert** auf: `modern::name(...)`. Ein fehlender Modern-Aufruf wird so zum Übersetzungsfehler statt zum stillen Aufruf der Classic-Version.
- Datentypen sind zwischen Classic und Modern gemeinsam: Ein Modern-Header bindet den Classic-Header für die Typen ein und deklariert nur die Modern-Funktionen. Ein eigener Modern-Typ entsteht erst, wenn Modern ihn ändern muss.
- Gemeinsame Callback-Typen übergeben `GameState&`; in Modern wird dort ausdrücklich nach `ModernState&` gewandelt.
- Feste Classic-Größen (138, 168, 0x165, Pool-Slots) kommen in Modern aus dem `WorldLayout` (`league_clubs()`, `first_foreign_cup()`, `no_team()`, `transfer_list_slot()` …). Offsets im Speicherabbild (etwa `rec + 0xa8`) bleiben.
- Prüfungen nach jeder Änderung:
  - `tools/modern/check_fork_calls.sh build` und `LIBRARY=bmh_modern_session tools/modern/check_fork_calls.sh build`: Modern ruft keine Classic-Saisonfunktion.
  - `season_sim` und `modern_season_sim` mit Classic-Daten und gleichen Argumenten liefern dieselbe Ausgabe, solange Modern keine Regel bewusst ändert.
  - Classic-Tests und Classic-Regression unverändert.
