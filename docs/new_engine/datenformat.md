# Datenformat · Änderungen für die Modern-Engine

Stand: 23. Oktober 2026. Hier steht jede Änderung am OBW-Format, die die
Modern-Engine einführt oder voraussetzt. **Der Editor muss jede Zeile mit
„Editor“ nachziehen**: anzeigen, bearbeiten und prüfen. Grundlage ist das
Format [OBW](../formats/OBW.md) und das Editor-Profil
[editor-v1](../formats/obw/editor-v1.md).

Status: *geplant* (noch nicht in der Engine), *umgesetzt* (Engine liest/prüft es).

## Übersicht

| Nr. | Bereich | Änderung | Phase | Status |
|---|---|---|---|---|
| F1 | Manifest | Laufzeit `runtime:modern-v1`, spielbare Modern-Welt | M3 | Engine: lädt `runtime:modern-v1` und vorerst auch `editor:world-v1`; lehnt Classic-Pakete ab |
| F2 | Ligen | Größe je Liga frei (12–20), Generator je Liga | M2 | Engine: Größe je Liga umgesetzt; Generator vorerst Tabelle passender Größe |
| F3 | Kalender | Ligatermine als Datumsliste | M2 | Engine-Funktion umgesetzt; Anbindung an den Weltlader mit M3 |
| F4 | Trainingslager | Katalog mit Karte und X/Y-Lage, Ort, Bild, Qualität 0–6 | M4 | Engine: liest den erweiterten Katalog; ohne Katalog gilt die eingebaute Vorgabe (18 Lager) |
| F5 | Namen | Vor- und Nachname Pflicht, UTF-8 | M3 | Engine: gelesen, in `ModernState` |
| F6 | Kader | bis 24 Plätze je Verein, Mindestgröße | M3 | Engine: 24 Plätze je Ligaverein; mehr als 24 Spieler werden abgelehnt |
| F7 | Finanzen | Finanzangaben für Vereine ohne Classic-Startregel | M5 | geplant |
| F8 | Spielstand | `save_profile: modern-career-v1` | M6 | Engine erledigt: Spielstand nennt seine Welt (ID, Revision), enthält nur die Karriere |
| F9 | Schiedsrichter | eigener Schiedsrichter-Pool der Welt, mehr als 49 | M5 | Engine erledigt (Pool aus `staff`, neu: `perception`); Daten folgen mit 1.4 |
| F10 | Almanach | alle gespielten Spiele im Spielstand (`history/…`) | M8 | Engine erledigt (Aufzeichnung, Spielstand); UI offen |

## F1 · Manifest

- Eine Modern-Welt trägt `"requires": ["runtime:modern-v1"]`. Die Modern-Engine lädt nur solche Welten; Classic lehnt sie ab.
- Das Editor-Profil bleibt `editor-world-v1`. Neu: Eine Welt mit `runtime:modern-v1` darf `x-editor.playable: true` haben, wenn sie die Modern-Prüfung (unten) besteht.
- Die Datenrevision steht weiter in der Erweiterung des Datensatzes (z. B. `data_revision`).
- **Editor:** Schalter „Als Modern-Welt spielbar machen“ setzt `requires` und `playable` erst nach erfolgreicher Modern-Prüfung; Entfernen setzt beides zurück. Die bisherige Bedingung `playable == false` gilt nur noch für Welten ohne `runtime:modern-v1`.

## F2 · Ligen

- `competitions[].stages[].teams` darf je Liga zwischen 12 und 20 liegen; die Mitgliederliste (`members`), `teams` und die Teams der Slots müssen übereinstimmen.
- `generator`: `round-robin` für jede Größe; `legacy:<n>` nur, wenn `rule:legacy-fixtures` eine Tabelle für genau `n` Teams hat.
- Stand 0.3: Die Engine spielt jede Liga mit der Spielplantabelle ihrer Größe (`rule:legacy-fixtures`, 12–20 Teams). `round-robin` wird gelesen, aber noch wie die gleich große Tabelle behandelt; nur die Reihenfolge der Paarungen unterscheidet sich. Ein eigener Round-Robin-Generator folgt bei Bedarf.
- Spieltage einer Liga: `2 × (teams − 1)` bei `legs: 2`.
- Die Classic-Zusatzfelder `x-legacy.league_index`, `calendar_bit`, `relegation_places`, `europe_places` bleiben und werden in 0.3 weiter gelesen.
- **Editor:** Größe zwischen 12 und 20 zulassen; Generator `legacy:<n>` nur anbieten, wenn die Tabelle existiert; Warnung, wenn Mitglieder, `teams` und Slots nicht übereinstimmen.

## F3 · Kalender

- Ligen verweisen über `matchdays` auf einen Kalender. Neben den Classic-Verweisen (`calendar:legacy-league-<n>`) ist ein Kalender mit expliziter Datumsliste `dates` erlaubt, wie er für die vier vergrößerten internationalen Ligen bereits vorliegt.
- Die Anzahl der Daten muss der Zahl der Spieltage entsprechen; Daten liegen aufsteigend innerhalb der Saison und nicht auf demselben Tag zweimal.
- Die Engine bildet die Daten auf Spieltage des Classic-Saisonablaufs ab; ein Datum, das dort kein möglicher Ligaspieltag ist, verschiebt sich auf den nächsten möglichen Tag (Hinweis im Ladeprotokoll).
- **Editor:** Datumsliste anzeigen und bearbeiten; Anzahl gegen die Spieltage prüfen.

