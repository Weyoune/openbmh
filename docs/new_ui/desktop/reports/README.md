# Tabellen, Stärken, Ligaspiele und Übrige Spiele

Stand: 23. Oktober 2026. Dieses Paket überträgt vier zusammenhängende
Desktopansichten in die native RmlUI. Gestaltungsreferenz ist das private,
unveränderte HTML-Mockup `bmh_4k_ui`, Revision
`494a25611de847f23bf714bfdab6708387f581a7`, insbesondere
`desktop/report-menus.js`, `league-preview.js`, `table-window.css` und
`menu-themes.css`. Proprietäre Referenzbilder und Weltdaten gehören nicht
in dieses öffentliche Dokument.

Die Ansichten verbinden **echte Katalogdaten aus der geladenen OBW-Welt**
mit ausdrücklich fiktiven Berichtsergebnissen. Sie starten keine Karriere,
führen keine Spiele aus und ändern weder Weltdatei noch Enginezustand.

## Sichtbare Daten

| Ansicht | Darstellung und Herkunft |
| --- | --- |
| Tabellen | Vereine und Wappen aus dem Katalog; Platz, Spiele, Siege, Unentschieden, Niederlagen, letzte acht Ergebnisse, Punkte, Tore und Tordifferenz aus der deterministischen Demoauswertung. |
| Stärken | Tatsächliche Vereine und Kader; gerundete Mittelwerte vorhandener Stärkewerte für Tor, Abwehr, Mittelfeld, Sturm und gesamten Kader. |
| Ligaspiele | Paarungen und fiktive Ergebnisse der gewählten Vorschau-Liga und Runde, mit echten Vereins-IDs, Namen und Wappen. |
| Übrige Spiele | Meisterschaft wie Ligaspiele; weitere Wettbewerbe als begrenzte fiktive Vorschau. Nach durchlaufenem Demoauftakt kann der DFB-Pokal stattdessen die dort angezeigten Auftaktpaare ohne Ergebnis zeigen. |

Vereinsanzahl und Reihenfolge stammen aus dem Katalog. Eine Liga mit
18 Vereinen hat 18 Tabellen-/Stärkezeilen und neun Begegnungen pro Runde;
bei 20 Vereinen sind es 20 Zeilen und zehn Begegnungen. Es gibt keine
fest verdrahtete 18er-Vereinsliste oder eine zweite Liste mit kopierten Namen.
Der verwaltete Verein wird anhand seiner stabilen ID hervorgehoben.

Stärkekomponenten verwenden die OBW-Positionen `gk`, `def`, `mid` und `fwd`.
Ein Spieler mit mehreren Positionen kann in mehreren Positionsgruppen
berücksichtigt werden; die Gesamtstärke mittelt die nutzbaren Kaderwerte.
Fehlende Positionen oder fehlende/verwertungsungeeignete Stärkedaten bleiben
`null` und erscheinen als `—`. Es werden keine erfundenen Ersatzspieler oder
scheinbaren Nullstärken eingesetzt. Eine tatsächlich gespeicherte Stärke
von null bleibt dagegen ein vorhandener Wert.

## Reproduzierbare Ergebnisse und Form

Die Vorschau bildet zwei Halbserien mit vertauschtem Heimrecht und
stabiler Katalogreihenfolge. Die Anzahl der Runden folgt der Ligagröße:
18 Vereine ergeben 34, 20 Vereine 38 Runden. Ergebnisse entstehen durch
festgelegte Ganzzahlarithmetik aus Runde, Vereinsindizes und Ligaindex,
entsprechend dem HTML-Prototyp. Sie verbrauchen keinen Zufall der Engine.
Gleicher Katalog und gleicher Vorschauzustand ergeben dieselben Ergebnisse.

Die Tabelle wertet die ersten **26 gespielten Vorschaurunden** aus, soweit
der fiktive Spielplan reicht. Die gewählte Runde des Begegnungsberichts
ändert diese Tabellenauswertung nicht. Der Bericht kann auch andere Runden
anzeigen; deren Zahlen sind ebenfalls Dummydaten und keine ausgetragenen
oder simulierten Spiele.

