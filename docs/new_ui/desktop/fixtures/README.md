# Paarungszeilen · gemeinsame Mittelachse

Stand: 24. Oktober 2026. Designänderung nach Michaels Vorgabe:

**Heimname → Heimwappen → Ergebnis → Auswärtswappen → Auswärtsname**

Das Ergebnis beziehungsweise der neutrale Trenner steht in der Mitte der
Begegnungszeile. Die Heimseite richtet sich nach rechts, die Auswärtsseite
nach links aus. Die Wappen liegen innen am Ergebnis; Namen wachsen nach
außen. Beide Mannschaftsbereiche sind gleich breit. Lange Namen erhalten
Auslassungspunkte, ohne Wappen oder Ergebnis zusammenzudrücken.

## Übertragene Ansichten

- Native und HTML: Ligaspiele, übrige Spiele einschließlich der langen
  DFB-Pokal-Liste; laufende Auslosung, zuletzt gezogene Paarungen und Übersicht.
- HTML: Ergebnisliste nach dem Spiel, Auswahl paralleler Übertragungen,
  TOTO-Spielprogramm und die WM-/EM-Finalrundenliste.
- Die Länder-Finalrunden verwenden ihre vorhandenen Ländernamen. Es werden
  dort keine Vereinswappen als Nationalmannschaftswappen ausgegeben.

Die großen Anzeigetafeln und Tabellen mit Ranglisten haben eigene Formen.
Datums-/Terminbeschreibungen im Kalender und bei Verlegen sind weiterhin
Beschreibungstext zur jeweiligen Auswahl.

## Geometrie

Die breiten Ligaberichte verwenden eine **160dp** breite Mittelspalte statt
400dp. Seitlich bleiben dort 8dp Innenabstand, damit auch die Überschrift
„Ergebnis“ vollständig hineinpasst. Heim und Auswärts liegen spiegelgleich
an dieser Mitte. Der native Bericht hält seine Mittelachse auch bei sichtbarem
Scrollbalken. Die Seitenbreiten werden aus den tatsächlich gerundeten
Rahmenpixeln berechnet; außen schützen zusätzliche 18dp den Platz für lange Namen.

Die Paarungsnummer der Auslosung erhält rechts ein gleich großes leeres
Gegenstück. Dadurch verschiebt sie die Ergebnisachse weder beim ersten Los
noch beim Wechsel zwischen ein- und zweistelligen Nummern. Die Übersicht
zentriert jede Begegnung innerhalb ihrer eigenen Spalte. Native Breiten:
2352dp für die aktuelle Paarung, 1289dp je Übersichtsspalte mit 34dp Abstand.

Im HTML-Spielprogramm werden zusätzliche Informationen wie Spielnummer,
Liga oder Auswahlkästchen ebenfalls symmetrisch berücksichtigt. TOTO zeigt
bei noch nicht ausgetragenen Spielen einen neutralen „–“-Trenner.

## Daten und Einbindung

Die Änderung betrifft die Präsentation der vorhandenen Paarungen. Teilnehmer,
Spielstände, Losfolge, Termine, Tippdaten und Enginezustand werden dabei
nicht verändert. Bestehende Vereinsknöpfe und Berichtsaufrufe bleiben bedienbar.
Die native UI bleibt eine Vorschau bis Anstoß; Match- und Post-Match-Seiten
in diesem Paket betreffen den bereits vorhandenen HTML-Prototyp.

Quellen: private HTML-Produktbasis `3070f242dfcd5130e5caa4eae20c53db14eebb4d`,
insbesondere `table-window.css`, `club-data.css`, `season-opening.css`,
`post-match.css`, `match-broadcast.css` und `toto-window.css`.
Native Umsetzung: `reports_screens.inc`, `screens.inc`, `reports.rcss`.
Der neue HTML-Stand ist die Gestaltungsreferenz für weitere native Ports.

## Prüfung

Die gezielten Katalog-/Berichts- und RmlUI-Controltests bestehen.
Der bestandene visuelle Vergleich prüft Mittelachsen und Wappenabstände, lange Namen,
32 Pokalpaarungen mit Scrollen, Auslosungsphasen sowie die vorhandenen
Klickpfade in drei Themes. Bilder, Messwerte und ladbare UI-Demostände bleiben
unter `bmh_4k_ui/docs/qa/fixture-alignment-2026-10-24/` im privaten
Mockup-Repository. Auch die echte laufende Ziehung wurde auf Pause,
Fortsetzen, Überspringen und den Wechsel zum Desktop geprüft. Die
Berichtsmitte stimmt einschließlich 720p und sichtbarem Scrollbalken mit
der Fensterachse überein. Bei der Auslosung ist die Zeile lokal exakt
symmetrisch; bei 720p bleibt die bestehende allgemeine Inhaltsrundung von
ungefähr einem Drittel Pixel zur Fensterachse sichtbar. Diese Prüfung
betrifft Linux/OpenGL und Browser, keine native Plattformabnahme aller Menüs.
