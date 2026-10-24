# Almanach · Datenvertrag der UI-Projektion

Stand: 24. Oktober 2026. Dieser Vertrag beschreibt die native
Präsentationsschnittstelle des [Almanachs](README.md). Er ist kein neues
Engine-Speicherformat. Die gegenwärtige Quelle ist ein unveränderliches
Pseudoarchiv; eine spätere Modern-Sessionprojektion kann denselben
Lesevertrag bedienen.

## Besitz und Lesezugriff

`DataSource::read_almanac(const Json &request) const` liefert eine
JSON-Antwort. Nicht unterstützte Quellen antworten mit
`{"ok":false,"error":"almanac_unavailable"}`. `DemoSource` leitet an
`query_demo_almanac(archive, request, state.date)` weiter.

Das separat gehaltene Demoarchiv hat die Form
`{schema: "openbmh-ui-demo-almanac@1", matches: [...]}`. Es liegt nicht in
`Snapshot.state` oder UI-Demoständen. Abfragen geben eigenständige
Ergebniskopien zurück und verändern weder Archiv noch Revision,
Änderungsmarkierung, Geld oder RNG. Die App hält ihre Navigation nur in
flüchtigen Feldern. Das Laden eines UI-Demostands ändert den
Abfragestichtag, importiert aber kein Enginearchiv. Bestehende `.obs`-
Dateien und F10-Daten werden von diesem Paket weder gelesen noch geändert.

## Requests und Antworten

Requests sind Objekte mit genau den angegebenen Schlüsseln. Datumswerte
sind gültige Gregorianische ISO-Daten `YYYY-MM-DD`; Jahre sind ganze
Zahlen von 1 bis 9999. Boolesche Werte und Fließkommazahlen ersetzen keine
Ganzzahlen. Der Stichtag `as_of` ist kein frei überschreibbarer
Requestparameter: Die Datenquelle bindet ihn an das gespeicherte Datum.

| Request | Erfolgsantwort zusätzlich zu `ok: true` |
|---|---|
| `{kind: "index"}` | `years: [Jahre]`, `match_count`, `first_date`, `last_date` |
| `{kind: "calendar", year: 2025}` | `year`, `days: [{date, count, highlights: [Match-IDs]}]` |
| `{kind: "day", date: "2025-08-03"}` | `date`, `matches: [vollständige Records]` |
| `{kind: "match", id: "demo:almanac:2025:0"}` | `match: vollständiger Record` |

`years` ist aufsteigend und enthält nur Kalenderjahre sichtbarer Records.
`first_date` und `last_date` sind bei leerem Archiv `null`,
`match_count` ist dann `0`. Ein Jahreskalender enthält nur Tage mit
sichtbaren Partien, chronologisch geordnet. `count` zählt alle Partien
des Tages. `highlights` enthält in der Demo die IDs von Partien mit
Verlängerung, Elfmeterschießen oder mindestens vier Spieltoren insgesamt;
es enthält keine zusätzlichen Ergebnisse. Die Tagesabfrage erhält bei
Partien desselben Datums ihre ursprüngliche Archivreihenfolge.

Alle vier Abfragen filtern zuerst auf `record.date <= as_of`. Damit
verraten weder die Jahresauswahl noch Anzahlen, Datumsgrenzen oder direkte
Matchabfragen zukünftige Partien. Ein gültiger leerer Tag oder ein Jahr
ohne sichtbare Daten liefert ein leeres Array. Unbekannte und noch nicht
sichtbare Match-IDs liefern denselben Fehler.

Fehler haben die Form `{ok: false, error: "..."}`:

| Fehler | Bedeutung |
|---|---|
| `almanac_unavailable` | Datenquelle unterstützt diese Projektion nicht. |
| `invalid_almanac_request` | Ungültige Requestform, unbekannte Art, zusätzliche Felder oder ungültiger Wert. |
| `invalid_almanac_cutoff` | Ungültiges Stichtagsdatum. |
| `invalid_demo_almanac` | Das separat gehaltene Demoarchiv verletzt seinen Vertrag. |
| `almanac_match_not_found` | Match-ID unbekannt oder jenseits des Stichtags. |

