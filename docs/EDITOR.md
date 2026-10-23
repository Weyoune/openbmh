# OpenBMH Editor 0.5.2

Der eigenständige, native Editor verwendet **RmlUI, SDL2 und OpenGL**. Er bearbeitet Startdatenbanken, keine laufenden Spielstände. Das Spiel und seine Classic-Prüfung bleiben unverändert. Editor 0.5.2 hat eine eigene Versionsreihe und den Release-Tag `editor-v0.5.2`; die Spielversion ist 0.2.0.

## Starten und Daten öffnen

Das vollständige Paket `OpenBMHEditor-0.5.2-<plattform>.zip` entpacken und `OpenBMHEditor.exe` (Windows) beziehungsweise `OpenBMHEditor` (Linux/macOS) starten. `assets/editor` muss beim Programm bleiben. Auf macOS kann die unsignierte Anwendung über **Rechtsklick → Öffnen** gestartet werden. Eine Signatur/Notarisierung ist nicht Bestandteil dieser Version. `OpenBMHEditor --version` zeigt die Editorversion ohne Grafikstart.

**Öffnen** bietet OBW-Dateien und entpackte Welten an. **BMH / FLTD importieren** liest einen Ordner mit einer unterstützten BMH- oder Football-Limited-Installation durch den bestehenden Konverter. Das Original wird nicht überschrieben. Eine bestehende Classic-OBW wird zunächst überprüft und dann in eine bearbeitbare Editorwelt übernommen.

**Demo öffnen** lädt ausschließlich synthetische Testdaten. Echte Vereine, Spielerdaten, Fotos und importierte Originalgrafiken gehören in getrennte, private Datenpakete.

## Bearbeiten

- Links einen Datenbereich auswählen; die Liste lässt sich durchsuchen, filtern, sortieren und seitenweise durchgehen. Rechts Änderungen mit **Anwenden** übernehmen. **Speichern** übernimmt auch die aktuellen Eingaben.
- Länder, Wettbewerbe, Vereine, Spieler, Kader, Stadien und Schiedsrichter lassen sich anlegen und bearbeiten. Referenzierte Datensätze werden erst nach Änderung ihrer Verknüpfungen gelöscht. Vereinswechsel zwischen Ligen aktualisieren auch deren Mitgliederlisten.
- Spielwerte behalten ihre **quellenspezifischen Wertebereiche**. Ein Classic-Wert wird nicht auf eine erfundene 1–10-Skala umgerechnet. Neue Spieler ohne belegte Werte bleiben zunächst unbewertet.
- Kader bleiben vollständig erhalten. Die Schaltfläche **24** schaltet lediglich die Seitengröße um. Spieler lassen sich über das Vereinsfeld verschieben, zum Tausch merken und mit einem anderen Spieler tauschen; die Pfeile ändern ihre Reihenfolge.
- **Prüfen** zeigt überlieferte Quellenkonflikte. Eine Auswahl übernimmt den gewählten Wert; die übrigen Quellen bleiben erhalten. Fehlende Daten sind Hinweise, fehlerhafte Verknüpfungen oder unzulässige Werte blockieren Änderungen und Speichern.
- Rückgängig/Wiederholen gilt gemeinsam für native Oberfläche und MCP. Noch nicht angewendete Eingaben werden bei externen Änderungen nicht automatisch ersetzt; eine veraltete Änderung muss nach Aktualisierung erneut geprüft werden.
- **Bilder** und die Bildaktionen am Datensatz unterstützen PNG, JPEG, WebP und eigenständige SVG. Vor dem Import: Ausschnitt ziehen oder Werte eingeben, frei drehen, spiegeln. Ein unveränderter Import behält seine Originalbytes. Bearbeitete Rasterbilder werden PNG, SVG bleibt Vektor. Das Original und die Transformationswerte bleiben für erneutes Bearbeiten im Paket. Classic-Porträts werden aus ihren eingefrorenen Rezepten angezeigt; eine Umbenennung ändert das Gesicht nicht.
- Bildänderungen am Spieler, Verein oder Wettbewerb erhalten eine eigene Zuordnung. Gemeinsame Platzhalter und Bilder anderer Datensätze bleiben dabei erhalten. Im Bereich **Bilder** kann ein gemeinsam verwendetes Bild bewusst zentral bearbeitet werden.

