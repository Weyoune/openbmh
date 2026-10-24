# Werbung und Vereinsausgaben · native Modern UI

Stand: 24. Oktober 2026. Die native Ansicht übernimmt das private
HTML-Mockup `bmh_4k_ui`, Referenzrevision
`9db6926d7f3d89a7c4219ae38f274766f060783a`. Maßgeblich sind
`desktop/finance-menus.js`, `finance-menus.css`,
`advertising-friendlies-window.css` und `menu-themes.css`.
Native Quellen:
[advertising_screens.inc](../../../../src/modern/ui/advertising_screens.inc),
[advertising_controls.inc](../../../../src/modern/ui/advertising_controls.inc),
[advertising.rcss](../../../../assets/modern/advertising.rcss) und
[demo_advertising.hpp](../../../../src/modern/ui/demo_advertising.hpp).
Private Referenzbilder und Weltdaten werden nicht öffentlich mitgeliefert.

## Aufbau und Bedienung

Oben stehen vierzehn Werbebanden in zwei Reihen mit sieben Spalten. Jede
Bande zeigt „Platz frei“ oder ihren vereinbarten Sponsor sowie ihre
Bandennummer. Darunter folgen die vier Bildkarten Trikotwerbung,
Fanartikel, Öffentlichkeitsarbeit und Fanbetreuung. TV-Rechte liegen
links unten, der Angebots- und Summenbereich rechts daneben. Banden,
Trikot und TV sind als Vertragsplätze auswählbar; die drei übrigen
Bildkarten öffnen die jeweilige Budgeteingabe.

Die Auswahl eines freien Platzes zeigt ein fiktives Angebot mit Sponsor,
Dauer und Monatsbetrag. Zwei Pfeile blättern zyklisch durch drei Angebote
mit zwölf, vierundzwanzig und sechsunddreißig Monaten Laufzeit. Erst
„Annehmen“ speichert die Vereinbarung. Bei einem belegten Platz erscheinen
der laufende Sponsor und Vertrag; Pfeile und Annahme sind gesperrt.
„Schließen“ beziehungsweise „Abbrechen“ hebt nur die lokale Auswahl auf,
eine bestehende Vereinbarung bleibt erhalten.

Escape und Rechtsklick heben zunächst eine offene Vertragsauswahl auf.
Im Budgeteditor schließt Escape zuerst die Zahleneingabe. Die
Schließen-Schaltfläche des Fensters beendet wie in der HTML-Vorlage das
Menü. Die Bedienelemente behalten eindeutige IDs und zugängliche Namen;
Beschriftungen verwenden begrenzte Textbreiten.

Die drei Ausgabenfelder erlauben ganze angezeigte Währungseinheiten von
0 bis 99.999.999. Vorschläge steigen bei Fanartikeln um 50.000 und bei
Öffentlichkeitsarbeit beziehungsweise Fanbetreuung um 5.000. Dies sind
Vorschlagsabstände, keine Beschränkung auf solche Vielfachen. Die
Zahleneingabe enthält acht Rohziffern, MAX und C; sie ist kein
arithmetischer Taschenrechner. „Übernehmen“ führt zur zweiten
Bestätigung. Ändern oder Abbrechen verändert keinen gespeicherten Wert.

Einnahmen sind die Summe der sechzehn Vertragsbeträge, Ausgaben die Summe
der drei Budgets, Saldo deren Differenz. Es handelt sich um fiktive
Monatswerte. Annahme und Budgetänderung buchen keine Ein- oder Auszahlungen
auf das gemeinsame Konto.

## Datenvertrag und Revisionsschutz

Der Provider arbeitet auf dem vollständigen Präsentationszustand und
besitzt ausschließlich dessen oberstes Feld `advertising`:

```text
advertising:
  spending: [Fanartikel, Öffentlichkeitsarbeit, Fanbetreuung]
  contracts: 16 × {sponsor, months, amount}
```

Alle gespeicherten Geldwerte sind Minor Units von `manifest.currency`.
Bei null Nachkommastellen entspricht eine gespeicherte Einheit einer
angezeigten Einheit, bei zwei Nachkommastellen sind es hundert. Der
Budgeteditor übergibt ganze Anzeigeeinheiten; der Provider multipliziert
sie genau einmal mit diesem Faktor. Vorhandene gültige Centwerte werden
beim Laden nicht still gerundet.

Die Plätze 0 bis 13 bezeichnen Banden, 14 Trikotwerbung und 15 TV-Rechte.
Ein freier Vertrag hat `sponsor: -1`, `months: 0`, `amount: 0`. Aktive
Verträge haben einen bekannten Anbieterindex, 12/24/36 Monate und einen
positiven Betrag innerhalb des Geldlimits. Für Banden und Trikot gibt es
zehn fiktive Sponsoren, für TV fünf Sender. Die Auswahlformel folgt der
HTML-Referenz; sie verwendet weder Zufall noch aktuelle Engineangebote.

