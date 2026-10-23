# Modern-Engine · Regeln für Agenten

- Modern-Engine-Arbeit findet auf dem Branch `0.3` statt; Dokumentation liegt in `docs/new_engine/`.
- Code der Modern-Saisonlogik gehört ausschließlich nach `src/modern/` (Namespace `bmh::modern`).
- **Classic bleibt unverändert.** Gemeinsame Teile (`src/formats`, `src/game/state.*`, Zufall, Match-Kern) dürfen nur so geändert werden, dass Classic mit seinem Layout bit-identisch bleibt. Nach jeder solchen Änderung laufen Classic-Tests und Regression.
- Die Modern-Engine lädt nur Modern-Datensätze (`runtime:modern-v1`); Classic lädt keine Modern-Datensätze.
- UI-Code (RmlUi) liest Spielzustand nur über die Modern-Session und ihre Projektionen ([UI-Schnittstelle](ui-schnittstelle.md)), nie direkt aus `GameState`.
- Regeländerungen gegenüber Classic werden in [DIFFERENCES](../DIFFERENCES.md) als Modern-Abweichung festgehalten.
- Mit jeder Phase den Status in [README](README.md) und die Details in [Phasen](phasen.md) nachführen.
- Private Datensätze, echte Namen, Wappen und Fotos gehören nicht in dieses Repository; Tests nutzen synthetische Welten.
- Diese Notizen sind deutsch; Code, Kommentare und Commits englisch.