**Speichern** erzeugt ausschließlich `.obw` im Profil **`editor-world-v1`, Format 1.1**. Editorwelten sind **noch nicht im Classic-Spiel startbar**. Die Bezeichnung ist im Editor sichtbar. Unbekannte Erweiterungen, nicht bearbeitete Dokumente, Quellnachweise und Bilder bleiben erhalten. Noch nicht im Formular angebotene Regeln, Kalender- und Finanzdaten werden erhalten, aber nicht als vollständig editierbare Bereiche ausgegeben.

Shortcuts: Ctrl/Cmd+O, Ctrl/Cmd+S, Shift+Ctrl/Cmd+S, Ctrl/Cmd+Z und Shift+Ctrl/Cmd+Z. In Textfeldern gilt die lokale Text-Rückgängig-Funktion.

## Stärken und Gehälter vergleichen

**Wertevergleich** in der Seitenleiste (oder die gleichnamige Schaltfläche bei Spielern, Vereinen und Wettbewerben) öffnet eine Übersicht mit drei Ansichten: **Spieler**, **Mannschaften**, **Ligen**. Spieler zeigen ihre einzelnen Spielwerte und das Gehalt je Saison; Mannschaften und Ligen zeigen Durchschnittswerte, Spielergehaltssummen, das Durchschnittsgehalt und die Anzahl bekannter Gehälter. Ein Klick auf eine Zeile öffnet ihren Datensatz zur Bearbeitung.

- Auf einen Spaltenkopf klicken, um auf- oder absteigend zu sortieren. Sortiert wird der gesamte gefilterte Bestand vor der Aufteilung auf Seiten mit bis zu 200 Zeilen. Suche sowie Land-, Liga- und Vereinsfilter grenzen die Ansicht ein. Fehlende Zahlen stehen in beiden Sortierrichtungen hinten.
- **Spalten auswählen** blendet beliebige vorhandene Spielwerte ein. Bei vielen Spalten horizontal scrollen; Tabellenkopf und Werte bewegen sich gemeinsam. Die Wertebereiche stehen im Spaltenkopf, Gruppenwerte sind spielergewichtete Durchschnitte über bekannte Werte. Der gesamte Kader einschließlich Reserve zählt; reine Quellenduplikate mit `x-simulation.squad_role: source-only-duplicate` werden ausgeschlossen und ihre Anzahl angezeigt.
- Verschiedene Attributsysteme bleiben getrennt. Bei einem einzigen System werden dessen Spielwerte automatisch angezeigt. Bei mehreren ein **Attributsystem** wählen, um dessen Spieler und die daraus gebildeten Mannschafts-/Ligawerte zu vergleichen. Das FC-Attribut `strength` heißt in einem System mit `overall` **Körperkraft**; es wird nicht als Classic-Gesamtstärke interpretiert. Es wird keine neue Stärkeskala berechnet.
- **Gehälter gelten je Saison**, in der Währung und kleinsten Einheit aus dem OBW-Manifest. Berücksichtigt werden zum Saisonstart gültige Verträge des Spielers mit seiner aktuellen Mannschaft. Personalverträge, abgelaufene und zukünftige Verträge zählen nicht. Mehrere gleichzeitig gültige Verträge derselben Zuordnung sind unklar und werden nicht addiert. Ohne Vertragsangabe steht **—**, ein ausdrücklich gespeichertes Nullgehalt bleibt **0**.
- Unvollständige Gehaltssummen und Durchschnitte tragen ein **\***. Sie berücksichtigen nur bekannte Spielergehälter; die Spalte **Gehälter bekannt** zeigt die Abdeckung. Auch Spielwertdurchschnitte verwenden nur bekannte Werte (Anzahl per Tooltip). Classic-Grunddaten liefern häufig keine konkreten Verträge: Der Editor erfindet diese Gehälter nicht.

[Native Vorher-/Nachher-Bilder](qa/editor-comparison/README.md) dokumentieren die Ansichten und Spaltenauswahl.

Die Übersicht verändert weder Daten noch Undo-Historie. Änderungen und Rückgängig aktualisieren ihre Werte automatisch. Alle drei Themes und beide Sprachen werden unterstützt.

Über MCP liefert **`editor_compare`** dieselbe schreibgeschützte Übersicht. Beispielargumente:

```json
{"table":"players","sort":"attributes.overall","descending":true,"limit":200}
```

`table` ist `players`, `clubs` oder `leagues`. Weitere Sortierschlüssel sind `name`, `salary` (Spieler), `salary_total`, `salary_average`, `salary_known`, `player_count`, `club_count` sowie `attributes.<feld>`. `attribute_set` grenzt das System ein, `search`, `nation`, `competition`, `team` filtern, `offset`/`limit` blättern. Die Antwort enthält Originalskalen, Stichtag, Währung und Abdeckung. Gehälter bleiben Integer in kleinster Währungseinheit; das Durchschnittsgehalt wird auf diese Einheit gerundet. Das entsprechende Session-Kommando für Command-Scripts heißt `compare`.

