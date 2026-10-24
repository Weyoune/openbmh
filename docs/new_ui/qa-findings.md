# Offene native UI-Befunde

Stand: 24. Oktober 2026. Diese Liste betrifft die ausdrücklich aktivierte,
noch unfertige Modern-Vorschau. Sie ist keine Abnahme der kompletten UI.
Verbindliche Quelle ist das private HTML-Mockup; echte Rendererbilder und
Bedienungsnachweise bleiben im privaten QA-Paket.

| Bereich | Priorität | Noch offen |
|---|---|---|
| Weitere native Dropdowns | P2 | Die neuen HTML-nahen Footerpicker gelten für Tabellen, Stärken, Ligaspiele, übrige Spiele, Kalender und Freundschaftsspiele. Manager und weitere Auswahlfelder verwenden noch den bisherigen Select-Unterbau. |
| Länder-Picker außerhalb des Berichtpakets | P2 | Bewusste Neuwahl derselben Nation setzt in HTML auf deren erste Liga zurück. In Berichtmenüs, Kalender und Freundschaftsspielen umgesetzt; Manager noch gesondert angleichen. Abbrechen muss die Unterliga erhalten. |
| Desktop/Rahmen | P2 | Vollständiger Bild-, Widgetinhalt- und Glasvergleich noch ausstehend; aktuelle Teilprüfungen betreffen einzelne Komponenten. |
| Weitere Untermenüs | Offen | Noch nicht portierte Routen im Gesamtkatalog bleiben ausdrücklich `UNIMPLEMENTED`. |

Mit dem [Berichtpaket](desktop/reports/README.md) sind Sortierung und
Richtungshinweis, „Gesamt“, Formspalte, Qualifikationsfarben/-legende,
eigene Vereinsmarkierung und die abweichende Stärkentabelle umgesetzt.
Vereinsprofil, Kader und Spielerprofil ersetzen den bisherigen Hinweistext.
Die neuen Footerpicker übernehmen Markierungsspalte, Popupzeilen,
Themes, Platzierung und Tastatursteuerung der HTML-Vorlage.

Das Planungspaket ergänzt Verlegen, Freundschaftsspiele und Hallentabellen.
Beim Verlassen eines Menüs werden lokale Dialog- und Pickerzustände auch
beim Wechsel über die OS-Beenden-/Speichern-Rückfrage verworfen.

Bereits korrigiert wurden vertikale Button-/Dropdownbeschriftungen,
Pfeilausrichtung, Escape/Fokus nach Auswahl, Training-Scrollposition,
Budgetumbrüche, lange Spielernamen neben Stärkewerten sowie Taktik-Schatten.
Das Verwerfen offener Auswahllisten und die Tastaturbedienung werden bei
gemeinsamen Controländerungen zusätzlich in mehreren Menüs nachgeprüft.

Die erste Gesamterfassung enthält 36 Routen, davon zwölf native Aufnahmen
und 24 ausstehende Ports. Diese historische Matrix bleibt unverändert.
Der anschließend übertragene Kalender bekommt einen eigenen Vergleich;
der aktuelle Runner erfasst 22 native Routen und 15 offene Ports.
Spätere Meilensteine erfassen denselben Gesamtkatalog erneut.

Siehe [QA-Ablauf](menu-qa.md), [Typografie](typography-qa.md) und
[aktuellen nativen Stand](NATIVE_STATUS.md).

Im Finanzpaket behoben: unsichtbare Bilanzplan-Labels, unnötige Umbrüche in
Vertragsbuttons, fehlendes Bilanz-Umschaltsymbol, Bank-Laufzeittext am Pfeil,
Tastaturhinweise der Bankhalle sowie abweichende Farben des Zahleneditors.
Das immobilienspezifische HTML-Seitenverhältnis war verzerrt und wurde auf
die ursprüngliche 2×3-Atlaskachel korrigiert. Native Bilder bleiben proportional;
der HTML-Randbeschnitt von 1 % ist als kleine verbleibende Bildabweichung
im privaten Nachweis dokumentiert.