Diese Fehlercodes beschreiben den nativen Provider. Der private
HTML-Arbeitsstand besitzt eigene Fehlerbezeichnungen; sein normalisierter
Recordaufbau und seine erfolgreichen Leseprojektionen folgen demselben
fachlichen Vertrag.

## Normalisierter Matchrecord

Alle folgenden Hauptfelder sind vorhanden. „Nullable“ bedeutet einen
expliziten JSON-Wert `null`, keinen ersatzweise erfundenen Inhalt.

| Feld | Typ und Bedeutung |
|---|---|
| `id` | Stabile, nicht leere Match-ID. |
| `date` | ISO-Kalendertag des gespielten Matches. |
| `season` | Historische Saisonbeschriftung, etwa `2025/26`; kein Kalenderjahrfilter. |
| `competition` | `{id, name, type}` mit historischen Werten; `type` beispielsweise `league`, `cup` oder `friendly`. |
| `round` | Positive ganze Spieltags- oder Rundennummer. |
| `home`, `away` | Je `{id, name, crest, manager}`; unterschiedliche stabile Vereins-IDs. |
| `result` | Getrennte Ergebnisphasen gemäß folgendem Abschnitt. |
| `referee` | Historischer Name oder `null`. |
| `attendance` | Bekannte ganze Zuschauerzahl einschließlich `0`, sonst `null`. |
| `weather` | `{temperature, rain}`; beide ganzzahlig oder `null`, Regen im Bereich `0..7`. |
| `lineups` | `{home, away}`; jede Seite entweder `null` oder `{starters: [...], bench: [...]}`. |
| `events` | Geordnetes Array der unten definierten Ereignisse. |

`crest` ist ein GUI-/Katalog-Assetverweis oder `null`. Namen und Verweise
werden als historische Snapshotwerte gehalten. Das DTO enthält keine
historischen Bildbytes und belegt deshalb nicht, dass eine später unter
demselben Assetverweis geladene Grafik dem früheren Wappenstand entspricht.
Der aktuelle Bildresolver darf eine Referenz darstellen; daraus folgt
keine zugesicherte historische Bildarchivierung.

`manager` ist entweder `{id, name}` für einen archivierten menschlichen
Manager oder `null` für die in der Demo gesichert computergesteuerte
Seite. Der Renderer zeigt dafür „Computer“. Ein künftiger Adapter darf
einen ungültigen oder unbekannten menschlichen Managerverweis nicht
stillschweigend auf diesen Computerfall abbilden.

Ein Aufstellungsspieler hat `{id, name, position?}`. `position` ist das
einzige optionale Feld dieses Teilobjekts. In der Demo wird sie beim
Archivaufbau aus dem Katalog kopiert, etwa `gk`, `def`, `mid` oder `fwd`.
Die sichtbaren Kürzel TW, ABW, MIT und STU sind reine Darstellung.
Der Renderer ersetzt fehlende historische Angaben nicht durch heutige
Kaderdaten. Die Demo bildet bei ausreichendem Kader genau einen Torwart
und zehn Feldspieler ab; alle übrigen Kaderspieler bleiben auf der Bank.
Ist das nicht möglich, bleibt die betreffende Aufstellung `null`.

Die Laufnummer eines Aufstellungsplatzes ist kein Spieler-ID-Ersatz und
keine historische Rückennummer. Historische Spieleridentitäten bleiben
auch bei gleichnamigen Personen getrennt. Katalogumbenennungen nach dem
Archivaufbau verändern weder Vereins- noch Spieler- oder Managernamen
bereits erzeugter Records.

### Ergebnisphasen

`result` enthält immer folgende Schlüssel:

| Feld | Bedeutung |
|---|---|
| `regulation` | `[Heimtore, Gasttore]` nach regulärer Spielzeit oder `null`, falls unbekannt. |
| `extra` | Ausschließlich zusätzlich in der Verlängerung gefallene Tore; bei nicht gespielter oder unbekannter Phase `null`. |
| `penalties` | Separates Ergebnis des Elfmeterschießens; ohne Elfmeterschießen `null`. |
| `final` | Spielendstand nach 90 beziehungsweise 120 Minuten, ohne Tore des Elfmeterschießens. |
| `extra_played` | Explizit bekanntes boolesches Kennzeichen für gespielte Verlängerung. |
| `penalties_played` | Boolesches Kennzeichen für gespieltes Elfmeterschießen. |

