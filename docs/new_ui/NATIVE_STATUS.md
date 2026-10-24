# Native Modern UI · erster Umsetzungsstand

Stand: 24. Oktober 2026. **Experimenteller RmlUI-Unterbau und erster
bedienbarer Durchlauf. Noch keine vollständige Umsetzung des HTML-Mockups.**
Die normale Classic-Konfiguration bleibt unverändert; die neue Oberfläche
wird ausdrücklich mit `OPENBMH_BUILD_MODERN_UI=ON` gebaut.

## Vorhanden

- Gemeinsamer SDL2-/RmlUI-Unterbau mit den vorhandenen OpenGL-/Direct3D-
  Renderern, den drei Farbpaletten und dem 2720 × 2040 großen 4:3-Fenster.
- Schreibgeschützter OBW-Katalog für Diamond Modern: Vereine, Ligen, Spieler,
  Wappen und Fotos. SVG-Wappen werden gegenüber kleinen Legacy-Bildern bevorzugt.
- Eigene GUI-Ressourcen für Hintergründe, Desktop-Icons, Managerporträts,
  Karten und Trainingslagerbilder. Der Vorbereitungsbefehl verändert weder
  das Mockup noch die Weltdatei.
- Hauptmenü und Mod-Auswahl; Grundeinstellungen mit vier großen Auswahlfeldern;
  Manager-/Vereinswahl mit Foto- und Illustrationsporträts. Ausländische
  Vereine bleiben sichtbar und sind für Manager gesperrt.
- Drei Teilnehmerseiten, automatische Demo-Auslosung mit Pause/Überspringen
  und Übersicht; in dieser Folge gibt es keinen Schließen-Knopf.
- Desktop mit Statusleiste, Buttons, fixierter Standardanordnung, Raster und
  freiem Verschieben, Rechtsklickmenü, gemeinsamer Fixierung, Rückgängig,
  Widgetauswahl und den vorgesehenen Widgetgrößen. Kurven-/Statistikinhalte
  und die Widgetvorschauen benötigen noch den detaillierten Mockup-Abgleich.
- Eintrittspreis-Popover: 330 × 196 Punkte, Entwurf, gesonderte Bestätigung,
  Abbrechen, Fehler-/Revisionsschutz und Tastaturbedienung.
- Trainingslager: drei Karten, 18 Angebote, eigene GUI-Bilder und lokale
  Vormerkung. Speichern/Laden sind als erste native Ansichten vorhanden;
  ihr vollständiger Bedienungsabgleich bleibt offen.
- Separate UI-Demostände und Desktopansichten. Ansichten speichern zusätzlich
  einen Screenshot; die Standardansicht kann nicht gelöscht werden.
- [Training](desktop/training/README.md): neun Übungen, vier Arten, Kader mit
  Schwerpunkt/Intensität, Automatikgruppen und gemeinsame Automatik.
- [Taktik](desktop/tactics/README.md): sieben interaktive Diagrammflächen,
  zyklische Einstellungen und Einsatzregler. Die Grafik aktualisiert sich
  unmittelbar und speichert ausschließlich UI-Vorschaueinstellungen.
- [Kalender](desktop/calendar/README.md): zwölf Monate mit der originalen
  Wochentagsanordnung, Tageswahl und Länder-/Ligavorschau; 18/20 Vereine,
  Schaltjahre und gespeicherte Auswahl werden berücksichtigt.

- [Berichte](desktop/reports/README.md): Tabellen und Stärken mit Sortierung,
  Länder-/Ligaauswahl und 18/20 Zeilen; Ligaspiele und übrige Spiele mit
  Rundennavigation. Gemeinsame Auswahlmenüs sowie Vereins-, Kader- und
  Spielerprofile einschließlich Wappen und Porträts. Ergebnisse, Tabellen
  und Qualifikationsfarben bleiben ausdrücklich Demo-Projektionen.

- [Freundschaftsspiele](desktop/friendlies/README.md): inländische und
  ausländische Gegner, Austragungsort und Kostenbestätigung, freier Termin,
  bis zu 20 lokale Vormerkungen und sortierte Übersicht.
- [Verlegen](desktop/postpone/README.md): Ausgangsspiel und neuer Termin,
  Originaltag als stabile Identität, erneute Verlegung und Rücknahme mit
  belegtem Originaltermin als Sperrfall. Änderungen erscheinen im Kalender.
- [Hallentabellen](desktop/indoor/README.md): bis zu 16 Vereine aus der ersten
  deutschen Liga, unabhängige Platz-/Punktesortierung, Vereinsprofile und Info.

- [Almanach](desktop/almanac/README.md): eigenes Desktopicon, Jahreskalender,
  Tagespaarungen und Spielberichte mit Phasenergebnissen, menschlichen Managern,
  Schiedsrichter, Zuschauern, Wetter, beiden Aufstellungen/Bank und Ereignissen.
  Die Vorschau verwendet unveränderliche Pseudodatensätze über zwei Saisons;
  die vorbereitete Leseprojektion ist noch nicht an F10/OBS angeschlossen.

- [Bank](desktop/bank/README.md): anklickbare Bankhalle, Sparbuch,
  Immobilien, Aktien, Kredit mit Tilgungsverlauf und Vermögensübersicht.
- [Bilanz](desktop/balance/README.md): aktuelle Einnahmen/Ausgaben und
  getrennte Zukunftsplanung, mit gemeinsamem Kontostand und Eintrittspreis.
