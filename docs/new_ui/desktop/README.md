# Desktop · Modern UI

Stand: 23. Oktober 2026. Diese Beschreibung bezieht sich auf den bestehenden
HTML-POC. Die native RmlUI-Umsetzung und die Anbindung an die Modern-Engine
werden für 0.3 separat entwickelt.

## Informationsleiste

Ganz links stehen **Vereinswappen, Vereinsname und Managername** als gemeinsame
Identität. Rechts davon folgen Spieltag bzw. Saisonvorbereitung, Saison und
Guthaben. Die OpenBMH-Wortmarke entfällt auf dem Desktop; das Hauptmenü behält
seine eigene Gestaltung. Ein Klick auf die Vereinsidentität öffnet weiterhin
die Desktopoptionen. Rechtsklick auf die freie Fläche bleibt ebenfalls möglich.

Die Leiste ist im logischen Referenzraum 148 Pixel hoch. Der aktuelle POC zeigt
Verein, Manager, Termin und Guthaben aus seinem fiktiven Spielzustand.

## Gestaltung in den Einstellungen

**Einstellungen → Anzeige → Gestaltung** enthält drei gleichwertige
Vorschaukarten. Jede zeigt die Kombination aus Desktop und Untermenüfenster.
Ein separater Theme-Button in der Informationsleiste entfällt.

| Theme | Darstellung |
|---|---|
| Midnight Glass | Dunkles Glas, ruhige Blautöne, Mint |
| Silver Glass | Helle Glasflächen, dunkle Beschriftung, Silberblau |
| Matchday | Graphit, klare Flächen, dezente warme und rote Akzente |

Die Auswahl ändert zunächst nur den Einstellungsentwurf und bleibt beim
Wechsel der Einstellungskategorie erhalten. Erst **Anwenden** übernimmt sie,
wechselt Desktop und Untermenüs gemeinsam und schließt die Einstellungen.
Abbrechen oder Escape verwirft die Auswahl. Der angewendete Look bleibt nach
dem Neuladen erhalten. Positionen, Widgetgrößen, Fixierungen und Spielwerte
werden durch einen Theme-Wechsel nicht verändert.

Die Vorschaukarten gibt es auch in den Einstellungen des Hauptmenüs; dort
bleiben die Kategorien auf Anzeige und Sound begrenzt. Beim Wechsel zum
Desktop wird die dort gespeicherte Gestaltung verwendet.

## Fenstermaße und Skalierung

Der gemeinsame 4:3-Rahmen für Untermenüs nutzt im logischen Desktop von
3840 × 2160 Pixeln folgende Maße:

| Eigenschaft | Bisher | Aktuell |
|---|---:|---:|
| Breite | 2560 px | 2720 px |
| Höhe | 1920 px | 2040 px |
| Abstand oben / unten | je 120 px | je 60 px |
| Abstand links / rechts | je 640 px | je 560 px |
| Seitenverhältnis | 4:3 | 4:3 |

Die zusätzlichen Flächen stehen den Menüs zur Verfügung; Fenster und Inhalte
skalieren mit dem Desktop. Auf breiteren Bildschirmen bleibt das Verhältnis
erhalten. Die Fenster berühren den oberen oder unteren Bildschirmrand nicht.

Der bisherige HD-Desktop unter `desktop/index.html` bleibt erhalten.
Der neue Desktop ist unter `desktop/modern.html` erreichbar. Die hier
beschriebenen Änderungen betreffen den HTML-POC.

## Quellen und Prüfung

Privates Repository `bmh_4k_ui`, Commit `19969d430e38eadae7ff7a9d1fb381ab02c395de`:

- `desktop/modern.html`, `desktop/modern-desktop.css`,
  `desktop/modern-desktop.js`: Informationsleiste und Desktop.
- `desktop/settings-menu.js`, `desktop/settings-window.css`: Entwurf,
  Theme-Auswahl und Vorschaukarten.
- `desktop/table-window.css`: gemeinsamer 4:3-Fensterrahmen.
- `docs/qa/desktop-chrome-2026-10-23/README.md`: Vorher-/Nachher-Bilder,
  passende POC-Zustände und 124 erfolgreiche Browserprüfungen. Geprüft wurden
  unter anderem 33 Menürouten in drei Themes sowie die Darstellung bei
  1280 × 720, 1920 × 1080, 2560 × 1440 und 3440 × 1440.

## Menükatalog

[Trainingslager: alle 18 Ziele mit Qualitätswerten, Preisen und Bedienung](trainingcamp/README.md).

[Transfers: Ablöse-Kasten und Abstand der Positionsfilter](transfers/README.md).
