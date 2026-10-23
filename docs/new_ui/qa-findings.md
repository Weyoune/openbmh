# Offene native UI-Befunde

Stand: 23. Oktober 2026. Diese Liste betrifft die ausdrücklich aktivierte,
noch unfertige Modern-Vorschau. Sie ist keine Abnahme der kompletten UI.
Verbindliche Quelle ist das private HTML-Mockup; echte Rendererbilder und
Bedienungsnachweise bleiben im privaten QA-Paket.

| Bereich | Priorität | Noch offen |
|---|---|---|
| Tabellen/Stärken | P1 | Überschrift-Knöpfe sind vorhanden, Sortieren und Richtungshinweis fehlen. |
| Tabellen | P1 | „Gesamt“ muss Land und Filter wie HTML zurücksetzen; derzeit kein vollständiger Rücksetzpfad. |
| Vereinsdetails | P1 | Tabellenklick zeigt nur einen Hinweis; Vereinsprofil, Kader- und Spielerprofilpfad fehlen. |
| Tabellen | P2 | „Letzte 8“, Qualifikationsfarben/-legende und eigene Vereinsmarkierung ergänzen. |
| Stärken | P2 | Spaltenkopf/Zeilenhöhe an die eigene HTML-Ansicht angleichen; keine Tabellenplatznummern. |
| Gemeinsame Dropdowns | P2 | Markierungsspalte, Popupzeilenhöhe, Hintergrund/Akzent und Platzierung an der Glasfenstergrenze angleichen. |
| Länder-Picker | P2 | Bewusste Neuwahl derselben Nation setzt in HTML auf deren erste Liga zurück; native Selects behandeln unveränderte Werte noch als unverändert. Abbrechen erhält dagegen ausdrücklich die Unterliga. |
| Desktop/Rahmen | P2 | Vollständiger Bild-, Widgetinhalt- und Glasvergleich noch ausstehend; aktuelle Teilprüfungen betreffen einzelne Komponenten. |
| Weitere Untermenüs | Offen | Noch nicht portierte Routen im Gesamtkatalog bleiben ausdrücklich `UNIMPLEMENTED`. |

Bereits korrigiert wurden vertikale Button-/Dropdownbeschriftungen,
Pfeilausrichtung, Escape/Fokus nach Auswahl, Training-Scrollposition,
Budgetumbrüche, lange Spielernamen neben Stärkewerten sowie Taktik-Schatten.
Das Verwerfen offener Auswahllisten und die Tastaturbedienung werden bei
gemeinsamen Controländerungen zusätzlich in mehreren Menüs nachgeprüft.

Die erste Gesamterfassung enthält 36 Routen, davon zwölf native Aufnahmen
und 24 ausstehende Ports. Diese historische Matrix bleibt unverändert.
Der anschließend übertragene Kalender bekommt einen eigenen Vergleich;
spätere Meilensteine erfassen denselben Gesamtkatalog erneut.

Siehe [QA-Ablauf](menu-qa.md), [Typografie](typography-qa.md) und
[aktuellen nativen Stand](NATIVE_STATUS.md).
