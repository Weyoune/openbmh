# Freundschaftsspiele · native Modern UI

Stand: 24. Oktober 2026, natives Planungspaket. Die unveränderte private
HTML-Referenz `bmh_4k_ui` steht auf
`a7f8e39c8cb562a09d85dabae81e62a981afeb85`; die relevanten Produktdateien
entsprechen `494a25611de847f23bf714bfdab6708387f581a7`.
Maßgeblich sind `desktop/management-menus.js`, `league-preview.js`,
`advertising-friendlies-window.css` und die gemeinsamen Tabellen- und
Theme-Regeln. Native Quellen sind
[planning_screens.inc](../../../../src/modern/ui/planning_screens.inc),
[planning_controls.inc](../../../../src/modern/ui/planning_controls.inc),
[calendar_screen.inc](../../../../src/modern/ui/calendar_screen.inc) und
[planning.rcss](../../../../assets/modern/planning.rcss).
Private Referenzbilder und Weltdaten werden nicht mitgeliefert.

## Ansicht und Ablauf

Die Gegnerphase zeigt Verein und Wappen, Stärke, Platz und Spiele. Nur der
verwaltete Verein fehlt in seiner eigenen Ligaauswahl; ausländische Vereine
sind als Gegner zulässig. Land und Liga wechseln unabhängig von der
Ligavorschau des normalen Kalenders und der Berichte. Zielstruktur sind
fünf deutsche Ligen mit je 18 Vereinen und die vier Auslandsländer Spanien,
Italien, Frankreich und England als Vorschau und Gegnerquelle. Vorhandene
Katalogligen und deren Vereinszahlen werden gelesen; eine ausländische
Auswahl eröffnet keine andere Karriere. Die Vorschautermine unterstützen
insbesondere 18 Vereine mit 34 und 20 Vereine mit 38 Spieltagen.

Die Dialogfolge der HTML-Referenz bleibt erhalten:

1. Gegner wählen oder „Termin auswählen“ öffnen: Dialog „Austragungsort“.
2. „Heim“ oder „Auswärts“ übernehmen: Dialog „Freundschaftsspiel · Kosten“.
3. „Termin wählen“ bestätigen: Kalenderphase mit zwölf Monaten von Juli
   bis Juni, vier Spalten und drei Reihen.
4. Freien Tag auswählen und „Termin vormerken“ betätigen: lokale
   Vormerkung mit Bestätigung. Nach dem Schließen führt der Fokus zur
   Schaltfläche „Vorgemerkte Spiele“.

Gegner und Austragungsort werden bereits bei ihrer Auswahl gespeichert.
Abbrechen oder Escape im Dialog erhält diese Auswahl, erzeugt aber kein
Spiel. Escape in der Kalenderphase führt zur Gegnerphase zurück. Tab
bleibt innerhalb eines offenen Dialogs; nach dessen Schließen wird der
aufrufende Knopf wieder fokussiert. Die Vormerkungsliste ist nach Datum
sortiert und zeigt Datum, Gegner und Heim/Auswärts.

Der Kostenbeitrag ist eine reine Demoanzeige:
`(Heim ? 25000 : 12000) + ursprünglicher Gegnerindex × 500`.
Der Index stammt aus der vollständigen Katalogreihenfolge der gewählten
Gegnerliga, bevor der eigene Verein aus der sichtbaren Liste entfernt wird.
Keine Bestätigung zieht diesen Betrag vom Guthaben ab.

## Termine, Zustand und Speicherung

[demo_calendar.hpp](../../../../src/modern/ui/demo_calendar.hpp) und
[demo_calendar.cpp](../../../../src/modern/ui/demo_calendar.cpp) stellen den
Präsentationsvertrag bereit. `DemoSource` hält unter `calendar` den
Saisonbeginn `start`, den gemeinsamen Tagesindex `selected`, die mit
`reports.league` synchronisierte Vorschau `league` sowie die getrennte
Gegnerauswahl `friendly_league`, `opponent`, `home` und
`friendly_stage` (`0` Gegnerphase, `1` Kalenderphase). Die Vormerkungen
liegen in `friendlies` als `{day, opponent, league, home}`. Vereins- und
Ligaangaben sind stabile Katalog-IDs. Leere oder einzelne Vereinsbestände
dürfen `opponent: ""` zeigen; ohne gültigen Gegner bleibt Vormerken gesperrt.

Die `calendar`-Aktionen `friendly-country`, `friendly-league`,
`friendly-opponent`, `friendly-venue`, `friendly-stage` und `friendly-book`
ändern ausschließlich diesen Demozustand. `select` wählt weiter den
geteilten Kalendertag. Der Provider prüft den vorhandenen und den geplanten
Zustand, bevor er eine Änderung übernimmt. Ungültige Typen, unbekannte IDs,
der eigene Verein als Gegner und zusätzliche Aktionsfelder werden
abgewiesen. `DemoSource` weist zusätzlich veraltete Revisionen zurück.

Freie Termine beginnen am Saisonindex 7, also am 8. Juli, und müssen vor
dem nächsten 1. Juli liegen. Ligaspiele einschließlich ihrer
[Verlegungen](../postpone/README.md) und vorhandene Freundschaftsspiele
belegen den Kalender der eigenen Karriere. Eine fremde Vorschau in
`calendar.league` verändert diese Prüfung nicht. Derselbe Tag kann nicht
zweimal reserviert werden; maximal 20 Vormerkungen sind erlaubt. Frühe und
belegte Tage sind in der Kalenderphase gesperrt; nach 20 Einträgen sind
„Termin auswählen“ und „Termin vormerken“ gesperrt. Im normalen Kalender
erscheinen eigene Vormerkungen nur bei Vorschau der eigenen Liga.

