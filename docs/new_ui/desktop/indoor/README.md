# Hallentabellen

Stand: 24. Oktober 2026. Die native RmlUI-Ansicht übernimmt die
Hallentabellen aus dem privaten HTML-Mockup `bmh_4k_ui`, Referenzrevision
`a7f8e39c`. Das relevante Produktverhalten entspricht Revision
`494a25611de847f23bf714bfdab6708387f581a7`. Maßgeblich sind insbesondere
`desktop/report-menus.js` (`indoor()`, `sortOptions()`), `poc-menus.css`,
`table-window.css` und `menu-themes.css`. Proprietäre Referenzbilder und
Weltdaten gehören nicht in dieses öffentliche Dokument.

## Sichtbare Daten und Grenzen

Die Ansicht kombiniert Vereine, Namen und Wappen aus dem geladenen
OBW-Katalog mit ausdrücklich fiktiven Platzierungen und Punkten. Sie
verwendet die ersten **höchstens 16 Vereine der ersten deutschen
Katalogliga**, in deren Katalogreihenfolge. Die Platzierung ist der
Ausgangsindex plus eins; die Punktzahl lautet `324 − 19 × Ausgangsindex`.
Bei 16 Vereinen reicht sie daher von 324 bis 39 Punkten. Kleinere Ligen
zeigen nur ihre vorhandenen Vereine. Ohne deutsche Liga bleibt die Liste
leer; es gibt keine erfundenen Ersatzvereine.

Die Auswahl erfolgt unabhängig von der aktuell in anderen Berichten
gewählten Liga, dem Land, Tabellenfilter, Spieltag und Wettbewerb. Eine
18er- oder 20er-Liga erzeugt hier deshalb jeweils 16 Zeilen. Stabile
Katalog-IDs verbinden die Zeilen mit den bestehenden Vereinsprofilen;
Vereinsnamen sind nicht im Quellcode festgeschrieben. Der verwaltete
Verein verwendet dieselbe Hervorhebung wie die übrigen Berichte.

Diese Zahlen dienen allein der UI-Vorschau bis zum Anstoß. Die Ansicht
berechnet keine echten Hallenturniere, verbraucht keinen Enginezufall und
ändert weder Weltdateien noch Enginezustand. Auch die Beschriftung
„Nach dem 5. Turnier“ bestätigt keine tatsächlich gespielten Turniere.

## Aufbau und Interaktionen

Der Menüinhalt hat die gemeinsame logische Fläche von 2612 × 1864.
Die Tabelle beginnt links oben; ihre beiden Spalten sind zu 78 Prozent
„Pl. / Verein“ und zu 22 Prozent „Punkte“ aufgeteilt. Kopf und Datenzeilen
sind jeweils 66 Einheiten hoch. Die Kopfzeile verwendet Schriftgröße 30,
die Datenzeilen 36. Platz und Verein stehen links, Punkte rechts. Anders
als in der Ligatabelle gibt es keine Qualifikationsmarkierungen. Die
untere Schaltfläche nimmt die volle Breite und 94 Einheiten Höhe ein.

Beide Spaltenköpfe sind Sortierschaltflächen mit sichtbarer Richtung und
`aria-sort`. „Pl. / Verein“ sortiert nach der vorhandenen Platzierung.
Erneutes Betätigen desselben Kopfes kehrt die Richtung um; die erstmalige
Auswahl von Platz beginnt aufsteigend, die von Punkten absteigend. Die
Standardansicht zeigt Punkte absteigend. Platznummern und Punktzahlen
bleiben beim Umsortieren ihrem Verein zugeordnet. Eine gespeicherte
Sortierung nach `original` ist ebenfalls zulässig und folgt je nach
Richtung der ursprünglichen Katalogreihenfolge. Die Sortierung liegt
separat unter `reports.sorts.indoor`; Änderungen anderer Berichtssorten
und „Gesamt“ der Ligatabelle setzen sie nicht zurück.

Vereinsbuttons öffnen dieselben Profile und Kader-/Spielerdetails wie die
übrigen Berichte. „Nach dem 5. Turnier“ öffnet den Dialog
„Hallenmasters“ mit dem Text „Fiktive Ergebnisse und Platzierungen zur
Saison …“. Die Saison folgt dem gespeicherten Demo-Datum: Juli bis
Dezember gehören zum beginnenden, Januar bis Juni zum vorherigen
Saisonjahr. Es gibt kein festgeschriebenes Jahr aus dem HTML-Mockup.
„Zurück“ schließt die Information und stellt den Fokus auf der
aufrufenden Schaltfläche wieder her. Die gemeinsame Berichtsebene sperrt
während eines offenen Dialogs den darunterliegenden Menüinhalt.

## Speicherung und Migration

Neue Demozustände enthalten `reports.sorts.indoor` mit `key: "points"`
und `desc: true`. Ältere Checkpoints, denen ausschließlich diese neue
Sortierung fehlt, erhalten sie über `migrate_demo_reports()`. Die
Migration prüft den vollständigen resultierenden Berichtszustand und
übernimmt ihn erst bei erfolgreicher Validierung. Vorhandene gültige
Indoor-Sortierungen bleiben unverändert. Vorhandene ungültige Sortierungen,
unbekannte Felder und weitere beschädigte Berichtsdaten werden abgewiesen;
die Migration repariert sie nicht teilweise.

`report_indoor_rows()` und die Berichtsaktionen arbeiten ausschließlich
mit dem Präsentationszustand und dem unveränderten Katalog. Ob und wann
die Engine echte Hallenturniere, Teilnehmer, Punkte oder Ergebnisse
liefert, bleibt eine separate Integrationsentscheidung.

## Prüfstand

`tests/modern_reports_tests.cpp` prüft mit synthetischen Katalogdaten den
16er-Grenzwert, stabile IDs, Plätze und Punkte, die Unabhängigkeit von
ausländischer Ligaauswahl und Berichtsfiltern, alle Sortierrichtungen,
unabhängige Berichtssortierungen sowie strenge, atomare und wiederholbare
Checkpointmigration. Kleine und leere Kataloge werden ebenfalls geprüft.
Eine separate C++-Syntaxprüfung von Projektion und Tests ist erfolgreich.

Der gemeinsame Paketbuild und alle 18 ausgewählten Regressionstests bestehen.
Die unabhängige native Prüfung umfasst 18 Matrixläufe in drei Themes bei
720p/1080p, 43 Bedienzustände und gezielte Nachprüfungen der visuellen
Sperrkennzeichnung. Die beschriebenen P1-/P2-Befunde sind behoben. Bilder,
Skripte und ladbare UI-Demostände liegen privat unter
`bmh_4k_ui/docs/qa/planning-batch-2026-10-24/`. Diese Nachweise betreffen
Linux/OpenGL und die dokumentierten Fälle, keine vollständige
Windows-/macOS-Laufzeitabnahme oder allgemeine Freigabe der Modern UI.