Alle bekannten Ergebnisse sind Paare nicht negativer ganzer Zahlen.
Sind reguläre Zeit und Verlängerung bekannt, gilt pro Seite
`regulation + extra == final`. Beispiel:

```json
{
  "regulation": [1, 1],
  "extra": [1, 1],
  "penalties": [4, 3],
  "final": [2, 2],
  "extra_played": true,
  "penalties_played": true
}
```

`extra: [0, 0]` bezeichnet eine bekannte torlose Verlängerung und ist
etwas anderes als `extra: null`. Die aktuelle Demo erlaubt bei gespielter
Verlängerung unbekannte Teilstände. Bei gesetztem `penalties_played`
verlangt ihr Validator dagegen ein bekanntes, entscheidendes
Elfmeterschießenergebnis. Das Elfmeterschießen wird niemals zum
Spielendstand addiert. Ohne gespielte Verlängerung bleibt `extra` stets
`null`.

Die booleschen Phasenkennzeichen sind in der Demo ausdrücklich bekannt.
Für einen Liveadapter mit unbekannter ET-Teilnahme muss vor der Anbindung
ein ausdrücklicher Unbekanntzustand vereinbart werden. `false` wäre dafür
kein zulässiger Ersatz; aus `decided == 2` einfach `true` abzuleiten wäre
ebenfalls unbelegt.

### Ereignisse

Jedes Ereignis enthält genau
`{kind, minute, side, player, other, detail}`. `side` ist `home` oder
`away`. `player` und `other` sind jeweils `{id, name}` oder `null`;
sie tragen keine nachträglich aus dem Kader ergänzten Werte.

| `kind` | Bedeutung von Spieler und Zusatzangaben |
|---|---|
| `goal` | Torschütze, ggf. Vorlagengeber unter `other`; `detail` bezeichnet die Torart. |
| `miss` | Schütze, ggf. Zuspieler unter `other`; gleiche Detailarten wie beim Tor. |
| `yellow` | Verwarnter Spieler. |
| `second_yellow` | Spieler mit Gelb-Rot. |
| `red` | Spieler mit direkter Roter Karte. |
| `injury` | Verletzter Spieler. |
| `substitution` | `player` kommt ein, `other` geht aus; unbekannte Beteiligte bleiben `null`. |
| `shootout` | Schütze eines einzelnen Versuchs; `detail: 1` getroffen, `detail: 0` verfehlt. |

Bei Tor und Fehlschuss bedeutet `detail: 0` Spiel, `1`
Elfmeter/Freistoß, `2` Nachschuss und `3` Ecke. Die gemeinsame Kategorie
Elfmeter/Freistoß wird nicht willkürlich in zwei Ursprünge aufgeteilt.
Die übrigen Ereignisarten verwenden in der Demo `detail: 0`.

Spielereignisse tragen Minuten ab 1, in der Verlängerung bis 120.
Shootout-Ereignisse verwenden symbolisch Minute 121. Die ursprüngliche
Arrayfolge ist bei gleicher Minute maßgeblich; insbesondere dürfen
Shootoutversuche nicht nach Spieler, Seite oder Trefferstatus umsortiert
werden. Die Anzeige sortiert Minuten stabil und nummeriert alle Versuche
einschließlich Fehlversuchen fortlaufend.

### Null, Nullwert und Validierung

`0` ist ein bekannter Wert, etwa keine Zuschauer, null Grad oder kein
Regen. `null` bedeutet fehlende Information. Leere Aufstellung, fehlender
Spieler und Computersteuerung dürfen nicht über numerische Null oder
den ersten Eintrag einer Tabelle erraten werden. Fehlende Namen und
Metadaten erscheinen neutral; ein fehlender Kader erhält keine erfundene
Startelf.