## F4 · Trainingslager

Die bestehende Regel `rule:training-camps` (Typ `training_camps`) bleibt und wird erweitert:

```json
{
  "id": "rule:training-camps",
  "type": "training_camps",
  "maps": [
    {"id": "map:de", "name": {"default": "Deutschland"}},
    {"id": "map:eu", "name": {"default": "Europa"}},
    {"id": "map:me", "name": {"default": "Naher Osten"}}
  ],
  "resorts": [
    {
      "id": "resort:4",
      "base_price": 485000,
      "quality": [4, 4, 4, 4],
      "map": "map:eu",
      "x": null,
      "y": null,
      "name": {"default": "Sportzentrum Groningen"},
      "place": {"default": "Groningen"},
      "country": "nation:nl"
    }
  ]
}
```

- `id`, `base_price`, `quality` wie bisher; `quality` in der Reihenfolge Platz, Fitness, Verpflegung, Unterkunft, je **0–6** (Classic: 1–5).
- **Karte und Lage:** Jedes Lager trägt immer `map` (Verweis auf `maps`) sowie `x` und `y` als ganzzahlige Koordinaten auf dieser Karte. Das Koordinatensystem einer Karte legt die Oberfläche fest; solange es nicht feststeht, sind `x` und `y` `null`. Sobald die GUI die Kartenmaße liefert, bekommt jede Karte zusätzlich `width` und `height` (Ursprung oben links), und `x`/`y` müssen innerhalb liegen.
- Die Karten sind eine eigene Liste, damit später weitere hinzukommen können, etwa wenn ausländische Vereine spielbar werden und Lager aus deren Sicht dargestellt werden. Ein Lager gehört genau zu einer Karte.
- Weitere Pflichtfelder für Modern: `name`, `place`, `country`. Geografische Koordinaten werden nicht gespeichert.
- Standardkarten und Resortbilder sind GUI-Ressourcen und werden anhand von Karten-/Lager-ID zugeordnet; sie sind keine Pflichtinhalte der Welt. `image` ist optional und nur für ein ausdrücklich in der Welt geliefertes Bild vorgesehen. Wenn gesetzt, muss der Asset-Schlüssel im normalen OBW-Asset-Index auflösbar sein. Siehe [GUI-/Welt-Trennung](../new_ui/assets.md).
- Die IDs `resort:0` bis `resort:17` entsprechen den IDs 0–17 der Oberfläche und werden nicht umsortiert.
- Fehlt die Regel in einer Modern-Welt, gilt die in die Engine eingebaute Vorgabe (`src/modern/game/training_camps.cpp`) mit den 18 fiktiven Lagern.
- **Editor:** Kartenliste und Lager bearbeiten: Karte (Pflicht), `x`/`y` (leer erlaubt, solange die Karte keine Maße hat), vier Qualitätswerte (0–6), Preis, Ort, Land und optionales datensatzspezifisches Bild. Prüfung auf eindeutige IDs, gültige Karte und Nation, Qualitäten im Bereich, auflösbare explizite Bildreferenzen und Koordinaten innerhalb der Kartenmaße, sobald diese gesetzt sind. Ein fehlendes `image` ist kein Fehler; feste GUI-Motive müssen nicht in den Datensatz kopiert werden.

## F5 · Namen

- Spieler haben in Modern genau `name.first` und `name.last` (UTF-8, NFC); ein zusammengesetzter Anzeigename wird nicht gespeichert.
- Vereine: `name` und `short` als lokalisierbare Texte mit Groß- und Kleinschreibung.
- Die Classic-Namensfelder im Archiv bleiben unverändert als Quelle erhalten; die Modern-Engine liest sie nicht.
- **Editor:** besteht bereits (Datenrevision 1.2); für Modern-Welten Pflichtprüfung „Vorname und Nachname vorhanden“.

## F6 · Kader

- Jeder Ligaverein hat höchstens 24 Kaderplätze; die Reihenfolge (`squad_order`) bleibt maßgeblich.
- Mindestens 14 Spieler je Ligaverein, davon mindestens ein Torwart.
- **Editor:** Warnung bei mehr als 24 oder weniger als 14 Spielern beziehungsweise ohne Torwart.

## F7 · Finanzen

- In 0.3 übernimmt der Mensch nur deutsche Vereine; deren Startfinanzen entstehen weiter aus der Classic-Startregel.
- Vereine ohne Finanzzeile (etwa die neuen internationalen Vereine) bleiben gültig. Eine Finanzzeile für sie wird erst nötig, wenn sie wählbar werden; das Format dafür wird dann hier ergänzt.
- **Editor:** vorerst keine Änderung.

## F8 · Spielstand

