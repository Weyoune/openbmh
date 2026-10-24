# Wiederholbare Menü-QA gegen HTML

Die HTML-Version ist der verbindliche Gestaltungsstandard. Der Vergleich
begleitet fortlaufend die Portierung bis Anstoß. Eine Bildschirmaufnahme
allein bedeutet keine visuelle Abnahme.

## Arbeitsweise

1. Unveränderte HTML-Quelle lesen; Browserbilder, Geometrie, Schrift und
   Interaktionszustände aufnehmen.
2. Native Ansicht mit Dummydaten hinter `DataSource` umsetzen.
   Datenabweichungen ausdrücklich nennen und das Referenzlayout beibehalten.
3. Dedizierten QA-Agenten den tatsächlichen Renderer gegen HTML prüfen lassen:
   drei Themes × 1280 × 720 und 1920 × 1080, einschließlich Buttons,
   Dropdowns, langer Texte, Scrollen, Fokus und Abbrechen.
4. Befunde mit Eingabeskript und ladbarem UI-Demostand belegen. Korrekturen
   gezielt nachprüfen; vollständige Interaktionsmatrix am Menü-Meilenstein.
5. Gesamtkatalog und offene Befunde fortschreiben. Fehlende Ports bleiben offen.

Das private Mockup-Repository enthält dafür
`tools/qa/native_mockup_compare.py` mit deutscher Anleitung.
`--scenes all` nimmt jede katalogisierte Ansicht bis Anstoß auf; fehlende
native Ports erhalten weiterhin eine HTML-Aufnahme. Native Fälle enthalten
PNG, DOM, Snapshot, Eingabeskript und ladbaren Demostand. Das Werkzeug ist
ein endlicher QA-Lauf. Der zusätzlich eingerichtete lokale Watcher startet
diesen Katalog nach stabilen Build- oder Referenzänderungen automatisch erneut.

Die Matrix trennt technische Aufnahme, Datenabgleich und visuelle Freigabe.
Reviewer, Zeitpunkt und fünf Prüfbereiche werden gespeichert. Unvollständige
Vergleiche bleiben `UNIMPLEMENTED` beziehungsweise `PENDING`. Protokolliert
werden außerdem Binärdatei-, Welt-, GUI- und Demostand-Hashes sowie Referenzrevision.

## Automatisierte Regression

`.github/workflows/modern-ui.yml` baut die aktivierte native UI unter Linux,
Windows und macOS. Fensterlose Tests mit synthetischen Daten prüfen unter
anderem Text-/Pfeilmitten, Dropdown-Schriftgrößen, Themes/Pixeldichten und das
Verwerfen von Eingaben. Private Ressourcen werden dort nicht vorausgesetzt.

CI ergänzt die tatsächlichen Renderer-/HTML-Vergleiche. Ein erfolgreicher
Build bestätigt keine visuelle Abnahme auf dem jeweiligen Betriebssystem.

Auf dem Entwicklungsserver läuft `openbmh-native-ui-qa.service` unabhängig
von einem aktiven Chat weiter. Er beobachtet fertiges Binary, ausgelieferte
Stylesheets, GUI-Index und HTML-Quelldateien; nach 60 Sekunden unverändertem
Stand erfasst er alle Menürouten in den drei Themes bei 1280 × 720. Er behält
die letzten drei eigenen abgeschlossenen Läufe. Identische Stände lösen keine
neuen Aufnahmen aus. Die größere Matrix und gezielte Bedienungsprüfungen
bleiben Teil der Agenten-Abnahme am Menü-Meilenstein.

Der Dienst erzeugt Bilder, Zustände, Logs und offene Reviewchecklisten.
Er erteilt keine automatische gestalterische Freigabe und behebt keine
erkannten Fehler eigenständig. Bedienung und Installationshinweise stehen
im privaten Mockup-Repository unter `tools/qa/README-watch-native.md`.

## Nachweise

Private Bilder und passende UI-Demostände liegen im Mockup-Repository unter
`docs/qa/native-rmlui-2026-10-23/`. Sie enthalten Weltinhalte und gehören
deshalb nicht ins öffentliche Quellcode-Repository. Öffentliche Zusammenfassung:

- [Nativer Stand](NATIVE_STATUS.md)
- [Typografie-Befunde](typography-qa.md)
- [Training](desktop/training/README.md) und [Taktik](desktop/tactics/README.md)

- [Berichte](desktop/reports/README.md): Tabellen/Stärken und zwei Spielübersichten
  als gemeinsames Paket; eigenständiger HTML-/Renderervergleich einschließlich
  20 Vereinen, Sortierung, Footerpickern und Vereins-/Spielerprofilen.

- [Verlegen](desktop/postpone/README.md),
  [Freundschaftsspiele](desktop/friendlies/README.md) und
  [Hallentabellen](desktop/indoor/README.md): ein gemeinsames Paket mit
  18 Rendererläufen, 43 Bedienzuständen und gesondertem OS-Beenden-/
  Speichern-Nachtest. Damals: 18 native und 18 offene Routen.

- [Almanach](desktop/almanac/README.md): Jahreskalender, Tagespartien und
  Spielbericht als neues readonly Menü; alle Ergebnisphasen, Kader/Bank,
  Ereignisse, alte Ansichten und unveränderte Revision/Dirty-Zustände geprüft.
  Nach diesem Paket: 19 native und 18 offene Routen.

- [Bank](desktop/bank/README.md), [Bilanz](desktop/balance/README.md) und
  [Verträge](desktop/contracts/README.md): gemeinsames Finanzpaket mit drei
  Themes bei 720p/1080p. Bankhalle und fünf Dienste, alle acht Transaktionen,
  Kredit-/Bestandsgrenzen, Entwürfe, doppelte Bestätigung und Abbruch geprüft.
  Zwölf Bilanz-/Vertragsläufe liefern 96 erfolgreiche Zustandsprüfungen;
  42 Text-/Layoutzustände zeigen keinen horizontalen Textüberlauf.
  Die 20 ausgewählten Regressionstests bestehen nach Übernahme der parallel
  eingetroffenen Engineänderungen. Screenshots, UI-Demostände und Eingabeskripte
  liegen privat unter `docs/qa/finance-batch-2026-10-24/`.
  Nach diesem Paket: **22 native und 15 noch offene Routen**.

Die Finanzprüfung umfasst echte Linux-/OpenGL-Aufnahmen. Windows-/macOS-
Laufzeitdarstellung wird dadurch nicht als abgenommen ausgegeben.

- [Umfeld](desktop/environment/README.md), [Werbung](desktop/advertising/README.md)
  und [TOTO](desktop/toto/README.md): weiteres gemeinsames Paket mit drei
  Themes bei 720p/1080p. Direkte Förderstufen, Angebotsannahme, Geldentwürfe,
  TOTO-Spalten und letzter Schein wurden durch die native Ereignisverarbeitung
  geprüft. TOTO: 138 erfolgreiche Zustandsprüfungen, 18 vermessene
  Layoutzustände und gezielte Schlussaufnahmen der korrigierten Elemente.
  Echte Checkpoints und Neustarts ergänzen die Browser-/Rendererprüfung.
  Die 24 Regressionstests bestehen. Private Nachweise:
  `docs/qa/club-batch-2026-10-24/`.
  Aktueller Katalog: **25 native und 12 noch offene Routen**.

CSS-Hintergrundbilder werden in der HTML-QA zusätzlich vollständig dekodiert,
bevor Screenshots entstehen. Das verhindert falsche Meldungen über fehlende
oder abgeschnittene Atlaskacheln im Umfeld beziehungsweise in Werbung.
