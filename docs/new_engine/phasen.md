# Phasen · Modern-Engine 0.3

Jede Phase hält das Spiel lauffähig. **Classic-Prüfung nach jeder Phase:**
`ctest -C Release -E repo_hygiene` und die Classic-Regression identisch zur
Referenz aus M0.

## M0 · Grundlage

- Classic-Referenz festhalten: Regressionsausgaben, Langläufe, Tag auf `0.3`.
- Modern-Datensatz lokal bereitstellen (privat, nicht in dieses Repository).

**Abnahme:** Referenzen liegen vor; spätere Phasen vergleichen dagegen.

## M1 · `WorldLayout` und Abzweig

- `WorldLayout` in `src/game/state.hpp`: Ligagrößen, ID-Bereiche (Liga, Amateure, Pokal-Ausländer, Nationalteams, `kNoTeam`), Kaderplätze pro Team, Pool-Offsets, Gesamtzahl der Plätze, Spieler- und Vereinsanzahl. `classic_layout()` liefert die heutigen Werte; `squad_range` liest das Layout.
- Gemeinsame Teile lesen Teambereiche aus dem Layout (`match_engine`, `rating`, `match`).
- Saisonlogik nach `src/modern/game/` kopieren (Bibliothek `bmh_season_modern`, Namespace `bmh::modern`); dort alle festen Größen aus dem [Befund](befund.md) durch Layoutwerte ersetzen.
- Vereinsbezogene Speicherblöcke werden in Modern zu Vektoren, die nach der Vereinsanzahl bemessen sind; Classic behält seine Blöcke.

**Abnahme:** Classic identisch; Modern-Kern baut und läuft mit Classic-Layout identisch zu Classic (Gegenprobe, dass der Abzweig nichts verändert hat).

## M2 · Ligen, Spielpläne, Kalender

- Spielplangenerator je Liga aus den Daten: `round-robin` (Kreismethode, jede Größe, Heim/Auswärts ausgeglichen) für die internationalen Ligen, `legacy:<n>` für die deutschen Ligen aus den Classic-Tabellen des Datensatzes.
- Kalender: Der Tagesplan der Saison wird beim Start aus dem Classic-Plan erzeugt und für die internationalen Ligen auf 38 Runden ergänzt. Ligen mit weniger Runden setzen danach aus. Tabellen, Prognose und Saisonende rechnen je Liga mit `2 × (Vereine − 1)` Spieltagen.
- Europapokal-Teilnehmer aus den internationalen Ligen werden wie bisher bestimmt, mit den neuen Teambereichen.

**Abnahme:** Synthetische Welt mit Ligen von 20, 20, 20 und 18 Vereinen spielt eine Saison vollständig durch; jede Paarung genau einmal pro Hin- und Rückrunde.

## M3 · Modern-Weltlader

- Ein Paket ist Modern, wenn `requires` `runtime:modern-v1` enthält; der Mod-Ordner darf zusätzlich `mod.json` mit `type: modern` tragen (Ansatz aus dem Modern-POC).
- Ablauf (`src/modern/content/world_loader.*`): Archiv lesen → Classic-Startzustand aus den Classic-Datenbereichen des Pakets aufbauen → Welttabellen darüberlegen (Vereine, Ligen, Kader und Reihenfolge, Spielerwerte `bmh-classic@1`, Verträge) → `WorldLayout` aus den Daten ableiten.
- Namen: `GameState` erhält UTF-8-Felder für Vor- und Nachname sowie Vereinsname und Kurzname. Texte der Engine (Post, Meldungen, Ranglisten, Konsole) verwenden in Modern einen gemeinsamen Namenszugriff.
- Asset-Schlüssel (Wappen, Porträts, Trophäen) werden unverändert an die UI weitergegeben.
- Gegenseitiger Ausschluss: Modern lehnt Classic-Pakete ab und umgekehrt.

**Abnahme:** Der Modern-Datensatz startet eine Karriere mit einem deutschen Verein; Kader, Namen und Ligen stimmen mit den Welttabellen überein.

**Umsetzung (Stand 24. Oktober 2026):**