- Modern-Spielstände sind `.obs` mit `save_profile: modern-career-v1`; Aufbau wie Classic-Spielstände (`state/…`, Integritätsdatei), aber mit dem Modern-Zustand und den Dimensionen der Welt.
- Anders als ein Classic-Spielstand enthält ein Modern-Spielstand **nicht** die Welt (sie ist samt HD-Bildern mehrere hundert MB groß). Das Manifest nennt sie unter `world` (`id`, `data_revision`, `title`); geladen wird nur mit genau dieser Welt.
- Einträge: `state/career.json` (Spielzustand), `state/modern.json` (Schema `modern-state@1.1`: Vereins- und Spieler-IDs, UTF-8-Namen, Nationen je Laufzeit-Index, Liga-IDs, Zähler neuer Spieler-IDs), `history/…` (Almanach, F10), `state/rng.json`, `state/driver.json`, `state/session.json`, `state/information.json`, `state/integrity.json`. Rund 210 KB.
- Ändert sich die Datenrevision, passt ein alter Spielstand nicht mehr; eine Spielstand-Migration ist noch nicht vorgesehen.
- **Editor:** öffnet keine Spielstände; keine Änderung.

## F9 · Schiedsrichter

- Heute kommen die 49 Schiedsrichter (plus Platzhalter 0) aus der Original-Datenbank: Namen im Namenspool, je ein Eigenschaftsbyte (`RefereeProperties`). Pro Spieltag erhält jedes Spiel einen noch freien Schiedsrichter. Mit den vergrößerten internationalen Ligen gibt es Tage mit bis zu 84 Ligaspielen; die Engine lässt deshalb vorübergehend Doppeleinsätze zu.
- Die Welt führt Schiedsrichter schon heute als `staff`-Zeilen mit `role: referee` (Revision 1.3: die 50 Originale mit `x-legacy.referee_index` 0–49 und `x-legacy.property`, dem Original-Eigenschaftsbyte).
- Das Eigenschaftsbyte hat zwei Werte, die die Spiele beeinflussen:
  - `perception` (Bits 0–1, 0–3): Abseits-Entscheidungen und übersehene Fouls;
  - `strictness` (Bits 3–4, 0–3): Karten.
- **Neue Schiedsrichter (ab Revision 1.4):** `staff`-Zeilen ohne `x-legacy`, Pflichtfelder sind `id`, `name` und `role: referee`. Dazu `attributes.strictness` und `attributes.perception` (je 0–3, fehlend = 1); eine Nation ist optional. Sie folgen in Tabellenreihenfolge auf die Originale (Index 50, 51, …), höchstens 255 insgesamt. Die Originale behalten ihr Eigenschaftsbyte aus `x-legacy.property`.
- Empfehlung: mindestens 100 Schiedsrichter, damit auch Tage mit Pokalspielen ohne Doppeleinsatz auskommen.
- **Engine: erledigt.**
  - Die Poolgröße kommt aus der Welt.
  - Namen gehen in den Namenspool (Einzelbyte-Kodierung wie bei den Vereinen), das Eigenschaftsbyte ist `strictness << 3 | perception`.
  - Ein Doppeleinsatz ist nur noch der Rückfall, wenn der Pool erschöpft ist.
  - Ein Spielstand passt nur zu einer Welt mit gleich vielen Schiedsrichtern.
  - Test ohne neuen Datensatz: `MODERN_EXTRA_REFEREES=N modern_season_sim …`.
- **Editor:** Schiedsrichterliste anzeigen und bearbeiten (Name, Nation, Strenge, Wahrnehmung), neue Schiedsrichter anlegen; Prüfung auf eindeutige IDs, Werte 0–3 und höchstens 255 Schiedsrichter.

## F10 · Almanach

- Ein Modern-Spielstand enthält alle gespielten Spiele der Karriere: `history/season-<Jahr>.bin` je abgeschlossener Saison (unveränderlich) und `history/current.bin` für die laufende. Beschreibung: [Almanach](almanach.md).
- `state/modern.json` (Schema jetzt `modern-state@1.1`) bekommt dazu `league_ids`, `league_names` und `generated_players` (Zähler der neu vergebenen Spieler-IDs `player:gen-<n>`).
- Die Integritätsprüfung deckt `history/` mit ab.
- **Editor:** keine Änderung (öffnet keine Spielstände). Eine Welt muss nichts liefern; sinnvoll sind stabile Vereins- und Spieler-IDs, die sich zwischen Datenrevisionen nicht ändern.

## Modern-Prüfung (Engine und Editor)

Eine Welt ist als Modern-Welt spielbar, wenn:

1. `requires` `runtime:modern-v1` enthält und alle Verweise auflösbar sind,
2. höchstens 32 Ligen bestehen, jede mit 12–20 Vereinen und passendem Generator und Kalender (F2, F3),
3. jeder Ligaverein die Kaderregeln erfüllt (F6),
4. jeder Spieler Vor- und Nachname hat (F5),
5. Trainingslager vollständig sind, sofern die Regel vorhanden ist (F4).

Die Engine meldet Verstöße mit Datei, Pfad und Regel; der Editor zeigt dieselben Meldungen.
