# Fußball-Almanach · native Modern UI

Stand: 24. Oktober 2026. Das Paket ergänzt einen eigenständigen Almanach
mit fiktiven historischen Spielen. Die private HTML-Referenz sind die mit diesem Paket eingeführten Dateien
`desktop/almanac-menu.js` und `desktop/almanac-window.css` auf Basis von
`13e44980540d9dbecaf9e667aec03cbe3fbfb3f0`. Diese Basisrevision allein
enthält das neue Menü noch nicht. Die HTML-Studie und die native
RmlUI-Umsetzung verwenden denselben normalisierten
[Datenvertrag](DATA_CONTRACT.md), bleiben aber getrennte Frontends.
Private Referenzbilder und Weltdaten gehören nicht in dieses Repository.

Native Quellen sind
[almanac_screens.inc](../../../../src/modern/ui/almanac_screens.inc),
[almanac_controls.inc](../../../../src/modern/ui/almanac_controls.inc),
[almanac.rcss](../../../../assets/modern/almanac.rcss),
[demo_almanac.hpp](../../../../src/modern/ui/demo_almanac.hpp) und
[demo_almanac.cpp](../../../../src/modern/ui/demo_almanac.cpp).

## Drei Ansichtsebenen

Das Desktop-Icon „Almanach“ öffnet zunächst den **Jahreskalender**.
Anders als der Saisonkalender zeigt er Januar bis Dezember desselben
Kalenderjahres. Zwölf Monatskarten stehen in vier Spalten und drei Reihen;
innerhalb der Karte stehen Wochentage in sieben Zeilen und Wochen in
sechs Datumsspalten. Monate mit 28, 29, 30 oder 31 Tagen behalten ihre
wirkliche Wochentagslage. Der Jahresschalter bietet nur Kalenderjahre mit
bereits sichtbaren Archivspielen an. Beim ersten Öffnen wird das jüngste
solche Jahr gewählt.

Nur Tage mit gespielten Partien sind Schaltflächen. Ihr zugänglicher Name
nennt das Datum und die Anzahl der Partien. Andere Tage bleiben reine
Datumsbeschriftungen; zukünftige Tage erscheinen zusätzlich gedämpft.
Die Jahre werden aus vorhandenen Datensätzen ermittelt, nicht aus einer
festgeschriebenen Liste. Ein leeres Archiv zeigt einen verständlichen
Leerzustand; eine nicht unterstützte Datenquelle meldet die fehlende
Verfügbarkeit.

Ein Datum öffnet die **Tagespaarungen** mit allen Partien dieses Tages.
Die gesamte Paarungszeile ist anklickbar. Heimname und Wappen stehen links,
Gastwappen und Name rechts vom mittigen Ergebnis. Darunter stehen
Wettbewerb, Spieltag beziehungsweise Runde und Saison. Eine Entscheidung
nach Verlängerung trägt „n. V.“, ein Elfmeterschießen „i. E.“. Bei letzterem
zeigt die Paarungszeile das separate Ergebnis des Elfmeterschießens.

Die Paarungszeile führt unmittelbar in den **Spielbericht**, ohne
zusätzlichen Informationsdialog oder aktuelles Vereinsprofil. Der Kopf
zeigt die archivierten Vereinsnamen, Wappenreferenzen, menschlichen
Manager beziehungsweise „Computer“ und den Spielendstand ohne
Elfmeterschießen. Reguläre Spielzeit, zusätzliche Verlängerungstore und
Elfmeterschießen stehen getrennt darunter. Beispiel: regulär `1 : 1`,
Verlängerung `1 : 1`, Spielendstand `2 : 2`, Elfmeterschießen `4 : 3`.
`result.extra` enthält ausschließlich die in der Verlängerung zusätzlich
gefallenen Tore.

Schiedsrichter, Zuschauer, Temperatur und Regen gehören ebenfalls zum
Spielbericht. Die beiden Reiter **Aufstellungen** und **Ereignisse**
wechseln den darunterliegenden Inhalt. Aufstellungen zeigen beide Seiten
mit Startelf und der gesamten überlieferten Bank. Die angezeigten
Laufnummern sind Zeilenpositionen, keine behaupteten historischen
Rückennummern. Fehlende Aufstellungen heißen „Keine Aufstellung
überliefert.“; fehlende Einzelangaben erscheinen als „—“. Eine bekannte
Null bleibt dagegen sichtbar, etwa `0` Zuschauer, `0 °C` oder `0 / 7` Regen.

Die Ereignisliste enthält Tor, Fehlschuss, Gelb, Gelb-Rot, Rot,
Verletzung, Wechsel und jeden Versuch des Elfmeterschießens. Tore und
Fehlschüsse nennen Spieler, Vorlage beziehungsweise Zuspiel und die Art
Spiel, Elfmeter/Freistoß, Nachschuss oder Ecke. Beim Wechsel stehen Ein-
und Auswechslung ausdrücklich getrennt. Im Elfmeterschießen bleiben
Treffer und Fehlversuche einzeln sichtbar. Ereignisse derselben Minute
behalten ihre ursprüngliche Reihenfolge; die symbolische Minute 121 wird
als fortlaufender Versuch „i. E.“ angezeigt.