`S`, `U` und `N` bezeichnen die letzten bis zu acht Ergebnisse innerhalb
des gewählten Tabellenfilters. Für die Sortierung der Formspalte zählen
Siege drei und Unentschieden einen Vorschaupunkt. Die Standardsortierung
verwendet Punkte, Tordifferenz und erzielte Tore; weitere Gleichstände
bleiben stabil. Die farbigen Platzmarkierungen folgen dem errechneten Platz
und bleiben auch bei anderer Spaltensortierung dem jeweiligen Verein
zugeordnet.

Die Qualifikationslegende übernimmt die vereinfachte HTML-Darstellung:
in der ersten Liga Champions-League-Plätze 1–4, Europa League auf Platz 5,
Conference League auf Platz 6 und die letzten drei Plätze als Abstieg.
Darunter werden Aufstieg, gegebenenfalls Aufstiegsrelegation und Abstieg
markiert; die vierte Stufe zeigt einen direkten Aufstiegsplatz. **Das sind
Darstellungsmarken der Demo, keine neuen Wettbewerbs-, Aufstiegs- oder
Qualifikationsregeln für eine Engine.** Sie erzeugen keine Teilnehmerlisten.

## Gemeinsame Auswahl und Sortierung

Die vier Ansichten teilen einen `reports`-Zustand mit stabiler Liga-ID,
Rundenindex, Tabellenfilter, Wettbewerb und getrennten Sortierungen für
Tabellen und Stärken. Ein Ligawechsel bleibt deshalb beim Wechsel zwischen
den Ansichten erhalten. Länder- und Ligaauswahl ändern die Vorschau,
nicht Manageridentität oder verwalteten Verein. Erneutes Wählen desselben
Landes setzt wie im HTML auf dessen erste Katalogliga zurück.

Die Tabellenfilter sind „Alle Spiele“, „Hinrunde“, „Rückrunde“, „Heim“ und
„Auswärts“. Die Spaltenköpfe wechseln die Sortierrichtung; numerische
Spalten beginnen bei neuer Auswahl absteigend, Platz und Vereinsname
aufsteigend. **„Pl. / Verein“ sortiert nach Platz, nicht nach Vereinsname.**
In der Stärkenansicht ist die Vereinsspalte nach Namen sortierbar.
Gleiche Sortierwerte behalten ihre stabile Ausgangsreihenfolge; fehlende
Stärkewerte stehen in beiden Richtungen hinter vorhandenen Werten.

„Gesamt“ setzt die Tabellenansicht auf die erste deutsche Liga, alle Spiele
und Punkte absteigend zurück. Die Stärkensortierung und andere unabhängige
Auswahlen bleiben erhalten. Der aktuelle Rundenindex wird auf den gültigen
Bereich begrenzt, nicht pauschal wieder auf Runde 27 gesetzt.

Die Rundenpfeile laufen innerhalb der verfügbaren Vorschaurunden umlaufend.
Für nicht zur Meisterschaft gehörende „Übrige Spiele“ übernimmt die Demo
34 Vorschaurunden und feste Beispielpaarzahlen je Wettbewerb. Die Vereine
dieser Beispiele stammen aus der ersten deutschen Katalogliga; das ist
keine echte internationale Teilnehmerauswahl. Gegenüber dem HTML werden
Runden bei Liga-/Wettbewerbswechseln und bei der Anzeige eines anderen
Kontexts ausdrücklich auf dessen Bereich begrenzt. Diese Absicherung ist
eine bewusste Abweichung der UI-Vorschau, keine Terminregel des Spiels.

## Footerpicker und Fokus

Die untere Bedienleiste enthält je nach Ansicht:

- Tabellen: „Gesamt“, Spielefilter, Land und Liga.
- Stärken: Land und Liga.
- Ligaspiele: zwei Rundenpfeile, Land und Liga.
- Übrige Spiele: zwei Rundenpfeile und Wettbewerb.

Die Berichtspicker sind native RmlUI-Menüs mit sichtbarer Markierung der
aktuellen Option. Klick, Enter oder Leertaste öffnen beziehungsweise
bestätigen. Auf-/Abwärtspfeil öffnen auch vom geschlossenen Auswahlknopf;
im Menü navigieren Auf/Ab sowie Pos1/Ende. Das reine Fokussieren einer
Option verändert die gespeicherte Auswahl noch nicht.