Speichern und Laden erhält Auswahl, Kalenderphase und Vormerkungen.
`migrate_demo_calendar()` ergänzt fehlende Planungsfelder älterer
Checkpoints und validiert das vollständige Ergebnis atomar. Vorhandene
ungültige Werte, beschädigte Einträge und Terminüberschneidungen werden
abgewiesen und nicht repariert. Ein Wechsel des ersten verwalteten Vereins
setzt dessen Kalenderplanung zurück; Änderungen weiterer Manager bei
unverändertem erstem Verein erhalten sie.

## Referenzmaße und bewusste Abweichungen

Die Referenzvermessung verwendet eine logische Inhaltsfläche von
2612 × 1864. Die Gegnerliste beginnt bei `(0,148)` und ist 2612 × 1584
groß; ihre Spalten sind 58/14/14/14 Prozent breit. Der Kopf ist 66 Einheiten
hoch bei Schriftgröße 27, Datenzeilen 78 bei Schriftgröße 35. Der untere
Bereich beginnt bei y1770 und ist 94 Einheiten hoch. Die Kalenderphase
verwendet `(0,148,2612,1424)` mit Monatsabständen von 22; darunter liegen
Terminübersicht und drei Aktionsknöpfe. Die mittigen Dialoge sind 1840
Einheiten breit, die Ort-Auswahl 496, der Kostendialog 720,5 Einheiten hoch.
Diese Maße beschreiben den HTML-Maßstab; ihre native Übereinstimmung gehört
zur gesonderten Laufzeitprüfung.

**OpenBMH deviates from the original:** Die Saison folgt dem gespeicherten
Demodatum einschließlich tatsächlicher Schaltjahre statt dem festen
HTML-Jahr 2027/28. IDs ersetzen veränderliche Vereins- und Ligaindizes.
Vereine, Wappen und Kader stammen aus dem geladenen OBW; die Stärke wird
mit der bestehenden Berichtsprojektion aus vorhandenen Kaderattributen
bestimmt. Fehlende Stärken bleiben „—“. Die HTML-Formel `79 − i % 9` wird
nicht als tatsächliche Vereinsstärke ausgegeben. Platz `i + 1` und Spiele
`26` bleiben ausdrücklich Demoangaben.

Nach dem Kostendialog erhält ein bereits gültiger gewählter Tag den Fokus;
sonst wird chronologisch der erste freie Tag ab Index 7 fokussiert. Die
HTML-Referenz greift stattdessen auf den ersten freien Knopf ihrer nach
Wochentagen angeordneten DOM-Reihenfolge zurück, im Referenzdefault auf
den 12. Juli. Der native Fokus verändert den gespeicherten gewählten Tag
nicht eigenmächtig.

## Prüfstand und Enginegrenze

`tests/modern_planning_tests.cpp` und die Planungsfälle in
`tests/modern_ui_core_tests.cpp` sind grün. Die synthetischen Tests prüfen
unter anderem stabile IDs, Gebührenindex, 18-/20-Vereinsvorlagen,
Schaltjahresgrenzen, freie/belegte Termine, das 20er-Limit, fehlerhafte
Eingaben einschließlich Ganzzahlüberlauf, atomare Ablehnung, Migration und
Speicher-Roundtrip. Die Sourceintegration prüft auch Planung bei fremder
Ligavorschau, unveränderte Finanzen und Manager sowie die vollständige
Ablehnung kollidierender Checkpoints.

Der gemeinsame Paketbuild und alle 18 ausgewählten Regressionstests bestehen.
Die unabhängige native Prüfung umfasst 18 Matrixläufe in drei Themes bei
720p/1080p, 43 Bedienzustände und gezielte Nachprüfungen der visuellen
Sperrkennzeichnung. Die beschriebenen P1-/P2-Befunde sind behoben. Bilder,
Skripte und ladbare UI-Demostände liegen privat unter
`bmh_4k_ui/docs/qa/planning-batch-2026-10-24/`. Diese Nachweise betreffen
Linux/OpenGL und die dokumentierten Fälle, keine vollständige
Windows-/macOS-Laufzeitabnahme oder allgemeine Freigabe der Modern UI.

Es entstehen ausschließlich lokale UI-Vormerkungen. Der Provider
konstruiert keine Engine oder Session, verbraucht keinen RNG und ändert
weder OBW-Welt noch Geld, Ergebnisse oder echte Spielpläne. Der spätere
Sessionadapter muss verbindliche Terminregeln, Gegnerzusagen, Kosten und
Matchanlage gesondert liefern; die Demo bestätigt diese Regeln nicht.

Der Austragungsort heißt nativ „Heimspiel“ statt des im Gold fest gebundenen
„Heimspiel in Frankfurt“. Die Demo erfindet keinen Ortsdatensatz für andere
verwaltete Vereine. Beim Wechsel über OS-Beenden → Speichern wird eine offene
Planungsauflage verworfen; der Speicherbildschirm bleibt per Tab bedienbar.