`valid_demo_almanac()` prüft den vollständigen Demoaufbau, eindeutige
Match-IDs, Ergebnis- und Ereigniskonsistenz, gültige Referenzen innerhalb
bekannter Aufstellungen, genau eine bekannte Torwartposition bei
vollständig bekannten Startpositionen, Wechsel, Platzverweise und die
Vollständigkeit eines entscheidenden Shootouts. Die Demoereignisse sind
vollständig und stimmen mit den Spieltoren überein. Dieser Validator ist
kein Ersatz für einen künftigen Adapter mit ausdrücklich unvollständigen
Engineaufzeichnungen; dort dürfen fehlende Phasen nicht durch erfundene
Ereignisse passend gemacht werden.

## Spätere Modern-Sessionprojektion und F10

Grundlage bleiben
[almanach.md](../../../new_engine/almanach.md) und
[almanac.hpp](../../../../src/modern/game/almanac.hpp). F10 enthält bereits
historische Saison-, Vereins-, Spieler-, Manager- und
Schiedsrichtertabellen, Aufstellungen, Zuschauer, Wetter und acht
Ereignisarten. Die Phasenlücke beim Ergebnis bedeutet nicht, dass F10
insgesamt nur einen Endstand und ein Ereignisprotokoll speichern würde.

Der spätere Adapter gehört an die konstante Leseseite der Modern-Session.
Er soll nur benötigte Saisons bei Bedarf dekodieren und historische
Tabellen in UI-Records auflösen. Renderer und Navigation bleiben
unabhängig von OBS, Binärdekodierung und laufenden Engineobjekten.
Abgeschlossene Archive müssen dafür nicht in jeden GUI-Snapshot kopiert
werden. Dieses Paket implementiert oder verändert diesen Adapter nicht.

Die fachliche Zuordnung für den späteren Adapter ist:

| Engine-/F10-Quelle | Normalisiertes UI-Feld |
|---|---|
| Saisonkopf und ursprünglicher Matchordinal | Stabile `id` und Saisonbeschriftung `season`. |
| `date`, `display_start_year`, `almanac_date()` | ISO-Kalendertag `date`; daraus Kalenderjahr und Cutoff. |
| `type`, `league`, `stage`, `round` sowie verfügbare Wettbewerbsmetadaten | `competition` und `round`; Ligakennung und Pokalstufe sind keine austauschbaren Indizes. |
| Saison-Clubtabelle und `home`/`away` | Historische Vereins-IDs und Namen unter `home`/`away`; Bildreferenzen sind keine historischen F10-Bildbytes. |
| `home_manager`/`away_manager` und Managertabelle | Historische menschliche Manager; gesichert `-1` bezeichnet Computer. |
| `home_goals`/`away_goals` | `result.final`, ohne Shootouttore. |
| `home_shootout`/`away_shootout`, `decided` | Separates `result.penalties` und bekannte Entscheidungsart; keine pauschale ET-Ableitung. |
| Vollständige Torereignisse und gesicherte Phaseninformation | Soweit belegt `result.regulation` und zusätzliche Tore unter `result.extra`. |
| Schiedsrichterreferenz, Zuschauer, Temperatur und Regen | `referee`, `attendance`, `weather`, nach Referenz- und Bekanntheitsprüfung. |
| Aufstellungsvektor und saisonale Spielertabelle | Historische Spieleridentitäten und Namen; `starters`/`bench` nur bei belegbarer Trennung. |
| `AlmanacEvent` und seine historischen Spielerreferenzen | Die acht `events.kind`-Werte, Minute, Seite, Spieler, zweiter Spieler und Detail. |

Die konkrete Gap-Liste für diese spätere Integration lautet:

1. **Ergebnisphasen:** `AlmanacMatch.home_goals/away_goals` enthalten den
   Spielendstand ohne Shootout; separate Shootouttore sind vorhanden.
   Ein eigener 90-Minuten-Stand und ein ausdrückliches ET-Kennzeichen
   fehlen im Fall `decided == 2`. Reguläre Tore und zusätzliche
   Verlängerungstore dürfen nur bei gesichert vollständigen Daten aus
   Torereignissen und Endstand abgeleitet werden. Fehlen solche Daten,
   bleiben Teilstände unbekannt. Aus Shootout oder `decided == 2` allein
   darf niemals pauschal auf eine gespielte Verlängerung geschlossen werden.
