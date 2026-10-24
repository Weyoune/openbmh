# TOTO · Elferwette

Stand: 24. Oktober 2026. Native RmlUI-Übertragung der HTML-Vorlage
`desktop/toto-menu.js` und `toto-window.css`, Referenzrevision `9db6926d`
im privaten Mockup-Repository. Roter Druck, warmer Papiergrund und blaue
Kreuze bleiben in allen drei Themes erhalten. Grafiken und Bedienung
werden nicht durch einen alternativen Wettbildschirm ersetzt.

## Sichtbare Daten

Links steht ein festes Vorschauprogramm aus neun Partien der ersten und
zwei Partien der zweiten deutschen Liga. Vereinsnamen und Wappen stammen
aus dem schreibgeschützten OBW-Katalog. Paarungen kommen aus derselben
synthetischen Spielplanprojektion wie die Berichte; aktuelle Berichtfilter
ändern das TOTO-Programm nicht. Die Vorschau verwendet Runde 27 und das
Startjahr der geladenen Welt. Fehlende Vereine werden nicht erfunden;
bei unvollständigem Programm bleibt die Vormerkung gesperrt.

Rechts enthält der Schein sechs unabhängige Spalten mit je elf Reihen.
Jede Reihe erlaubt genau eine Auswahl aus 1/0/2; erneutes Anklicken entfernt
das Kreuz. Die erste Spalte ist vorbelegt, weitere Spalten sind leer.
Der Tippvorschlag und das Leeren wirken nur auf die ausgewählte Spalte.

Der Gesamteinsatz wird über die gemeinsame Zahleneingabe bearbeitet:
0–99.999.999 ganze Anzeigeeinheiten, Vorschläge 1.000–6.000 und gesonderte
Betragsbestätigung. Eine Vormerkung braucht einen positiven Einsatz,
mindestens eine vollständige Spalte und keine teilweise ausgefüllte Spalte.
„Letzter Schein“ liest eine unabhängige Kopie der zuletzt übernommenen
Spalten und des Einsatzes. Spätere Eingaben ändern diese Kopie nicht.

## Bedienung und Zustand

Tab führt zu den Spaltenköpfen, jeweils einer Tippzelle pro Spalte sowie
den normalen Fenster-/Aktionsbuttons. Pfeile navigieren über alle 18 Zellen
je Spiel beziehungsweise elf Reihen. Home/End springen zur ersten/letzten
Zelle, mit Ctrl zusätzlich zur ersten/letzten Reihe. Fokussieren verändert
weder Tipps noch ausgewählte Spalte. Escape schließt zunächst den Dialog
und stellt den vorherigen Fokus wieder her.

`DemoSource.state.toto` enthält die sechs Spalten, den Einsatz in kleinster
Währungseinheit und den letzten Schein. Lokale Dialog-/Fokusflags sind kein
Spielstand. Aktionen sind revisionsgebunden; abgewiesene Eingaben und
fehlerhafte alte Checkpoints lassen den Zustand unverändert. Nur fehlende
TOTO-Felder werden beim Laden ergänzt. Screenshots werden zusammen mit
UI-Checkpoints und JSONL-Bedienschritten gesichert.

Es gibt keine Auszahlung, Auswertung oder Geldbuchung. Die Vorschau ruft
keine Match-, Saison- oder Zufallsfunktion auf. Ein späterer Engineadapter
muss Spielprogramm, Annahmeschluss und tatsächliche Buchungsregeln liefern.
Classic und OBW-Format bleiben davon unberührt. Die synthetischen Tests
prüfen unabhängige Spalten, leere/teilweise Scheine, tiefe Ergebniskopien,
Geldpräzision, Revisionsschutz und Wiederherstellung. Renderer-/HTML-Vergleich:
[gemeinsame Menü-QA](../../menu-qa.md).
