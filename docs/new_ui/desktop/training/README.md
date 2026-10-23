# Training · native Modern UI

Stand: 23. Oktober 2026. Verbindliche Quelle: privates HTML-Mockup
`bmh_4k_ui`, Revision `a349d0a47733c8f5b0c330b0c60b31c86e5a2106`,
`desktop/squad-menus.js`, `squad-menus.css` und `table-window.css`.
Der native Port liegt in `src/modern/ui/sports_screens.inc`.

Die Nutzerkorrektur vom 23. Oktober 2026 erweitert diese Vorlage: Die
Namensspalte nutzt nun 760 statt 300 logische Pixel. Dafür werden Schwerpunkt
(510 → 330) und Intensität (497 → 217) schmaler; Positions- und Stärkespalte
behalten ihre Breite. Das HTML-Mockup und der native Port erhalten dieselbe
Aufteilung. Der Namensinhalt steht mit 20 Pixeln Innenabstand an derselben
linken Kante wie „Spieler“ im Kopf. Ein eigener 720 Pixel breiter Textbereich
hält lange Namen innerhalb ihrer Zelle; drei Intensitätsbälle und die
Schwerpunktauswahl bleiben vollständig bedienbar.

## Darstellung und Bedienung

- Neun Übungen mit dem unveränderten GUI-Iconatlas und drei Bällen je Zeile:
  Kondition, Technik, Eckball, Freistoß, Zweikampf, Pass, Schuss,
  Schnelligkeit und Teamplay.
- Vier Trainingsarten auf dem gezeichneten Spielfeld; Automatik für Arten,
  Kondition/Technik, Fertigkeiten und Spieler sowie der gemeinsame Schalter.
- Rechts der vollständige OBW-Kader mit Position, Name, Stärke,
  Schwerpunkt und Intensität. Lange Namen werden mit Abstand zur Stärke
  gekürzt. Die Liste scrollt bei größeren Kadern.
- Ein bereits gewählter Intensitätsball schaltet die Spielerintensität auf
  null. Escape verwirft die Schwerpunktwahl; Fokus und Listenposition bleiben
  bei Änderungen erhalten.
- Automatik sperrt manuelle Eingaben der jeweiligen Gruppe. Die
  Spielerautomatik behält Schwerpunkte bei; der gemeinsame Schalter schaltet
  alle vier Gruppen zusammen.

Der Demoanbieter übernimmt die HTML-Vorschaubudgets 8/4/18 für Arten,
Kondition/Technik und Fertigkeiten. Ab 15 Fertigkeitsbällen zeigt er hohe
Belastung. Überschreitungen werden ohne teilweise Übernahme abgewiesen.
**Dies sind UI-Dummydaten, keine Trainingsregel oder Spielerentwicklung.**

## Daten und spätere Engine-Anbindung

`Snapshot.state.training` enthält Vereins-ID, neun Übungswerte, vier
Artenwerte, vier Automatikflags sowie Intensität/Schwerpunkt nach Spieler-ID.
Revisionierte `training`-Aktionen gehen ausschließlich an `DemoSource`.
Der erste Manager bestimmt den angezeigten Kader. Ein Vereinswechsel erzeugt
eine passende neue Vorschau. Ältere UI-Demostände werden beim Laden ergänzt;
echte `.obs`-Spielstände bleiben davon unberührt.

Der spätere Sessionadapter muss Budgets, Automatik und Wirkungen liefern.
Die Verbindung zur globalen Einstellungsautomatik steht noch aus.
Namen/Positionen/Stärke stammen aus dem schreibgeschützten OBW-Katalog;
Icons und Ballgrafik sind [GUI-Ressourcen](../../assets.md).
Die Referenz hat 16 fiktive Spieler, der geprüfte Diamond-Kader 20.
Diese Datenabweichung wird nicht durch Entfernen von Spielern kaschiert.

## QA

Die Spaltenkorrektur ist in allen drei Themes bei 720p und 1080p mit echten
Rendererbildern geprüft. Überschrift und Namen beginnen an derselben Kante;
die Namens-, Select- und Pfeilmitten sowie die geöffneten Optionszeilen sind
vertikal ausgerichtet. Alle 20 Namen des Prüfkaders passen vollständig.
Auch nach Scrollen bleiben die drei Intensitätsbälle vor der Scrollleiste.
Ein zusätzlicher Inline-Baseline-Abstand im nativen Select wurde durch
Blockdarstellung beseitigt; die Optionszeilen nutzen ihren vollständigen
Inhaltsbereich zur vertikalen Zentrierung.

Zwölf tatsächliche Trainings-/Taktikläufe decken drei Themes und zwei Größen
(1280 × 720, 1920 × 1080) ab. Geprüft: Budgets, Überlastungsfarbe,
Schwerpunktwahl/Escape, Intensität null, Automatik, echtes Mausrad,
Scroll-/Fokuserhalt, Wiederöffnen und gespeicherte Wiederherstellung.
Die Budgetumbrüche wurden in allen sechs Training-Konfigurationen nachgeprüft.
Synthetische Tests ergänzen atomare Ablehnung, Revisionen und Migration.

Die gemeinsame Dropdown-Markierungsspalte und geringe Unterschiede der
Kantenglättung bleiben dokumentierte Abweichungen. Screenshots, Eingaben und
passende Demostände: [wiederholbare Menü-QA](../../menu-qa.md).
