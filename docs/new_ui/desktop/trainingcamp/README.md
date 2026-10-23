# Trainingslager · Modern UI

Stand: 23. Oktober 2026. Bestandsaufnahme des HTML-POC, noch keine native
RmlUI- oder Modern-Engine-Implementierung.

Es gibt **18 Trainingslager**: sechs in Deutschland, neun im übrigen Europa
und drei im Nahen Osten. **Anbieternamen, Preise und Qualitätswerte sind fiktive
POC-Daten**. Die Orte sind reale geografische Bezugspunkte. Es handelt sich
nicht um recherchierte Angebote realer Hotels oder Trainingszentren.

Die vier Qualitätswerte werden getrennt als Sterne angezeigt:
**Platzqualität, Fitnessgeräte, Verpflegung und Unterkunft**. Deutschland und
Europa verwenden eine Skala von 0 bis 5, der Nahe Osten von 0 bis 6 Sternen.
Die Tabelle gibt die tatsächlich hinterlegten Werte wieder; eine gemeinsame
Hotel-Sternebewertung gibt es nicht.

## Deutschland · 6 Trainingslager

| ID | Trainingslager | Ort / Land | Platz | Fitness | Verpflegung | Unterkunft | Preis |
|---:|---|---|---:|---:|---:|---:|---:|
| 3 | Wellness Resort Ulm | Ulm · Deutschland | 2 | 3 | 2 | 2 | 145.000 € |
| 5 | Ball-Arena Potsdam | Potsdam · Deutschland | 3 | 2 | 3 | 2 | 175.000 € |
| 8 | Sportpark Sauerland | Schmallenberg · Deutschland | 1 | 1 | 1 | 1 | 25.000 € |
| 9 | Trainingshof Harz | Goslar · Deutschland | 2 | 2 | 2 | 2 | 55.000 € |
| 10 | Alpencamp Allgäu | Kempten · Deutschland | 3 | 3 | 2 | 3 | 195.000 € |
| 11 | Leistungszentrum Schwarzwald | Freiburg · Deutschland | 5 | 5 | 4 | 5 | 490.000 € |

## Europa · 9 Trainingslager

| ID | Trainingslager | Ort / Land | Platz | Fitness | Verpflegung | Unterkunft | Preis |
|---:|---|---|---:|---:|---:|---:|---:|
| 0 | Kraftmühle Łódź | Łódź · Polen | 4 | 4 | 4 | 4 | 410.000 € |
| 1 | Vitalpark Vinadio | Vinadio · Italien | 4 | 4 | 4 | 3 | 450.000 € |
| 2 | Kraftwerk Stettin | Stettin · Polen | 4 | 4 | 3 | 4 | 420.000 € |
| 4 | Sportzentrum Groningen | Groningen · Niederlande | 4 | 4 | 4 | 4 | 485.000 € |
| 6 | Camp Royal Saint-Denis | Saint-Denis · Frankreich | 5 | 4 | 5 | 4 | 650.000 € |
| 7 | Elite-Fußballcamp Ancona | Ancona · Italien | 5 | 5 | 5 | 5 | 840.000 € |
| 12 | Algarve Performance Camp | Faro · Portugal | 5 | 4 | 5 | 5 | 920.000 € |
| 13 | Costa del Sol Sport Resort | Málaga · Spanien | 5 | 5 | 5 | 4 | 895.000 € |
| 17 | Riviera Trainingscamp Belek | Belek · Türkei | 5 | 5 | 5 | 4 | 790.000 € |

## Naher Osten · 3 Trainingslager

| ID | Trainingslager | Ort / Land | Platz | Fitness | Verpflegung | Unterkunft | Preis |
|---:|---|---|---:|---:|---:|---:|---:|
| 14 | Doha Elite Football Resort | Doha · Katar | 6 | 6 | 6 | 6 | 2.800.000 € |
| 15 | Abu Dhabi Royal Training | Abu Dhabi · VAE | 6 | 6 | 6 | 6 | 3.200.000 € |
| 16 | Dubai Platinum Sports Camp | Dubai · VAE | 6 | 6 | 6 | 6 | 3.600.000 € |

## Bedienung im POC

- Drei Regionsschalter: Deutschland, Europa, Naher Osten. Ein Wechsel wählt
  das erste Lager der Region aus.
- Eine eigene Karte je Region; Länder mit Lagern sind heller. Es gibt keine
  zusätzliche Ortsliste. Orts- und Landesname erscheinen in der Detailansicht.
- Ein Klick auf eine Kartenmarkierung übernimmt die Auswahl. Hover oder
  Tastaturfokus zeigt vorübergehend ein anderes Lager; danach kehren die
  Details zur gespeicherten Auswahl zurück.
- Die Detailansicht enthält ein eigenes Lagermotiv, Namen, Ort, vier
  Qualitätswerte, Preis und Schwerpunktregler. Die Bilder füllen ihren
  abgerundeten Rahmen proportional aus.
- Kondition, Technik und Spiel sind jeweils von 0 bis 15 einstellbar.
  Erholung ist abgeleitet und nicht direkt bedienbar:
  `15 − floor((Kondition + Technik + Spiel) / 3)`.
- „Vormerken“ speichert die Auswahl im POC und zeigt eine Bestätigung.
  Es entstehen keine Abbuchung, Kalenderbuchung oder Trainingswirkung.

Ausgangsauswahl: ID 4, Sportzentrum Groningen. Schwerpunkte: Kondition 5,
Technik 4, Spiel 6; daraus folgt Erholung 10. Anfangs ist kein Lager vorgemerkt.
Die IDs entsprechen der bestehenden Array-Reihenfolge und dürfen bei einer
Migration gespeicherter POC-Ansichten nicht versehentlich neu sortiert werden.

## Grundlage für die Engine-Anbindung

Die UI benötigt eine stabile Lager-ID, Region, geografische Position, Name,
Ort und Land, vier Qualitätswerte, Preis und Bildreferenz. Auswahl und
Vormerkung sind getrennte Zustände; die drei einstellbaren Schwerpunkte
werden separat gespeichert.

Vor einer wirksamen Buchung sind Preisbasis, Dauer, verfügbare Termine,
Stornierung und Trainingswirkung festzulegen. Der POC spezifiziert diese
Spielregeln nicht. Insbesondere darf aus dem angezeigten Preis kein Preis
pro Spieler oder pro Woche abgeleitet werden.

## Quellen und Nachführung

Quelle: privates Repository `bmh_4k_ui`, Commit `19969d430e38eadae7ff7a9d1fb381ab02c395de`.

- `desktop/camp-menu.js`: vollständiger Angebotskatalog, Qualitätsreihenfolge,
  Preise, Bildauswahl, Karteninteraktion und Vormerken.
- `desktop/camp-map-data.js`: Regionsnamen und Kartenprojektion.
- `desktop/submenus.js`: Anfangszustand und Validierung der POC-Daten.
- `docs/qa/camp-training/README.md` und `docs/qa/ui-polish/README.md`:
  ergänzende Bild- und Bedienungsnachweise im privaten POC-Repository.

Die Auflistung wurde direkt mit allen 18 Einträgen des Quellkatalogs
abgeglichen. Bei Änderungen am Angebot ist dieses Dokument zusammen mit
der jeweiligen UI-Anpassung zu aktualisieren.
