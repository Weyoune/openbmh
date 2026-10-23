# Fußball-Almanach

Michaels Wunsch: Die Modern-Engine hebt **jedes je gespielte Spiel** auf, damit das Spiel einen „Fußball-Almanach“ mit starken Statistiken bekommt (ewige Torjäger, direkte Vergleiche, Rekorde, Serien, Zuschauer, Schiedsrichter …). Nur Modern; Classic bleibt unverändert.

Code: `src/modern/game/almanac.hpp/.cpp`. Datenformat: [F10](datenformat.md#f10--almanach).

## Was aufgezeichnet wird

Pro Spiel (`AlmanacMatch`):

| Feld | Inhalt |
|---|---|
| Saison | `season` (gespielte Saisons), `start_year` (Kalenderjahr des Saisonbeginns) im Saisonkopf |
| Datum | Tageszähler des Kalenders; `almanac_date(zähler, display_start_year)` liefert Tag/Monat/Jahr wie der Schreibtisch |
| Spieltyp | `type`: 0 Liga, sonst Pokal-ID + 1 (DFB-Pokal, Europapokale, Länderspiele, Freundschaftsspiele, Hallenturnier, Relegation …) |
| Wettbewerb | `league` (Ligaindex, im Saisonkopf mit Welt-ID und Namen) bzw. `stage` (Pokalstufe = Eintrag der Pokaltabelle) |
| Spieltag | `round`: Liga-Spieltag ab 1 bzw. Runde innerhalb der Pokalstufe ab 1 |
| Vereine | Heim/Gast als stabile Welt-IDs (Vereinstabelle der Saison) |
| Menschliche Manager | je Seite der Manager (Index und Name) oder −1 für Computer; Grundlage für spätere Vereinswechsel |
| Ergebnis | Tore nach regulärer Zeit bzw. Verlängerung, Elfmeterschießen getrennt, `decided` (0 regulär, 1 Verlängerung, 2 Elfmeter) |
| Schiedsrichter, Zuschauer, Wetter | Name (Tabelle der Saison), Zuschauerzahl, Temperatur in °C, Regen 0–7 |
| Aufstellungen | je Seite die 11 Startspieler, dann die Bank (nur Ligavereine; Länder- und Amateurteams haben in der Engine keine Kader) |
| Ereignisse | Minute, Seite, Spieler und ggf. zweiter Spieler: Tor (mit Vorlage und Art: Spiel, Elfmeter/Freistoß, Nachschuss, Ecke), Fehlschuss, Gelb, Gelb-Rot, Rot, Verletzung, Wechsel (ein/aus), Elfmeter im Elfmeterschießen |

## Woher die Daten kommen (keine Wirkung auf das Spiel)

- Tore, Fehlschüsse, Karten, Verletzungen: das Ereignisprotokoll der Spielgruppe (`MatchGroup::log`), das die Engine wie das Original für **jedes** Spiel führt.
- Elfmeterschießen: die Show-Ereignisse der Match-Engine (das Protokoll hat keine Elfmeter); Modern sammelt sie jetzt immer.
- Wechsel: Vergleich der Spieler auf dem Platz nach jeder Minute. Damit werden auch Wechsel des menschlichen Managers (Bildschirm, Halbzeit) erfasst.
- Aufstellung, Schiedsrichter, Wetter, Spieltag: beim Anpfiff; Ergebnis und Zuschauer nach dem Spieltag.
- Der Almanach zieht keine Zufallszahlen und ändert nichts, was die Saisonlogik liest. Geprüft: Ein Modern-Lauf über 60 Züge ist mit und ohne Almanach identisch.

## Identitäten

- **Vereine** tauschen beim Auf- und Abstieg ihre Laufzeitnummer; der Almanach speichert deshalb die Welt-ID (`world_club_id`).
- **Spieler**: Die Engine verwendet den Platz eines Spielers für eine neue Person wieder (Karriereende/Abgang in den Pool mit neuen Werten, Jugendspieler). Dort vergibt `new_player_identity` eine neue ID `player:gen-<n>`. Der Zähler wird mitgespeichert. Wie im Original behält die neue Person den Namen des Platzes; die ID trennt die Karrieren.
- Spieler ohne Welt-ID (Classic-Daten) heißen `player:legacy-<index>`, Vereine `team:<index>`.

## Speicherung

- Jede Saison hat ihre eigenen Tabellen (Ligen, Vereine, Spieler, Manager, Schiedsrichter) und ist damit für sich lesbar.
- Abgeschlossene Saisons liegen im Spielstand als `history/season-<Jahr>.bin` und werden nie wieder geschrieben; die laufende als `history/current.bin`. Beide stehen in `state/integrity.json`.
- Binärformat Version 1 (Little Endian): `OBAL`, u16 Version, Saisonkopf, Tabellen, Spiele. Text: u16 Länge + UTF-8. Einzelheiten in `encode_almanac_season`.
- Größe: etwa 0,5 MB pro Saison im (komprimierten) Spielstand, mit allen Ereignissen und Aufstellungen. Ein Spielstand nach einer Saison ist ~700 KB statt ~210 KB.

## Für die UI (Codex)

- `ModernState::almanac`: `finished` (Dateiname + Bytes je Saison, mit `decode_almanac_season` lesen) und `current` (laufende Saison).
- Auswertungen (Torjäger aller Zeiten, Bilanzen, Rekorde) baut die Engine als Projektionen, sobald die Almanach-Oberfläche feststeht. Bis dahin kann die UI direkt auf den Saisons rechnen. `modern_season_sim` zeigt am Laufende ein Beispiel (`print_almanac`: Prüfung Tore gegen Ergebnis, Top-Torschützen über alle Saisons).
- Namen sind UTF-8; Wettbewerbsnamen für Pokale kommen wie im Ergebnisbildschirm aus den Texten (`type - 1` = Pokal-ID).

## Offen

- Kader der Länder- und Amateurteams, damit auch deren Aufstellungen aufgezeichnet werden.
- Projektionen für die Almanach-Oberfläche.
- Bessere Kompression der abgeschlossenen Saisons (heute schnellste ZIP-Stufe).
