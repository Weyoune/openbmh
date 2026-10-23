# Offene native UI-Befunde

Stand: 23. Oktober 2026. Diese Liste betrifft die ausdrücklich aktivierte,
noch unfertige Modern-Vorschau. Sie ist keine Abnahme der kompletten UI.
Verbindliche Quelle ist das private HTML-Mockup; echte Rendererbilder und
Bedienungsnachweise bleiben im privaten QA-Paket.

| Bereich | Priorität | Noch offen |
|---|---|---|
| Weitere native Dropdowns | P2 | Die neuen HTML-nahen Footerpicker gelten zunächst für Tabellen, Stärken, Ligaspiele und übrige Spiele. Kalender/Manager und weitere Auswahlfelder verwenden noch den bisherigen Select-Unterbau. |
| Länder-Picker außerhalb des Berichtpakets | P2 | Bewusste Neuwahl derselben Nation setzt in HTML auf deren erste Liga zurück. In den vier Berichtmenüs umgesetzt; Kalender/Manager noch gesondert angleichen. Abbrechen muss die Unterliga erhalten. |
| Desktop/Rahmen | P2 | Vollständiger Bild-, Widgetinhalt- und Glasvergleich noch ausstehend; aktuelle Teilprüfungen betreffen einzelne Komponenten. |
| Weitere Untermenüs | Offen | Noch nicht portierte Routen im Gesamtkatalog bleiben ausdrücklich `UNIMPLEMENTED`. |

Mit dem [Berichtpaket](desktop/reports/README.md) sind Sortierung und
Richtungshinweis, „Gesamt“, Formspalte, Qualifikationsfarben/-legende,
eigene Vereinsmarkierung und die abweichende Stärkentabelle umgesetzt.
Vereinsprofil, Kader und Spielerprofil ersetzen den bisherigen Hinweistext.
Die neuen Footerpicker übernehmen Markierungsspalte, Popupzeilen,
Themes, Platzierung und Tastatursteuerung der HTML-Vorlage.

Bereits korrigiert wurden vertikale Button-/Dropdownbeschriftungen,
Pfeilausrichtung, Escape/Fokus nach Auswahl, Training-Scrollposition,
Budgetumbrüche, lange Spielernamen neben Stärkewerten sowie Taktik-Schatten.
Das Verwerfen offener Auswahllisten und die Tastaturbedienung werden bei
gemeinsamen Controländerungen zusätzlich in mehreren Menüs nachgeprüft.

Die erste Gesamterfassung enthält 36 Routen, davon zwölf native Aufnahmen
und 24 ausstehende Ports. Diese historische Matrix bleibt unverändert.
Der anschließend übertragene Kalender bekommt einen eigenen Vergleich;
der aktuelle Runner erfasst 15 native Routen und 21 offene Ports.
Spätere Meilensteine erfassen denselben Gesamtkatalog erneut.

Siehe [QA-Ablauf](menu-qa.md), [Typografie](typography-qa.md) und
[aktuellen nativen Stand](NATIVE_STATUS.md).