- [Verträge](desktop/contracts/README.md): tatsächlicher OBW-Kader,
  drei Datenseiten, Gehaltsangebot/Laufzeit, Versicherung und Prämien.
  Die gemeinsame Zahleneingabe bietet sechs Vorschlagswerte, Tastaturbedienung,
  lokale Entwürfe und eine zweite Bestätigung vor Geldänderungen.

**Anstoß führt keine Aktion aus**, weder per Maus noch per Tastatur. Das gilt
auch für die direkte Aktionsschnittstelle des Demoanbieters.

## Noch offen

Die übrigen Desktop-Untermenüs werden in zusammengehörigen Paketen vom
HTML-Mockup übertragen. Für sie erscheinen derzeit keine vereinfachten Ersatzlayouts.
Der Einstellungsdialog, die vollständigen Widgetinhalte,
Mod-Importkomfort und dauerhafte globale Voreinstellungen sind noch nicht
vollständig. Übersetzungen, echte Engine-Anbindung und Plattform-Abnahme
unter Windows/macOS folgen später. Dies ist kein freigegebenes Test-Release.
Die [QA-Befundliste](qa-findings.md) hält die noch offenen Abweichungen nach.

## Architekturgrenze für die Engine

Die Oberfläche liegt unter `src/modern/ui`, verwendet aber den Namespace
`bmh::modern_ui`. Damit verdeckt sie nicht `bmh::ui`, auf das die Modern-
Session des Engine-Forks derzeit zugreift.

`DataSource` liefert besitzende, unveränderliche `Snapshot`-Werte mit Revision
und nimmt `Action`-Anfragen entgegen. Aktuell implementiert ausschließlich
`DemoSource` diese Grenze. Keine Ansicht greift auf `GameState` zu; die Demo
ruft keine Modern-Session, Zufallsquelle oder Matchfunktion auf. Ihr `.json`-
Speicherformat ist ausdrücklich kein `.obs`-Spielstand und keine neue
Spielregel. Ein späterer Session-Adapter ersetzt den Demoanbieter.

Die bisher nur für den Editor verwendeten Archiv-/Bild-Hilfen sind als
`bmh_asset_io` geteilt. Editor-Oberfläche, MCP-Server und HTTP-Server werden
nicht in die Modern UI eingebunden.

## Lokal bauen und ansehen

```sh
python3 tools/modern_ui/prepare_gui_assets.py \
  --mockup /path/to/bmh_4k_ui --output /path/to/gui-resources
cmake -S . -B build-modern -DOPENBMH_BUILD_MODERN_UI=ON \
  -DBMH_MODERN_GUI_ASSETS=/path/to/gui-resources
cmake --build build-modern --target openbmh_play modern_ui_core_tests modern_reports_tests modern_finance_ui_tests modern_ticket_document_tests
ctest --test-dir build-modern -R '^modern_(ui_core|reports_|finance_ui_|ticket_)' --output-on-failure
./build-modern/OpenBMH --modern-ui-demo --mod /path/to/Diamond_Modern.obw
```

Bei Mehrkonfigurations-Generatoren liegt das Programm im Konfigurationsordner;
`--config Release` beziehungsweise `ctest -C Release` ergänzen. Die
Spieler-/Vereinsdaten und die privaten Referenzmotive werden nicht mit diesem
Quellcode verteilt. Ohne externe GUI-Ressourcen kann das Hauptmenü den bereits
mitgelieferten Launcher-Hintergrund verwenden.

Entwicklungsoptionen: `--gui-assets`, `--ui-scene`, `--ui-theme`, `--ui-size`,
`--ui-state`, `--ui-screenshot` und `--ui-script`. Das zeilenweise JSON-
Eingabeskript führt Maus-/Tastaturaktionen durch RmlUI aus; es ersetzt keine
Sessionaktionen durch direkte Zustandsmanipulation. `checkpoint` schreibt einen
wiederherstellbaren Demostand passend zu den Bildschirmaufnahmen.

## Prüfung

Synthetische Tests prüfen Katalog, HQ-Bildauswahl, ausländische Übernahmesperre,
Revisionsschutz, Desktop-Grenzen/Fixierung/Größen und atomare Demostand-
Wiederherstellung. Drei fensterlose RmlUI-Tests prüfen den Eintrittspreis in
allen Themes, einschließlich doppelter Pixeldichte und abgewiesener Übernahme.
Die [dedizierte Schrift-QA](typography-qa.md) prüft zusätzlich Buttons und
Dropdowns gegen das HTML-Mockup, einschließlich tatsächlicher Textmitten,
Pixeldichten sowie Escape- und Fokusverhalten.

Die [Menü-QA](menu-qa.md) hält den Gesamtkatalog einschließlich fehlender
Ports nach. Training/Taktik wurden in zwölf tatsächlichen Rendererläufen
mit drei Themes und zwei Auflösungen verglichen. Die native UI und die
synthetischen Kontrolltests bauen in der CI unter Linux, Windows und macOS.

Ein nativer Linux-/OpenGL-Durchlauf mit externem Diamond-Datensatz führt über
das Hauptmenü, die Managerwahl und Auslosung zum Desktop, verändert den
Eintrittspreis nach Bestätigung, öffnet das Nahost-Lager und prüft Verschieben
und Rückgängig. Bildschirmaufnahmen, Eingabeskripte und Demostände bleiben im
privaten Mockup-Repository. Windows-/macOS-Laufzeitverhalten wird damit nicht
als geprüft ausgegeben.