- `src/modern/content/world.*`: typisierte Sicht auf die Welttabellen (Ligen, Vereine, Spieler, Nationen, Ligatermine).
- `src/modern/content/world_package.*`: liest aus dem Paket nur, was die Engine braucht (Manifest, Welttabellen, Classic-Nutzdaten, Classic-Grafiken); HD-Wappen und -Porträts bleiben im Archiv. Der Classic-Startzustand entsteht aus den unveränderten `x-classic`/`x-legacy`-Daten.
- `src/modern/content/world_expand.*`: bringt den Classic-Start auf die Welt. Vereins-IDs folgen den Ligamitgliedern (die vergrößerten internationalen Ligen verschieben alle IDs dahinter um 30); Tabellen pro Verein werden umgerechnet, neue Vereine übernehmen die Startwerte eines Originalvereins derselben Liga; neue Spieler erhalten ihre Werte aus den `bmh-classic@1`-Attributen; jeder Ligaverein hat 24 Kaderplätze; die Pools hinter den Kadern (Transferliste, vereinslos, Ausland, Nationalteam, 54 Reservespieler) behalten Inhalt und Reihenfolge.
- Namen und OBW-IDs für die UI liegen in `ModernState` (`world_club_name`, `player_first`, `player_last`, …); die Engine-Texte nutzen weiter die Einzelbyte-Namenspools, in die neue Namen angehängt werden.
- Probelauf: `modern_season_sim <Welt.obw> [Spielzüge] [Verein] [Startwert]`; `MODERN_DEBUG=1` zeigt die Ladeschritte, `MODERN_WATCH=<Sekunden>` meldet regelmäßig den Engine-Schritt (Endlosschleifen finden).
- Ergebnis mit dem Modern-Datensatz 1.3: zwei Saisons, rund 5.300 Spiele; alle Ligen spielen ihre Runden vollständig (deutsche Ligen 34, LaLiga/Premier League/Serie A 38, Ligue 1 34).
- Dabei gefunden und behoben: der Schiedsrichterpool (49) reicht nicht für Tage mit deutschen und vergrößerten internationalen Ligen (Modern-Abweichung), die Fairplay-Tabelle pro Ligaverein ist mitgewachsen, Kaderzuordnung über das Layout statt fester 24/16-Arithmetik.
- Offen für M6: Modern-Spielstände; die Session nutzt übergangsweise die Classic-UI-Projektionen, die dafür auf das Layout umgestellt sind.

## M4 · Trainingslager

Siehe [Trainingslager](trainingslager.md): Katalog aus den Daten, drei Regionen, Qualitätswerte 0–6, unveränderte Preisformel und Aufenthaltsgrenzen, begrenzte 6-Sterne-Wirkung.

**Abnahme:** Tests für Preis, Grenzen und Wirkung mit festen Seeds; Simulation: drei Aufenthalte in 6-Sterne-Lagern bringen einem Spitzenteam im Schnitt höchstens etwa einen Tabellenplatz.

## M5 · Datenrevision 1.4 (privat)

- Neue, nummerierte Migration des Modern-Datensatzes nach dem bestehenden Migrationsvertrag (Eingangs-Hash, Bericht, unabhängige Prüfung, Editor-Roundtrip): `runtime:modern-v1`, spielbar, Trainingslager-Katalog, Kalenderangaben für 38 Spieltage, Finanzangaben auch für die 30 neuen Vereine, ein größerer Schiedsrichter-Pool (mindestens 100, [Datenformat F9](datenformat.md#f9--schiedsrichter)) samt Engine-Anbindung.
- Werkzeug: C++ (`openbmh_world_migrate`) oder das bestehende Migrationswerkzeug der Datenpflege; vor Beginn abstimmen.

**Abnahme:** Prüfbericht ohne Fehler; Editor öffnet und speichert verlustfrei.

## M6 · Spielstände und UI-Projektionen

- Spielstände `.obs` mit `save_profile: modern-career-v1`: gleiche Hülle wie Classic, aber Modern-Zustand mit Layout und Namen; Dimensionen aus der Welt.
- Projektionen und Aktionen der `ModernSession` gemäß [UI-Schnittstelle](ui-schnittstelle.md).

**Abnahme:** Speichern und Laden mitten in der Saison setzt identisch fort; UI-Projektionen haben Tests mit synthetischen Welten.

## M7 · Balance

- Konsolen-Langläufe mit dem Modern-Datensatz (mehrere Startkonstellationen, je zehn Saisons): Tore pro Spiel, Punkteverteilung je Liga, Ermüdung und Verletzungen bei 38 statt 22 Spieltagen, Marktwerte und Gehälter, Finanzen des menschlichen Vereins, Wirkung der Trainingslager.
- Korrekturen zuerst in den Daten (etwa Werte der neu ergänzten Spieler, Preise), dann in Regeln; jede Regeländerung in [DIFFERENCES](../DIFFERENCES.md).

**Abnahme:** Bericht mit Kennzahlen und begründeten Anpassungen.
