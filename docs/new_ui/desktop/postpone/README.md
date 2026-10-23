# Spiel verlegen · native Modern UI

Stand: 24. Oktober 2026, natives Planungspaket. Quelle ist das unveränderte
private HTML-Mockup `bmh_4k_ui`, Referenzrevision
`a7f8e39c8cb562a09d85dabae81e62a981afeb85`; die relevanten Produktdateien
entsprechen `494a25611de847f23bf714bfdab6708387f581a7`.
Maßgeblich sind `desktop/management-menus.js`, `league-preview.js` und
`career-menus.css` nach den gemeinsamen Tabellenregeln. Die native
Umsetzung liegt in
[planning_screens.inc](../../../../src/modern/ui/planning_screens.inc),
[planning_controls.inc](../../../../src/modern/ui/planning_controls.inc),
[calendar_screen.inc](../../../../src/modern/ui/calendar_screen.inc) und
[planning.rcss](../../../../assets/modern/planning.rcss).
Private Referenzbilder und Weltdaten bleiben außerhalb des Repositorys.

## Ansicht und Bedienung

Der Kopf nennt den eigenen Verein, dessen Liga und die Saison. Daneben
stehen die Schritte „Spiel wählen“ und „Termin wählen“. Zwölf Monatskarten
von Juli bis Juni stehen in vier Spalten und drei Reihen. Die
Wochentage stehen wie in der HTML-Vorlage in sieben Zeilen, die Wochen in
sechs Datumsspalten. Ligatermine sind gelb; ein bestätigtes Ausgangsspiel
erhält einen grünen Innenrahmen. Unter dem Kalender stehen Ausgangsspiel
und neuer Termin in getrennten Karten, darunter die drei Aktionen
„Ausgangsspiel wählen“, „Hierhin verlegen“ und „Letzte Verlegung zurück“.

Zunächst einen gelben Ligatag anklicken und als Ausgangsspiel bestätigen.
Danach einen freien Tag ab Saisonindex 7, dem 8. Juli, auswählen und
„Hierhin verlegen“ betätigen. Die Ausgangsauswahl wird nach erfolgreicher
Verlegung aufgehoben. Anders als im Buchungskalender der
[Freundschaftsspiele](../friendlies/README.md) bleiben die Tagesknöpfe hier
auch bei früheren oder belegten Terminen anklickbar; der Aktionsknopf
bleibt dann gesperrt. Das × der Ausgangskarte hebt nur diese Auswahl auf:
gewählter Tag und gespeicherte Verlegungen bleiben erhalten.

Die Planung verwendet immer die Liga des ersten verwalteten Vereins.
Eine zuvor gewählte fremde Liga in Berichten oder im normalen Kalender
ändert weder die eigene Karriere noch die Verlegungsquelle. Der
Planungsrenderer verwendet dafür eine eigene Ansichtskopie; die geteilte
Ligavorschau wird durch das Öffnen dieses Menüs nicht umgestellt.

## Ursprünglicher Termin und Rücknahme

Jede Begegnung hat einen unveränderlichen ursprünglichen Saisonindex
`from`. `CalendarFixture.day` zeigt den aktuellen Termin, `round` behält
den Spieltag und `home`/`away` behalten die stabilen Vereins-IDs. Die
Ausgangsauswahl speichert `source_day: from`, keine Position in einer Liste
und keinen bereits verschobenen Zwischentermin.

Beispiel: Ein Spiel von Tag 30 wird auf Tag 7 und anschließend auf Tag 8
verlegt. Die gespeicherte Verlegung lautet danach `{from: 30, to: 8}`.
Eine erneute Verlegung derselben Quelle ersetzt deren bisherigen Eintrag
und hängt sie an das Ende der Rücknahmereihenfolge. „Letzte Verlegung
zurück“ entfernt diesen letzten Eintrag und stellt Tag 30 wieder her;
Tag 7 ist kein Rücknahmeschritt.

Ist der Originaltag inzwischen durch ein anderes Ligaspiel oder ein
Freundschaftsspiel belegt, wird die Rücknahme atomar abgewiesen. Der Dialog
„Rücknahme nicht möglich“ erläutert den belegten ursprünglichen Termin.
Die vorhandene Verlegung bleibt erhalten. Das entspricht der belegten
HTML-Folge einschließlich des Falls, dass nach einer Verlegung ein
Freundschaftsspiel auf den freigewordenen Originaltag vorgemerkt wurde.

## Daten, Validierung und Checkpoints

Der Präsentationsprovider in
[demo_calendar.hpp](../../../../src/modern/ui/demo_calendar.hpp) und
[demo_calendar.cpp](../../../../src/modern/ui/demo_calendar.cpp) erweitert
`calendar` um `source_day` mit Standardwert `-1` und `moves` mit Einträgen
`{from, to}`. Er teilt `start` und `selected` mit dem normalen Kalender und
den Freundschaftsspielen. Nur `calendar.league` wird mit
`reports.league` synchronisiert; die Gegnerliga `friendly_league` bleibt
separat.

Die `calendar`-Aktionen `postpone-source`, `postpone-cancel`,
`postpone-move` und `postpone-undo` arbeiten auf einer Kopie des
Demozustands und übernehmen diese erst nach vollständiger Validierung.
`select` wählt den Tag. Die Quelle muss ein ursprünglicher eigener
Ligatermin sein; Ziele müssen ab Index 7 und innerhalb der Saison liegen.
Doppelte Quellen oder Ziele sowie Überschneidungen zwischen Ligaspielen
und den höchstens 20 Freundschaftsvormerkungen werden abgewiesen. Das
gilt auch nach mehreren Verlegungen. `DemoSource` überprüft die Revision
vor jeder Aktion; fehlgeschlagene oder veraltete Aktionen verändern weder
Zustand noch Revision.

