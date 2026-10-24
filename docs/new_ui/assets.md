# GUI-Ressourcen und Weltinhalte

Stand: 23. Oktober 2026. Festlegung für die native Modern UI, noch keine
Implementierung eines neuen Ressourcenladers oder einer Paketmigration.

## Zuständigkeit

Die festen Bildmotive des HTML-Mockups gehören zur GUI. Sie werden zusammen mit
der Oberfläche ausgeliefert und müssen nicht im aktiven OBW-Paket vorhanden sein.
Das gilt insbesondere für die HD-Trainingslagerbilder, die aktuell nur im
privaten Mockup-Repository vorliegen.

| GUI-Ressourcen | Weltinhalte im OBW-Datensatz |
|---|---|
| Desktop-Icons, Widgetmotive und Bedienelemente | Vereinsnamen, Wappen und Vereinsfarben |
| Hintergrund, Themes, Fenster, Schriften und UI-Texte | Spieler, Werte, Verträge, Fotos und Jugendporträt-Auswahl |
| Trainingslagerkarten und feste Resort-Illustrationen | Trainingslager-IDs, Regionen, Lage, Namen, Qualitätswerte und Preise |
| Bank-, Stadion-, Umfeld- und andere feste Menüillustrationen | Stadionzustand, Finanzen, Personal und sonstige Spielwerte |
| Managerporträt-Auswahl der GUI | Wettbewerbsdefinitionen und zugeordnete Trophäen |

Die GUI verwendet Bildmotive unverändert aus dem abgestimmten Mockup. Dynamische
Werte, Texte, Auswahlzustände und Trefferflächen bleiben davon getrennt.

## Laden und Referenzieren

- Die GUI besitzt einen eigenen Ressourcenbestand, zunächst unter
  `assets/modern/gui/` neben den Programmressourcen. Die geplanten Theme-Pakete
  nach [OBW §9.6](../formats/OBW.md#96-rmlui-themes-obt) bleiben davon unabhängig
  als spätere Verpackung möglich; ein neuer Theme-Importer ist hier nicht vereinbart.
- Weltbilder werden über den Asset-Index des aktiven OBW-Pakets aufgelöst.
  Die bestehenden HQ-/SVG-Varianten haben Vorrang vor niedrig aufgelösten
  Classic-Varianten desselben Inhalts.
- GUI- und Weltauflösung bleiben getrennte Schnittstellen. Der GUI-Lader greift
  nicht auf `assets/classic/` oder alte KB-/VGA-Objekte der Welt zurück.
- Die Engine liefert semantische IDs und Projektionen, keine Dateipfade zu
  GUI-Bildern. Die Oberfläche ordnet den IDs ihre festen Bildmotive zu.
- Fehlende GUI-Ressourcen werden als unvollständige GUI-Installation gemeldet.
  Fehlende Spielerfotos verwenden die vorgesehene Silhouette. Ein fehlendes
  GUI-Bild macht den Weltkatalog nicht zu einem ungültigen OBW-Datensatz.

## Trainingslager

Die drei Karten und die 18 Resortmotive gehören zur GUI. Die Oberfläche ordnet
sie über `map:de`, `map:eu`, `map:me` beziehungsweise `resort:0` bis `resort:17`
zu. Die bestehende Reihenfolge wird nicht neu nummeriert. Die Karten des
Mockups haben jeweils einen Zeichenraum von 1200 × 1336; die Lagewerte einer
Welt müssen zum dokumentierten Kartenkoordinatensystem passen.

Ein Standardlager benötigt **keinen** Eintrag wie `asset:camp/4/image` im
Asset-Index der Welt. Das optionale Feld `image` bleibt für ein ausdrücklich
mitgeliefertes, datensatzspezifisches Bild möglich; dann ist es ein normaler
OBW-Asset-Verweis und muss dort auflösbar sein. Ohne diesen Verweis wählt die
GUI ihr eigenes Resortmotiv anhand der Lager-ID.

Diese Trennung gilt auch für die Demo: Diamond liefert die vorhandenen Welt-
und HQ-Spielerinhalte; die GUI liefert ihre Karten und Resortbilder. Der noch
separate 18er-Demokatalog ist kein Anlass, GUI-Bilddateien in die Welt zu kopieren.
Die Regeln und die künftige Engine-Anbindung stehen in
[Datenformat F4](../new_engine/datenformat.md#f4--trainingslager).

## Schrittweise Bereinigung der Modern-Pakete

Alte GUI-Grafiken sollen schrittweise aus Modern-OBW-Paketen verschwinden.
Dafür erhält die Modern UI von Anfang an keine Abhängigkeit auf diese Dateien.
Eine Entfernung erfolgt als eigene, protokollierte Datenmigration: betroffene
Dateien und Referenzen erfassen, verbleibende Daten-/Editor-/Engine-Nutzung
prüfen, entfernen und die neue Paketrevision validieren. Classic-Pakete behalten
ihre für die alte Oberfläche erforderlichen Inhalte und Siegel.

Mit dieser Dokumentationsänderung wird kein bestehendes Paket umgeschrieben.
Die öffentliche Quellverteilung enthält weiterhin nur eigene oder entsprechend
freigegebene Ressourcen und synthetische Tests. Referenzabhängige Motive und
private QA-Inhalte bleiben im privaten Bestand.

## Quellen

- Benutzerfestlegung vom 23. Oktober 2026 zur Zuständigkeit der GUI-Assets.
- Privates Repository `bmh_4k_ui`, Ausgangscommit
  `cacf8fdd5376110529afbf97c7b977aaee1d7957`: Karten, Campmotive und Menügrafiken.
- Diamond 2026/27 Modern, Datenrevision 1.3: der geprüfte Asset-Index enthält
  HQ-Wappen und Spielerbilder, aber keine `asset:camp/...`-Bildschlüssel.

## Almanachsymbol

Das neue `almanac.svg` ist ein eigenständig gezeichnetes Vektorsymbol aus
Kalenderbuch und Fußball. Die GUI-Vorbereitung übernimmt
`assets/vector/desktop-almanac.svg` als `button.almanac`. Die native UI liefert
dieselbe Grafik unter `assets/modern/icons/almanac.svg` mit und kann sie auch
bei einem älteren externen GUI-Paket anzeigen. Ein OBW muss dieses Motiv
nicht enthalten. Bestehende Desktopansichten erhalten nur das zusätzliche
Icon an einer freien Stelle; vorhandene Objekte werden dabei nicht verschoben.