## Optionen und MCP

**Optionen** bietet Deutsch/Englisch, Midnight Glass/Silver Glass/Matchday und den integrierten MCP-Server. Die Sprache ändert die Oberfläche; Namen und Quelldaten werden nicht übersetzt. Einstellungen werden im Benutzerprofil gespeichert.

Unter **Darstellung** stehen die drei Farbwelten der modernen Untermenüs zur Auswahl:

- **Midnight Glass:** Schiefergrau mit dezenten Mint-Akzenten.
- **Silver Glass:** helle silbergraue Flächen mit Petrol-Akzenten.
- **Matchday:** Graphit mit warmen Korall-Akzenten.

**Anwenden** wechselt das Theme ohne Neustart. Die Farbwahl gilt auch für Tabellen, Eigenschaften, Dropdowns, Meldungen und die Bildbearbeitung. [Native Vergleichsbilder](qa/editor-poc/README.md#theme-comparison) zeigen alle drei mit denselben synthetischen Daten.

Der MCP-Server ist standardmäßig aktiviert und bindet ausschließlich `127.0.0.1`, standardmäßig Port `8765`. Port `0` wählt einen freien Port. Status und tatsächlicher Port sind in Optionen sichtbar. **Verbindung kopieren** kopiert eine JSON-Konfiguration einschließlich Bearer-Token für HTTP-fähige MCP-Clients. Das Token bleibt lokal im Benutzerprofil und wird nur auf ausdrückliches Kopieren/CLI-Anfordern ausgegeben. Ein anderer bereits laufender Editor kann den Standardport belegen; dann einen anderen Port wählen.

Implementiert ist [MCP Streamable HTTP 2025-11-25](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports), mit Aushandlung von 2025-06-18; JSON-Antworten, ohne optionale SSE-Sitzungen. Neuere MCP-Versionen werden nicht als unterstützt beworben. Der Server prüft Token, Host und gegebenenfalls Origin. Oberfläche und Agent bearbeiten dieselbe Welt, verwenden dieselbe Validierung und dieselbe Undo-Historie. Für konkurrierende Änderungen `expected_revision` aus `editor_info`/`editor_get` mitsenden.

Werkzeuge: `editor_info`, `editor_open`, `editor_new`, `editor_list`, `editor_compare`, `editor_regulations`, `editor_get`, `editor_apply`, `editor_rules`, `editor_validate`, `editor_history`, `editor_save`, `editor_undo`, `editor_redo`, `editor_roster_move`, `editor_roster_swap`, `editor_roster_order`, `editor_review_resolve`, `editor_image_import`, `editor_image_edit`, `editor_image_preview`, `editor_image_export`. Die eingeblendeten Tools enthalten Parameterschemata und Beschreibungen. Öffnen einer anderen Welt verwirft ungespeicherte Änderungen nur mit `discard: true`.

## Spiel- und Ligaregeln ansehen

**Regelwerk** zeigt Auf- und Abstieg, Ziel-Ligen, Relegation und internationale
Qualifikation. Wettbewerbe lassen sich durchsuchen und nach Land filtern.
**Spielregeln** zeigt die übernommenen Einträge aus `world/rules.json`, etwa
Wertebereiche, Kreditfaktoren, Trainingslager und Classic-Verfahren. Phasen und
weitere Originalfelder sind ebenfalls einsehbar. Dieser Bereich ist vollständig
schreibgeschützt; er verändert weder Welt noch Undo-Historie.

Explizite OBW-Regeln werden direkt dargestellt. Bei Classic-Importen kennzeichnet
die Ansicht die versionierten Classic-Verfahrensregeln. Parallel-Ligen behalten
Meister plus bedingte Relegationsplätze; die unterste Liga nutzt den Nachrückerpool.
Internationale Plätze folgen dem ursprünglichen Champions-League-/UEFA-/
Pokalsieger-Cup-Verfahren. Nicht festgelegte Regeln bleiben als solche sichtbar.
Das sind keine heutigen UEFA-Regeln und kein neuer Saison-Simulator.
`editor_regulations` bietet dieselben Informationen schreibgeschützt über MCP.

## Kompilieren

Voraussetzungen entsprechen dem Hauptprojekt: C++20-Compiler, Git, CMake ≥ 3.24 und Ninja. Windows: Visual Studio 2022 C++ Build Tools / x64 Native Tools Prompt; macOS ARM: Xcode Command Line Tools, CMake und Ninja; Linux: GTK3-, X11- und OpenGL-Entwicklungspakete wie im Haupt-README. Die Grafikausgabe benötigt OpenGL 3.3, auf macOS OpenGL 4.1.

```sh
cmake -S . -B build-editor -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENBMH_BUILD_EDITOR=ON
cmake --build build-editor --target openbmh_editor editor_core_tests editor_comparison_tests editor_regulations_tests --parallel 2
ctest --test-dir build-editor -C Release -R '^editor_(core_tests|comparison_tests|regulations_tests|version)$' --output-on-failure
```

macOS ARM zusätzlich bei der Konfiguration: `-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`. Unter Windows das Argument nach `-R` ohne einfache Shell-Anführungszeichen verwenden. Der Editor wird beim normalen Spiel-Build standardmäßig nicht mitgebaut.

```sh
build-editor/OpenBMHEditor --demo
build-editor/OpenBMHEditor --headless --open world.obw --validate
build-editor/OpenBMHEditor --headless --open world.obw --output edited.obw
build-editor/OpenBMHEditor --serve --open world.obw --mcp-port 8765
build-editor/OpenBMHEditor --stdio-mcp --open world.obw
```

Weitere Argumente: `--settings datei.json`, `--assets verzeichnis`, `--commands script.json`, `--print-mcp-config`. Ein Command-Script ist ein Array aus `{"command":"…","args":{…}}`, mit denselben Kommandos wie die gemeinsame Session (z. B. `get`, `apply`, `save`). `--headless` startet kein Fenster. `--stdio-mcp` bietet dieselben Werkzeuge über newline-delimited JSON-RPC; stdout enthält dabei ausschließlich Protokollnachrichten. Testaufnahmen: `--screenshot bild.png --quit-after-ms 1800 --page clubs` (unter Linux mit Xvfb möglich).

Pakete baut `tools/package/make_editor_package.py --build build-editor --out dist --platform linux-x64` (oder `windows-x64`, `macos-arm64`). `--source-package` ergänzt den zugehörigen Quellcode mit den tatsächlich verwendeten, gepinnten Abhängigkeiten. Die CI **OBW Editor** baut und prüft alle drei Plattformen. Tags `editor-v<version>` erzeugen nach erfolgreicher Prüfung einen Release-Entwurf. Die nativen Tests unterscheiden sich von einem späteren menschlichen GUI-Test auf Windows/macOS.

## English quick start

Extract the complete package and run **OpenBMHEditor**. Open an OBW archive or an unpacked world, import a supported BMH/Football Limited installation, or open the synthetic demo. Use the sidebar, searchable/filterable list and property inspector; **Apply** records one shared, undoable transaction. **Save** writes only an **OBW 1.1 editor world**, not a playable Classic mod or a legacy installation.

Use **Comparison** to inspect sortable player, team and league ratings and per-season salaries. Select attribute columns and filter by country, league, club or attribute system. Group ratings are player-weighted means; salary totals and means use known player contracts only and explicitly flag incomplete coverage. Missing values remain unknown. The read-only MCP tool `editor_compare` returns the same data.

Use **Regulations** to inspect promotion, relegation, playoffs, international qualification and stored game rules. Explicit OBW declarations and versioned Classic policies are distinguished; missing rules stay unspecified. This page and `editor_regulations` are read only.

The editor retains full squads, original image data, source claims and unknown extensions. Its 24-player view does not remove players. Image import offers normalized crop, arbitrary rotation and mirroring; re-editing uses the preserved source. Classic portraits keep their frozen appearance recipe. Unsupported editing areas are preserved rather than replaced.

In **Options**, switch the interface to **English**, choose one of the three themes, and enable/disable the integrated local MCP server. **Copy connection** provides the authenticated configuration. The server defaults to loopback port 8765; use port 0 for a free port. GUI and MCP share the same world, validation and undo history. It supports MCP Streamable HTTP 2025-11-25/2025-06-18 and optional stdio mode. Use `expected_revision` for concurrent edits.

Build with the three commands above, including `-DOPENBMH_BUILD_EDITOR=ON`. The native application requires OpenGL 3.3 (macOS 4.1) and the platform development dependencies documented in the main README. Editor 0.5.2 is unsigned and does not bundle any real-world dataset or proprietary game assets.