Die Ligavorlage ist deterministisch: erster Termin `30 + 7 × round`, je
ein Heim- und Auswärtsspiel pro Gegner. 18 Vereine ergeben 34, 20 Vereine
38 Spieltage. Die Zielstruktur umfasst fünf deutsche Ligen mit je 18
Vereinen sowie die vier Auslandsländer Spanien, Italien, Frankreich und
England als Vorschau. Die tatsächliche Projektion folgt den vorhandenen
Katalogligen. Eigene Verlegungen werden ausschließlich bei Vorschau der
eigenen Liga angewandt. Bei einer fremden Liga zeigt der normale Kalender
die unveränderten Termine ihres ersten Vereins; eigene Vormerkungen werden
dort ausgeblendet. Ausländische Vereine bleiben als Freundschaftsgegner
wählbar, ohne die Karriere zu wechseln.

Speichern und Laden erhält die ausgewählte Quelle und alle Verlegungen.
`migrate_demo_calendar()` ergänzt fehlende Planungsfelder älterer
UI-Checkpoints, validiert aber vorhandene Werte und Einträge unverändert.
Ungültige Quellen, Kollisionen, unbekannte IDs oder fehlerhafte Typen
führen zur vollständigen Ablehnung. Beim Wechsel des ersten verwalteten
Vereins wird die lokale Kalenderplanung zurückgesetzt. Änderungen weiterer
Manager bei unverändertem erstem Verein setzen sie nicht zurück.

## Referenzmaße und bewusste Abweichungen

Die vermessene HTML-Inhaltsfläche ist 2612 × 1864 groß. Der Kopf belegt
`(0,0,2612,100)`. Der Kalender liegt bei `(0,132,2612,1362)`, mit
horizontalen Abständen von 28 und vertikalen von 22. Monatskarten messen
632 × rund 439,33; Monatsnamen und Tage verwenden Schriftgröße 30,
Wochentage 24. Die beiden Zusammenfassungskarten beginnen bei y1526,
sind jeweils 1293 × 200 groß und haben 26 Einheiten Abstand. Der
Aktionsbereich beginnt bei y1770, ist 94 Einheiten hoch und enthält drei
gleich breite Knöpfe mit 24 Einheiten Abstand. Die bestätigte Quelle hat
einen grünen Innenrahmen von 3 Einheiten. Diese Referenzmaße sind das
Vergleichsziel; sie erklären keine native Abnahme für bereits bestanden.

**OpenBMH deviates from the original:** Die Saison folgt dem gespeicherten
Demodatum statt dem festen HTML-Zeitraum Juli 2027 bis Juni 2028. Reale
Gregorianische Schaltjahre bestimmen die Saisonlänge von 365 oder 366
Tagen. Stabile Vereins- und Liga-IDs ersetzen die HTML-Listenindizes;
der eigene Verein wird aus dem gespeicherten Erstmanager ermittelt und
ist nicht fest auf den ersten HTML-Verein eingestellt. Katalognamen und
Wappen können entsprechend von der Referenz abweichen. Die Unterschiede
bei Gegnerstärken und Fokus nach dem Kostendialog sind im
[Freundschaftsspiel-Dokument](../friendlies/README.md) beschrieben.

## Prüfstand und Enginegrenze

Die synthetischen Providerprüfungen in `tests/modern_planning_tests.cpp`
und die Planungsintegration in `tests/modern_ui_core_tests.cpp` sind grün.
Geprüft werden insbesondere ursprüngliche Terminidentität, erneutes
Verlegen, Reihenfolge der Rücknahme, durch Freundschaftsspiele blockierte
Rücknahme, doppelte Quellen und Ziele, Ligakollisionen, freie Originaltage,
fremde Ligavorschau, stabile IDs, Saisonränder und atomare Ablehnung.
Die Sourceintegration prüft den Speicher-Roundtrip und stellt bei
kollidierenden Checkpoints den unveränderten Zustand samt Revision sicher.

Der gemeinsame Paketbuild und alle 18 ausgewählten Regressionstests bestehen.
Die unabhängige native Prüfung umfasst 18 Matrixläufe in drei Themes bei
720p/1080p, 43 Bedienzustände und gezielte Nachprüfungen der visuellen
Sperrkennzeichnung. Die beschriebenen P1-/P2-Befunde sind behoben. Bilder,
Skripte und ladbare UI-Demostände liegen privat unter
`bmh_4k_ui/docs/qa/planning-batch-2026-10-24/`. Diese Nachweise betreffen
Linux/OpenGL und die dokumentierten Fälle, keine vollständige
Windows-/macOS-Laufzeitabnahme oder allgemeine Freigabe der Modern UI.

Diese Verlegungen betreffen ausschließlich den lokalen UI-Demostand.
Weder Engine noch Session werden erzeugt oder verändert, kein RNG wird
verbraucht und weder Geld noch OBW-Welt oder echte Spiele ändern sich.
Der spätere Sessionadapter muss tatsächliche Spielpläne, zulässige
Verlegungszeiträume, Wettbewerbe, Zustimmung und mögliche Kosten liefern.
Die aktuelle Demo behauptet keine solche Enginefreigabe.