Escape oder Rechtsklick schließen zunächst die offene Auswahl. Tab
schließt die Auswahlliste und setzt die normale Fokusfolge fort. Ein Klick
außerhalb schließt die Liste, ohne das eigentliche Klickziel zu ersetzen.
Nach einer Auswahl liegt der Fokus wieder auf dem zugehörigen Footerknopf;
nach Sortierung, Reset oder Rundenschritt auf dem auslösenden Knopf.
Details sperren Hintergrundaktionen. Abbruch, Auswahl, Fokusumlauf und
Fokusrückgabe wurden mit echten RmlUI-Ereignissen geprüft.

## Vereins-, Kader- und Spielerprofile

Vereinsknöpfe in allen vier Ansichten öffnen ein Vereinsprofil. Dort werden
Wappen, Stadion, Kapazität, Stärke und Manager aus vorhandenen OBW-Daten
beziehungsweise der Demo-Managerzuordnung angezeigt. Vorliegende
Stadionbereiche können zur Kapazität summiert werden; fehlende Angaben
bleiben sichtbar als `—`. Eine explizite Vereinsstärke und ein aus
Kaderspielern gebildeter Mittelwert haben unterschiedliche Datenquellen
und werden nicht als identische Kennzahl ausgegeben.

„Spieler“ führt zum tatsächlichen Kader dieses Vereins, in OBW-Kaderreihenfolge.
Ein Spielerknopf öffnet dessen Profil mit aufgelöstem Foto beziehungsweise
konfiguriertem Asset-Fallback, Positionen, Alter, Stärke, Verein und Vertrag.
Das Alter wird bei vorhandenem Geburtsdatum zum Demodatum berechnet.
Ein nur importiertes Referenzalter behält seinen bekannten Bezugszeitpunkt;
es wird kein Geburtstag erfunden. Vertragsdauer bedeutet verbleibende
vollständige Kalendermonate aus einem passenden vorhandenen Vertrag,
keine Vertragsverhandlung oder Initialisierung eines Spielzustands.

Die Dialogfolge lautet Bericht → Verein → Kader → Spielerprofil.
„Zurück“/„Schließen“, Escape oder Rechtsklick schließen die Detailauflage
und stellen den ursprünglichen Berichtsfokus wieder her. Es gibt in diesem
Paket keinen Vertrags-, Transfer- oder sonstigen Schreibpfad aus einem Profil.
Lange Kader werden innerhalb des Dialogs gescrollt. Tab macht den fokussierten
Spieler sichtbar; dasselbe gilt für Vereine in langen Begegnungslisten. Dies
verbessert bewusst einen Tastaturfehler der HTML-Vorlage, in der verborgene
Zeilen zwar fokussiert, aber nicht automatisch sichtbar werden.

## DataSource, Checkpoints und Kalenderkopplung

[DemoSource](../../../../src/modern/ui/model.cpp) besitzt die getrennten
Präsentationsdaten. [demo_reports](../../../../src/modern/ui/demo_reports.hpp)
liefert die Auswertungen; [Berichtsansichten](../../../../src/modern/ui/reports_screens.inc),
[Picker](../../../../src/modern/ui/report_pickers.inc) und
[Detailansichten](../../../../src/modern/ui/report_details.inc) konsumieren sie.
Aktionen werden auf einer Kopie validiert. Unbekannte IDs, unerlaubte
Schlüssel, falsche Typen und Werte außerhalb der Grenzen werden ohne
Veröffentlichung einer Teiländerung abgelehnt.

UI-Checkpoints speichern `reports` mit Tabellen-/Stärkensortierung,
Filter, Liga, Wettbewerb und Runde. Geöffnete Picker oder Detaildialoge
sind flüchtige Ansichtszustände; ein Datensnapshot ist kein Versprechen,
diese Auflagen nach dem Laden automatisch wieder zu öffnen.

Alte UI-Demostände ohne `reports` erhalten dessen Defaults und übernehmen
dabei die vorhandene Kalenderliga. Alte Stände ohne `opening_created`
erhalten `false`, auch wenn ihr bisheriges `opening`-Feld bereits „complete“
lautet. Ungültige vorhandene Berichtsdaten werden nicht still durch Defaults
ersetzt. Beim Laden vorhandener gültiger Berichtsdaten ist deren Liga für
die gemeinsame Kalender-/Berichtsvorschau maßgeblich.

