# Kalender · native Modern UI

Stand: 24. Oktober 2026. Quelle ist das unveränderte private HTML-Mockup
`bmh_4k_ui`, Referenzrevision `a7f8e39c8cb562a09d85dabae81e62a981afeb85`:
`desktop/management-menus.js`, `league-preview.js`, `table-window.css` und
`menu-themes.css`. Native Ansicht: `src/modern/ui/calendar_screen.inc`.

## Gestaltung und Bedienung

Zwölf Monatskarten von Juli bis Juni stehen in einem Raster mit vier Spalten
und drei Reihen. Innerhalb jedes Monats stehen **die Wochentage in sieben
Zeilen**, die Wochen in sechs Spalten. Diese transponierte Originalanordnung
wird beibehalten. Leere Randplätze enthalten keine künstlichen Datumsknöpfe.

Monatsnamen und Tageszahlen verwenden 30 logische Pixel, Wochentage 24.
Sonntagsbeschriftungen sind rot, Ligatermine gelb. Der gewählte Tag erhält
den thematischen Hintergrund und einen vollständigen inneren Rahmen.
Unter dem Kalender steht genau eine Terminzeile; darunter liegen die
gleich großen Auswahlfelder Land und Liga. Es gibt keinen zusätzlichen Footer.

Maus, Leertaste und Enter wählen einen Tag. Fokus bleibt beim neu gerenderten
Tagesknopf. Pfeiltasten verschieben das Datum nicht eigenmächtig. Länder- und
Ligawechsel erhalten den Tag und ändern ausschließlich die Vorschau.
Escape schließt zunächst die geöffnete Auswahl, danach das Menü. Rechtsklick
schließt das Untermenü wie im HTML. Datums- und Ligawahl bleiben nach
Wiederöffnen und Laden eines UI-Demostands erhalten.

## Daten und Enginegrenze

Der Demoanbieter liefert `calendar` mit Saisonbeginn, gewähltem Tagesindex,
Liga-ID sowie die lokale Planung aus [Freundschaftsspielen](../friendlies/README.md)
und [Verlegen](../postpone/README.md). Die Vormerkungen und Verlegungen
ändern die angezeigten Termine der eigenen Liga; bei fremder Ligavorschau
bleiben deren Vorschautermine unverändert.
Unbekannte Ligen, ungültige Tagesindizes und nicht ganzzahlige Eingaben
werden atomar abgewiesen. Die Saison stammt aus dem Demodatum; sie ist
nicht auf das Jahr der HTML-Vorschau festgelegt.

Die Vorschautermine sind deterministische Dummydaten: zwei Begegnungen je
Gegner, erste Runde am Saisonindex 30, danach sieben Tage Abstand. 18 Vereine
ergeben 34, 20 Vereine 38 Spieltage. Ist der verwaltete Verein in der Liga,
zeigt die Vorschau dessen Termine; ansonsten die des ersten Vereins der
angezeigten Liga. Die Manageridentität wird dadurch nicht geändert.

**Dies ist keine Spielplanerzeugung für die Engine.** Es gibt weder Zufall
noch Sessionzugriffe. Der spätere Sessionadapter ersetzt die Terminvorschau
durch echte Spieltage, Pokale, Verlegungen und Freundschaftsspiele. Die
zugehörigen nativen Planungsmenüs und farbigen Freundschaftsmarkierungen
sind als reine UI-Vorschau angebunden. Bestätigte Vormerkungen sind grün
markiert und erscheinen mit Gegner in der Terminzeile.

## QA

Der unabhängige Vergleich nutzt alle drei Themes bei 1280 × 720 und
1920 × 1080. Für den Datumsvergleich wurde ein isolierter Demostand auf
2027/28 gesetzt, damit auch der 29. Februar aus der HTML-Referenz prüfbar ist.
Die ursprüngliche Weltdatei bleibt unverändert. Abweichende Vereinsnamen aus
Diamond gegenüber der HTML-Demo werden ausdrücklich als Datenunterschied
behandelt.

Synthetische Regression prüft Gregorianische Schaltjahre einschließlich
2100, Wochentagspositionen, zwei Begegnungen je Gegner, 18-/20-Vereinsgrößen,
atomare Ablehnung und Wiederherstellung. Die Renderer-QA prüft zusätzlich
sichtbare Wochentage, Auswahlrahmen, Terminzeilen, Tastatur/Fokus und
Länder-/Ligawechsel. Land und Liga verwenden jetzt die gemeinsamen Footerpicker mit identischer
Popupschrift und separater Markierungsspalte. Das bewusste erneute Wählen
des gleichen Landes setzt seine erste Liga zurück; Escape verwirft die Auswahl.
