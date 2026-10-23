# Taktik · native Modern UI

Stand: 23. Oktober 2026. Quelle: privates HTML-Mockup `bmh_4k_ui`, Revision
`0bf0f2afd2ff8d5dabb96c795db2a5d0abaaf278`, `desktop/squad-menus.js`,
`squad-menus.css` und `table-window.css`. Der Port verwendet dieselbe
Geometrie, Farben und Interaktionsflächen.

## Darstellung und Bedienung

Die transparente Tafel liegt im gemeinsamen 4:3-Fenster. Spielfelder,
Spielerpunkte, Pfeile und Kurven entstehen als skalierbares Diagramm mit
erhaltenem Seitenverhältnis. Es gibt keine sichtbaren Hoverkästen;
Tastaturfokus bleibt sichtbar.

Sieben Flächen wechseln zyklisch Defensiv/offensiv, drei Pass-/Schussbilder,
flache/hohe Bälle und zwei Flügelbilder. Der dezente Regler stellt den
Einsatz von 0 bis 11 ein. Ausrufezeichen und Hinweis aktualisieren sich
während der Bedienung. Die drei Themes verwenden ihre Referenzfarben,
einschließlich der dunklen Linien in Silver.

Die dynamische Textur verwendet einen festen Cacheplatz. Bei Änderungen wird
die vorherige GPU-Textur freigegeben; wiederholte Klicks legen keine
unbegrenzte Bildsammlung an. Kein zusätzliches Tafelbild im OBW ist nötig.

## Demo und spätere Anbindung

`Snapshot.state.tactics.values` enthält acht Vorschaueinstellungen.
Revisionierte `tactics`-Aktionen mit `cycle` oder `effort` gehen ausschließlich
an den Demoanbieter. Die HTML-Startwerte sind `[2, 1, 1, 0, 1, 0, 1, 6]`;
Speichern, Laden und Wiederöffnen erhalten Änderungen. Keine Matchfunktion
wird ausgeführt.

Die Formation ist derzeit dieselbe feste Vorschau wie im HTML-POC. Sie ist
noch nicht mit einer nativen Aufstellungsansicht verbunden. Der Sessionadapter
liefert später Formation, erlaubte Werte und Wirkungen. Die Kreideschrift
nutzt verfügbare Systemschriften mit Rückfall auf die mitgelieferte UI-Schrift;
geringe Metrikunterschiede zwischen Betriebssystemen sind noch zu prüfen.

## QA

Zwölf vollständige Trainings-/Taktikläufe decken drei Themes und zwei
Auflösungen ab. Geprüft: alle sieben Wechsel samt Rückkehr zu Startwerten,
tatsächlich aktualisierte Diagramme, beide Reglerenden, Tastaturbedienung,
Wiederöffnen und Wiederherstellung in einem zweiten Prozess. Zusätzliche
Silver-Aufnahmen prüfen Reglerknopf, Fortschritt und Schatten.

Linux/OpenGL wurde tatsächlich gerendert. Windows/macOS haben CI-Builds und
synthetische Tests; ihre visuelle Laufzeitabnahme ist damit nicht erledigt.
Kleine Schrift-/Kantenrasterungsunterschiede bleiben protokolliert.
Siehe [wiederholbare Menü-QA](../../menu-qa.md).
