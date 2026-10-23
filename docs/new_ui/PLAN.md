# Native Modern UI bis zum Anstoß

Stand: 23. Oktober 2026. **Abgestimmter Gesamtumfang; native Umsetzung begonnen.**
Der [native Umsetzungsstand](NATIVE_STATUS.md) unterscheidet den bereits
bedienbaren Unterbau von den noch ausstehenden Menüübertragungen.
Diese Seite hält die Entscheidungen aus dem UI-Plan fest. Sie ersetzt keine
Implementierungs- oder Abnahmebelege. Die Engine entsteht parallel bei Claude.

## Einstieg und Darstellung

- Die native Oberfläche entsteht in OpenBMH mit SDL2/RmlUI auf `0.3`.
- Gestalterische Referenz ist das aktuelle HTML-Mockup im privaten Repository
  `bmh_4k_ui`, einschließlich aller drei Themes und der jüngsten QA-Korrekturen.
  Es werden keine vereinfachten Ersatzansichten oder neue Layouts eingeführt.
- Das gemeinsame Hauptmenü und seine Mod-Verwaltung folgen dem Mockup. Die
  aktive Mod entscheidet: Classic startet die alte UI, Modern die neue Demo.
- Die erste vollständige Fassung ist deutsch. Übersetzungsschlüssel werden
  vorbereitet; Englisch bleibt bis zur vollständigen Übersetzung deaktiviert.
- Das Paket umfasst Hauptmenü, Grundeinstellungen, Manager-/Vereinswahl,
  Saisonvorbereitung, den bedienbaren Desktop und sämtliche vor dem Anstoß
  erreichbaren Untermenüs. Anstoß bleibt sichtbar und positionierbar, hat aber
  weder per Maus noch per Tastatur eine Startaktion. Matchansicht, Simulation
  und Post-Match gehören nicht zu diesem Paket.

## Daten und Zuständigkeit

- Diamond 2026/27 Modern liefert Namen, Kader, Werte, Ligen und HQ-Weltbilder
  schreibgeschützt aus dem OBW-Paket. Die Demo verändert den Datensatz nicht.
- Veränderliche Zustände wie Kontostände, Tabellen, Nachrichten und Abläufe
  werden von einem getrennten Demo-Datenanbieter geliefert. UI-Aktionen dürfen
  Demo-Eingaben speichern, bilden aber keine Spielregeln oder Saisonlogik nach.
- Der Datenanbieter gibt unveränderliche Projektionen mit Revision heraus und
  nimmt Aktionen entgegen. Die spätere Modern-Session wird über einen Adapter
  angebunden; Fenster und Renderer lesen nie direkt `GameState`.
- GUI-Motive sind ein eigener Ressourcenbestand. Trainingslagerbilder und
  Karten kommen aus der GUI, Vereinswappen und Spielerfotos aus der Welt.
  Einzelheiten: [GUI-/Welt-Trennung](assets.md).
- Demo-Spielstände bleiben von echten `.obs`-Karrieren getrennt. Ihre
  Speichern-/Laden-Dialoge entsprechen dem Mockup; sie bestätigen keine
  Engine-Spielbarkeit.

## Spielwelt und Auswahl

- Das Ligaset folgt Diamond/BMH: fünf deutsche Ligen mit je 18 Vereinen;
  LaLiga, Premier League und Serie A mit je 20 sowie Ligue 1 mit 18 Vereinen.
  Es werden keine zusätzlichen Ligen aus dem großen Season-Paket übernommen.
- Wettbewerbe behalten ihre BMH-Struktur. Aktuelle Champions-/Europa-/Conference-
  League-Regeln werden nicht aus der Darstellung des HTML-Demos abgeleitet.
- Manager können nur deutsche Vereine übernehmen. Ausländische Vereine bleiben
  sichtbar; ihre Logos sind abgeblendet und mit einem Schloss markiert. Die
  Übernahme ist auch per Doppelklick oder Tastatur gesperrt.
- Tabellen, Kader und Spielpläne der ausländischen Ligen bleiben einsehbar.
- Die Trainingslageransicht unterstützt Deutschland, Europa und Nahost mit
  den abgestimmten 18 Lagern. Bis die Engine-Projektion vorliegt, liefert ein
  separater Demo-Katalog diese Daten. Die acht alten Lager in Diamond 1.3 werden
  nicht als vollständiger Modern-Katalog ausgegeben.

## Übertragung und Abnahme

1. Gemeinsamer Unterbau: Welt-/GUI-Ressourcen, Datenanbieter, Komponenten,
   Mod-Routing und drei Themes.
2. Durchgehender Weg vom Hauptmenü über Managerwahl und Saisonvorbereitung
   zum Desktop.
3. Vollständiger Widgetkatalog, Raster/freies Verschieben, Fixierung,
   Rückgängig, Ansichten und alle vereinbarten Untermenüs, einschließlich
   [kompaktem Eintrittspreis-Schieberegler](desktop/entry-price/README.md).
4. Vergleichsbilder mit zugehörigen Demo-Zuständen und zusammenhängender
   GUI-Abnahme; Testpakete für Windows x64, Linux x64 und macOS ARM64.

Die Untermenüs übernehmen den gemeinsamen 4:3-Rahmen mit 2720 × 2040 im
logischen 3840 × 2160-Desktop. Abnahme umfasst 720p, Full HD, 4K, HiDPI und
Ultrawide, lesbare Eingaben und Dropdowns, Bildproportionen und Inhalte innerhalb
ihrer Flächen. Jede Ansicht wird mit der HTML-Referenz verglichen; gemeinsame
Bedienelemente und repräsentative Fenster zusätzlich in allen drei Themes.
Regressionen laufen an Meilensteinen und bei Änderungen gemeinsamer Bausteine.

Classic-Import/-Start bleibt eine eigene Gegenprobe. Private Daten, Motive und
referenzabhängige QA-Belege bleiben im privaten Bestand; öffentliche Tests
verwenden synthetische Welten. Detailverträge werden vor oder mit der jeweiligen
Implementierung in [new_engine](../new_engine/ui-schnittstelle.md) nachgeführt.