`migrate_demo_advertising()` ergänzt bei älteren UI-Demoständen das
vollständig fehlende Werbefeld mit freien Plätzen und den Startbudgets
50.000 / 15.000 / 10.000. Ein vorhandenes Werbefeld wird streng validiert:
fehlerhafte Typen, unbekannte Felder, unzulässige Indizes und beschädigte
Verträge führen zur atomaren Ablehnung. Es werden keine teilweise
beschädigten Bestände repariert.

`edit_demo_advertising()` unterstützt zwei Änderungen:

- `{kind: "accept", slot, offer}` berechnet das gewählte Angebot erneut
  und übernimmt es nur in einen weiterhin freien Platz.
- `{kind: "spending", index, value}` bestätigt genau eines der drei
  Budgets in ganzen Anzeigeeinheiten.

Beide Aktionen validieren den gesamten Werbezustand und publizieren erst
dann die vollständige Kopie. `demo_advertising_quote()` und
`demo_advertising_view()` liefern ausschließlich Leseprojektionen.
**OpenBMH deviates from the original:** Auswahl und Angebotsnummer bleiben
flüchtige App-Felder und ersetzen die gespeicherten Navigationsfelder
des HTML-Prototyps.
Eine sichtbare Quote und jeder Budgetentwurf sind an Welt und beobachtete
Source-Revision gebunden. Veraltete Bestätigungen werden nicht auf einen
inzwischen veränderten Stand angewandt.

## Referenzgeometrie und Ressourcen

Die logische Inhaltsfläche beträgt 2612 × 1864. Der Bandenbereich ist
310 hoch, mit 16 Einheiten Abstand zwischen den Knöpfen. Die vier
636,5 × 650 großen Bildkarten beginnen bei y346 und haben 22 Einheiten
Abstand. TV liegt bei `(0,1032,596,638)`, der Detailbereich bei
`(636,1032,1976,828)`. Im Angebot sind Sponsorbild, Pfeile und Fakten in
430 / 84 / verbleibende Breite aufgeteilt. Drei Summenspalten stehen am
unteren Rand. Midnight, Silver und Matchday verwenden dieselbe Anordnung.

Die GUI-Schlüssel `advertising.panel.0` bis `.3` referenzieren die vier
unveränderten Quadranten des vorhandenen Bilderatlas. TV verwendet
`art.advertising-tv-v1.png`. Diese festen GUI-Bilder gehören zum
GUI-Ressourcenpaket, nicht in die OBW-Welt oder deren Assetindex. Es werden
keine neuen Originalbilder in öffentliche Tests oder Dokumente kopiert.

## Prüfstand und Enginegrenze

Die synthetischen Komponententests in
[modern_advertising_ui_tests.cpp](../../../../tests/modern_advertising_ui_tests.cpp)
prüfen beide Währungspräzisionen, die drei Angebotsarten, aktive
Vertragssperren, alle sechzehn Plätze, Betragsgrenzen und große Summen,
Nullwerte, ungültige Eingaben, atomare Migration sowie unveränderte
Konten und Katalogdaten.

Die native Linuxprüfung ist mit Midnight, Silver und Matchday jeweils bei
1920 × 1080 und 1280 × 720 erfolgreich ausgeführt. Defaultansicht,
Angebot und aktiver Vertrag wurden gegen die HTML-Vorlage geprüft;
Bildpositionen, Bandenbeschriftungen und Hinweiszeilen sind abgeglichen.
Der Interaktionslauf prüft Null und MAX, Entwurf/Review/Ändern/Abbruch,
Trikot und TV, aktuelle Angebotsbindung nach einer Budgetänderung sowie
unveränderte Konten. Escape, Rechtsklick und Fensterschließen sind mit
unveränderten Modell- und Revisionswerten geprüft. Ein durch den nativen
`checkpoint`-Befehl geschriebener UI-Demostand wurde in einer zweiten
Appinstanz mit exakt gleichem Modell wieder geladen; die lokale Auswahl
startet dort bei `-1`. Dies dokumentiert Linux-Ergebnisse und keine
Laufzeitprüfung anderer Plattformen.

Es gibt keine Anbindung an Engine, Classic-Initialisierung, Session,
Spielzeit oder Zufallsströme. Vereinbarungen laufen in dieser Demo nicht
monatlich ab und erzeugen keine echten Buchungen. Ein späterer
Sessionadapter muss verbindliche Werbeverträge und Ausgabenregeln liefern.
