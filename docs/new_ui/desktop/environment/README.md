# Umfeld

Stand: 24. Oktober 2026. Die native Ansicht übernimmt die Diamond-3.6-
Anordnung des privaten HTML-Golds `bmh_4k_ui`, Revision `9db6926d`:
`desktop/submenus.js`, `submenus.css` und `table-window.css`. Sechs Karten
stehen um Ball und Vereinswappen: drei Nachwuchsstufen, Co-Trainer,
Vereinsarzt und Masseur. Alle drei Glas-Themen behalten diese Anordnung.

Jede Karte bietet eine Nulltaste und **sechs** Sterne. Ein Klick wählt den
Budgetwert unmittelbar; erneut gewählte Werte bleiben bestehen. Gold sieht
hier keinen Ausgabenentwurf oder Bestätigungsdialog vor. Der neue
Präsentationszustand `environment.levels` enthält sechs Ganzzahlen von
0 bis 6; seine Startwerte sind `2,3,4,3,4,2`. Die OBW-Tabelle `staff` bleibt
schreibgeschützt und wird nicht als zweite Budgetquelle verwendet.

Die fiktiven Monatsgrundpreise betragen 2.000/3.000/5.000 für Nachwuchs und
7.500/6.000/4.500 für Personal. Der Kartenbetrag ist Grundpreis mal Sternzahl,
in der internen kleinsten Währungseinheit. Null Sterne ergeben echte
Vorschaukosten von null; sechs Sterne bleiben vollständig verfügbar.
Die Jugendplätze 11./1. Liga, 3./2. Liga und 7./1. Liga sind feste
Demobeispiele. Sie entstehen weder aus Spielen noch aus den Sternen.

Die Auswahl bucht kein Guthaben ab, ändert keine Mitarbeiterverträge und
simuliert weder Nachwuchs noch Training, Zeit oder Zufall. Classic und
Engine bleiben unverändert. Der geladene erste Manager liefert lediglich
das tatsächliche Vereinswappen; es werden keine Mitarbeiternamen erfunden.
Die Bilder gehören zum separaten GUI-Ressourcensatz, nicht zur OBW-Welt;
Sterne sind native SVG-Formen und benötigen kein besonderes Schriftglyph.

Migration ergänzt nur einen fehlenden Umfeldzustand. Vorhandene ungültige
Werte werden atomar abgewiesen. Synthetische Prüfungen stehen in
`tests/modern_environment_ui_tests.cpp`. Gemeinsamer Build und alle 24
Regressionstests sind erfolgreich. Die native Abnahme umfasst alle drei
Themen bei 1280×720 und 1920×1080: sechs Karten und 42 Bedientasten, Budgets
0/6 samt Kosten, Tastatur/Fokus, vollständige Grafiken, Wappen und einen
echten Prozessneustart aus gespeichertem UI-Checkpoint. Die übrigen
Zustandsfelder einschließlich Guthaben bleiben dabei unverändert. Es
besteht kein offener P1/P2-Befund für diese Ansicht.