„Zurück“ und Escape führen vom Spielbericht zu den Tagespaarungen, von
dort zum Jahreskalender und schließlich zum Desktop. Der Fokus kehrt
jeweils zur aufrufenden Zeile, zum Datum oder zum Desktop-Icon zurück.
Escape schließt zuerst eine offene Jahresauswahl. Jahreswechsel setzen
Tag und Spielbericht zurück. Die aktive Registerkarte wird beim Öffnen
eines anderen Spielberichts wieder auf „Aufstellungen“ gesetzt.

## Präsentationsquelle und Speicherung

[DataSource::read_almanac()](../../../../src/modern/ui/model.hpp) ist eine
konstante Leseoperation. Die Standardimplementierung meldet
`almanac_unavailable`. `DemoSource` besitzt ein unveränderliches
Pseudoarchiv außerhalb von `Snapshot.state` und den UI-Demoständen.
Alle Abfragen verwenden das gespeicherte Demodatum als `as_of`: Sichtbar
sind ausschließlich Spiele bis einschließlich dieses Tages. Auch eine
direkte Abfrage einer bekannten zukünftigen Match-ID liefert keinen
Datensatz.

Jahr, Tag, Match-ID, Registerkarte und Jahresauswahl sind nur flüchtige
Felder der App. Navigation ruft weder `Source.apply()` auf noch erhöht
sie die Revision oder setzt den Spielstand auf geändert. Sie verändert
keine Finanzen, Manager, Kader oder Zufallsströme. Das Archiv wird beim
Speichern nicht in jeden Snapshot oder UI-Checkpoint kopiert.

Die Demo erzeugt zwölf Spiele aus zwei abgeschlossenen Saisons vor der
Saison von `manifest.start_date`; ohne dieses Feld gilt `2026-07-01` als
Demoausgangspunkt. Das ergibt drei Kalenderjahre mit mehreren Partien an
einem Tag, regulären Ergebnissen, Verlängerung und einem vollständigen
Elfmeterschießen. Vereins- und Spielernamen werden beim Aufbau aus dem
Katalog kopiert. Namen werden danach nicht aus dem aktuellen Kader neu
ermittelt. Fehlende Kader werden nicht durch erfundene Spieler ersetzt.
Die HTML-Studie verwendet eigene Pseudokader; deren Namen und Werte sind
keine zusätzlichen OBW-Daten.

Das neue Desktop-Objekt heißt `app-almanac`. Ein mitgeliefertes SVG-Icon
und die GUI-Ressourcenaufbereitung machen es unabhängig von zusätzlichen
Weltbildern verfügbar. `migrate_desktop()` ergänzt bei älteren gültigen
Layouts das fehlende Icon an einem freien Platz. Vorhandene Objekte
bleiben unverändert. Beschädigte bestehende Layoutdaten werden atomar
abgewiesen und nicht teilweise repariert. Gespeichert wird weiterhin nur
das Desktop-Layout, nicht die Almanachnavigation oder das Pseudoarchiv.

## Enginegrenze und Prüfstand

Der Renderer liest ausschließlich die Projektion über die Datenquelle.
Er öffnet keine `.obs`-Datei, dekodiert kein F10 und greift nicht auf
`ModernState` oder die laufende Session zu. Der vorhandene
[Engine-Almanach](../../../new_engine/almanach.md) und sein Speicherformat
bleiben durch dieses UI-Paket unverändert. Eine spätere konstante
Modern-Sessionprojektion soll benötigte Saisons bei Bedarf dekodieren und
dieselben Ansichten liefern. Die konkreten F10-Grenzen und die Regeln für
unbekannte Werte stehen im [Datenvertrag](DATA_CONTRACT.md).

Für das integrierte Paket sind 19 automatische Tests grün. Dazu gehören
die neuen synthetischen Providerprüfungen sowie Source-, Core- und
Desktopmigrationstests. Geprüft sind unter anderem Cutoff und leeres
Archiv, historische Namen, getrennte Ergebnisphasen, Aufstellung und Bank,
Ereignisse und Shootout, ungültige Eingaben sowie unveränderte Snapshots,
Revisionen und Änderungsmarkierungen bei Leseabfragen.

Die unabhängige native Laufzeitprüfung besteht: sechs Matrixläufe in drei
Themes bei 720p/1080p mit 96 dokumentierten Zuständen, gezielte Text-/Icon-
Nachaufnahmen, 25 Navigations-/Cutoff-/Dirty-Zustände und fünf Fokusproben.
Beide 20er-Kader sind bis zum Bankende erreichbar, ebenso alle zehn
Shootoutversuche. Die Archivnavigation verändert weder Revision noch Daten
oder vorhandene Dirty-Markierungen. Altansichten werden additiv migriert;
reines Blättern löst beim Beenden keine Speichernachfrage aus.

Die HTML-Prüfung ergänzt leeres Archiv, heute/zukünftig, unbekannte Menschen,
Nullwerte sowie direktes Elfmeterschießen ohne Verlängerung. Diese Sonderfälle
sind im nativen Renderer zusätzlich quellgeprüft; native Standard-, ET-, PK-
und Fehlwertfälle wurden tatsächlich bedient. Bilder, Skripte, Inputs und
Checkpoints liegen privat unter `bmh_4k_ui/docs/qa/almanac-2026-10-24/`.
Die Belege betreffen Linux/OpenGL und Browser, keine vollständige native
Windows-/macOS-Abnahme. Eine F10-/OBS-Anbindung ist weiterhin nicht enthalten.
