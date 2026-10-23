# Trainingslager · Modern-Engine

Stand: 23. Oktober 2026, Planung für Phase M4. Angebot und Bedienung der
Oberfläche stehen in [new_ui/desktop/trainingcamp](../new_ui/desktop/trainingcamp/README.md).

## Classic heute (Ausgangspunkt)

Quelle: `src/game/desk_actions.cpp` (`training_camp_price`, `book_training_camp`, `training_camp_effect`).

- Acht Lager mit Basispreis und vier Qualitätswerten von 1 bis 5.
- **Preis:** Basispreis + 5000 × Anzahl der Kaderspieler. Eine Buchung ist ein Aufenthalt; es gibt keine Dauer oder Wochenpreise.
- **Grenzen:** höchstens ein Aufenthalt pro Woche und drei pro Saison; die Buchung scheitert, wenn Guthaben plus Kreditrahmen nicht reichen.
- **Schwerpunkte:** drei Regler von 0 bis 15; der vierte Wert (Erholung) folgt aus `15 − (a + b + c) / 3`.
- **Wirkung** mit `scaled(x, note) = x × note × 20 / 100`:
  - Mannschaftsmoral: `+ scaled(12, Verpflegung)`, höchstens 47.
  - Je Spieler und Wert (Kondition, Technik, Form, Eingespieltheit, Frische) eine Zufallsstufe aus der Tabelle `{−3 … +5}`; der Zufallsbereich reicht bis Index `scaled(Schwerpunkt, Note) + 8`. Ein höherer Schwerpunkt mal Note verschiebt die Verteilung zu größeren Schritten.
  - Platzqualität wirkt auf Technik und Form, Fitness auf Kondition, Verpflegung auf Moral, Unterkunft auf Eingespieltheit und Frische.
  - Trainingsobergrenze je Wert: Zufall 83–87; die beiden teuersten Classic-Lager (Index ab 6) heben sie um 1.
  - Ältere Spieler trainieren seltener (Chance sinkt ab 14 Jahren Rohalter in Zehnerschritten).

## Modern: Katalog aus den Daten

Der Katalog steht im Modern-Datensatz unter `rules.training_camps`. Fehlt er,
gilt die öffentliche Vorgabe `assets/modern/rules/training_camps.json` mit den
18 fiktiven Lagern der Oberfläche.

Der Katalog erweitert die bestehende Regel `rule:training-camps` (Liste
`resorts`); das genaue Format und die Aufgaben für den Editor stehen in
[Datenformat F4](datenformat.md#f4--trainingslager). Ein Eintrag:

```json
{
  "id": "resort:4",
  "base_price": 485000,
  "quality": [4, 4, 4, 4],
  "map": "map:eu",
  "x": null,
  "y": null,
  "name": {"default": "Sportzentrum Groningen"},
  "place": {"default": "Groningen"},
  "country": "nation:nl",
  "image": "asset:camp/4/image"
}
```

- `quality`: Platz, Fitness, Verpflegung, Unterkunft, je 0–6.
- `map`: immer gesetzt, `map:de` (Deutschland), `map:eu` (Europa) oder `map:me` (Naher Osten); weitere Karten können später hinzukommen.
- `x`, `y`: Lage auf der Karte in deren Koordinatensystem; bis die Oberfläche die Kartenmaße festlegt `null`.
- Die IDs `resort:0` bis `resort:17` entsprechen den IDs 0–17 der Oberfläche und werden nicht neu sortiert.
- Preis, Grenzen und Schwerpunkte bleiben wie in Classic.

## Die 6-Sterne-Stufe

Ziel: spürbar besser als 5 Sterne, aber nicht übermächtig. Die Lager des Nahen
Ostens kosten 2,8–3,6 Mio. € gegenüber höchstens 0,92 Mio. € in Europa.

1. **Normale Skalierung, gedeckelt:** Die Note 6 geht wie jede andere in `scaled()` ein. Der Zufallsindex wird auf das Tabellenende begrenzt, sodass kein Einzelschritt größer als +5 wird. Bei vollem Schwerpunkt gleicht das 5 Sternen; bei mittlerem Schwerpunkt fällt die Verteilung etwas günstiger aus. Moral steigt um 14 statt 12.
2. **Höheres Leistungsdach:** Das bisherige Premium-Merkmal „Index ab 6“ wird zu „Platzqualität mindestens 5“ (+1 auf die Trainingsobergrenze). Haben Platz und Fitness beide 6 Sterne, kommt ein weiterer Punkt hinzu (+2 insgesamt). Spitzenspieler können damit etwas höher trainiert werden, aber nicht über 99.
3. **Regeneration:** Bei 6 Sternen Unterkunft verkürzt sich die Restdauer jeder laufenden Verletzung im Kader einmalig um eine Woche (nicht unter eine Woche). Bei 6 Sternen Verpflegung erhält jeder Spieler zusätzlich bis zu +1 Kondition (höchstens bis zur Obergrenze).

Prüfung in M4 und M7: Ein Spitzenteam, das pro Saison dreimal ein
6-Sterne-Lager bucht, verbessert sich gegenüber drei 5-Sterne-Aufenthalten im
Schnitt höchstens um etwa einen Tabellenplatz. Andernfalls werden die Punkte 2
und 3 abgeschwächt. Das Ergebnis wird hier und in [DIFFERENCES](../DIFFERENCES.md) festgehalten.

## Für die Oberfläche

Die Modern-Session liefert Katalog, Buchungsstand (Aufenthalte dieser Saison,
Buchung diese Woche), aktuelle Schwerpunkte und den Preis für den eigenen Kader.
Aktionen: Schwerpunkte setzen, Lager buchen. Einzelheiten in der
[UI-Schnittstelle](ui-schnittstelle.md#trainingslager).