2. **Matchidentität:** Ein eigenes stabiles Match-ID-Feld fehlt.
   Saisonkennung plus ursprünglicher Matchordinal kann den stabilen
   Projektionsschlüssel bilden. Eine aktuelle Sortier- oder Zeilennummer
   reicht nicht. Die Demo verwendet entsprechend
   `demo:almanac:<Saisonstartjahr>:<ursprünglicher Ordinal>`.
3. **Historische Namensstände:** Die Tabellen werden pro Saison geführt.
   Vereine und Spieler werden beim ersten Eintrag ihrer stabilen ID
   aufgenommen; Namen werden nicht für jeden einzelnen Matchtag erneut
   eingefroren. Diese historischen Tabellen sind maßgeblich und dürfen
   nicht durch heutige Kadernamen ersetzt werden.
4. **Spielerattribute:** F10-Spieler enthalten ID, Vorname, Nachname und
   Nation, keine archivierte Rückennummer, Position oder Stärke.
   Optionale Demo-Positionen stammen aus dem POC-Aufbau und sind keine
   vorgeblich aus F10 gelesenen Daten.
5. **Startelf und Bank:** Der Enginevektor bedeutet nominell elf Starter,
   anschließend Bank. Der Recorder überspringt jedoch leere Plätze.
   Bei unvollständiger Startelf mit Bank kann die Grenze im Vektor allein
   mehrdeutig sein. Kader von Länder- und Amateurteams fehlen ausdrücklich.
   Die UI darf aus dieser Unsicherheit keine erfundene Elf konstruieren;
   das normalisierte DTO trennt beide Bereiche ausdrücklich.
6. **Ereignisbedeutung:** `Goal.detail == 1` kombiniert Elfmeter/Freistoß;
   `0`, `2`, `3` stehen für Spiel, Nachschuss und Ecke.
   `Substitution.player` bezeichnet Ein, `other` Aus.
   `kAlmanacNone == 0xffff` ist ein fehlender Verweis, kein Spieler an
   Tabellenposition 0.
7. **Shootoutfolge:** Mehrere Versuche teilen typischerweise Minute 121.
   Ihre ursprüngliche Arrayreihenfolge muss erhalten bleiben; jeder
   Treffer und Fehlversuch zählt. Ein gesondertes Zeit- oder Sequenzfeld
   für diese Kicks existiert nicht.
8. **Fehlende Rohwerte:** Manche Raw-Standardwerte unterscheiden eine
   fehlende Angabe nicht von einer gültigen Null, etwa bei Zuschauern,
   Temperatur und Regen. Der Adapter braucht gesichert bekannte Werte
   oder `null`, keine Heuristik „0 bedeutet unbekannt“. Schiedsrichterindex
   0 bezeichnet keinen ausgewählten Schiedsrichter.
9. **Referenzgrenzen:** Die Binärdekodierung prüft nicht jeden Spieler-,
   Manager- oder Schiedsrichterindex. Der Adapter muss Grenzen prüfen und
   unbekannte Referenzen neutral behandeln. Ein ungültiger menschlicher
   Managerindex darf nicht unbemerkt als Computersteuerung erscheinen.
10. **Datumdomäne:** Datum ist aus Tageszähler und `display_start_year`
    über `almanac_date()` zu übertragen. Ein Saisonstartjahr ist kein
    Kalenderjahrfilter. Engine- und Schreibtischdatum müssen deshalb
    ausdrücklich in die Kalenderdatumdomäne der UI übersetzt werden.

Die Wappenfrage bleibt daneben eine gesonderte Darstellungsgrenze:
Assetreferenzen sind vorhanden, historische Bildbytes sind durch den
vorliegenden Vertrag nicht belegt. Weder Namen noch Positions- oder
Bildlücken rechtfertigen einen stillen Zugriff des Renderers auf heutige
Spielwerte oder einen behaupteten vollständigen historischen Bildstand.