Ein Ligawechsel im [Kalender](../calendar/README.md) aktualisiert auch die
Berichtsliga und begrenzt deren Runde. Umgekehrt übernehmen Kalender und
Berichte die in den Berichten ausgewählte Liga gemeinsam. Datumsauswahl
und Manageridentität werden dadurch nicht ersetzt.

Der Abschluss des Demoauftakts meldet die Aktion `opening = complete`
und setzt dabei `opening_created` auf `true`. Der alte gespeicherte
Zeichenkettenwert allein ersetzt diese Abschlussmeldung nicht. Danach
verwendet „Übrige Spiele → DFB-Pokal“
dieselben benachbarten Auftaktpaare aus höchstens 64 deutschen Ligavereinen,
also höchstens 32 Begegnungen. Überschrift ist „1. Runde“, Ergebnisse
bleiben `null` beziehungsweise „–“, und die Rundenpfeile sind deaktiviert.
Das verbindet zwei Demoansichten; es ist weder eine echte Pokalauslosung
noch eine neue Session- oder Spielregel.

## Referenzgeometrie und Prüfstand

Die HTML-Referenz verwendet den logischen 3840×2160-Desktop mit einem
2720×2040-Fenster bei `(560, 60)`, einer Titelleiste mit 108 und einer
Footerbedienung mit 94 logischen Pixeln Höhe. Bei 1920×1080 entsprechen Fenster und Footer
1360×1020 beziehungsweise 47 physischen Pixeln Höhe. Tabellen- und
Stärkenbreiten sowie Zeilenhöhen berücksichtigen die tatsächlichen 18 oder
20 Vereine. Bodytext, Tabellenkopf und Auswahlknöpfe orientieren sich an
36, 30 und 32 logischen Pixeln; die Spieltagsüberschrift an 46.
Die Themes Midnight, Silver und Matchday verwenden die gemeinsame
Menügestaltung. Geometrische Zielwerte sind keine Behauptung pixelgleicher
Rasterung zwischen Browser und RmlUI.

[Synthetische Providertests](../../../../tests/modern_reports_tests.cpp)
prüfen unter anderem 18-/20-Team-Spielpläne, Hin-/Rückspiel und Heimrecht,
Punkte-/Torerhaltung, Filterzerlegung, Sortierung, fehlende Positionswerte,
atomare Ablehnung, Reset, Clamping und die begrenzten Auftaktpaare. Der
isolierte Linux-Komponententest und alle 17 ausgewählten Integrations-/
Regressionstests bestehen auf dem zusammengeführten Stand.

Die unabhängige Menüprüfung erfasst 48 Fälle: vier Ansichten × drei Themes ×
zwei Größen × 18/20 Vereine. 113 native Bedienungszustände und 94
Browserreferenzzustände decken unter anderem Sortierrichtungen, Filter,
Rücksetzen, Ligawechsel, Rundenumlauf und Wettbewerbe ab. Die Detailprüfung
umfasst zusätzlich 22 Zustandsprüfungen und drei finale Sichtprüfungen.
Gemessene Textboxen passen in ihre Zellen und stehen vertikal mittig;
die Bruchteilsverteilung der 20er-Zeilen weicht um weniger als einen
physischen Pixel von Chromiums automatischem Tabellenlayout ab.

Private Screenshots, Eingabeskripte, Binärhashes und UI-Demostände liegen in
`bmh_4k_ui/docs/qa/reports-batch-2026-10-23/`. Der OS-Neustart unterbrach den
Build; neun fehlende Matrixfälle und die finalen Aufnahmen wurden danach
ergänzt. Die beiden Fokuskorrekturen wurden gezielt nachgeprüft. Diese
Nachweise betreffen Linux/OpenGL und die beschriebenen Daten; sie sind
keine vollständige Windows-/macOS-Laufzeitabnahme oder Freigabe aller Menüs.

Die [gemeinsame Paarungsanordnung](../fixtures/README.md) ersetzt seit
24. Oktober die weit auseinanderliegenden Mannschaftsseiten: Heimname und
Heimwappen rechtsbündig zur schmalen Ergebnisspalte, danach Auswärtswappen
und Auswärtsname linksbündig.
