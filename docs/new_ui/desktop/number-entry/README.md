# Gemeinsame Zahleneingabe

Die native Bank, Bilanzplanung und Vertragsansicht verwenden dieselbe
RmlUI-Zahleneingabe nach `number-calculator.js/.css` des HTML-Mockups.
Der Eintrittspreis behält seinen bereits abgenommenen kleinen Schieberegler.

Der Dialog enthält ausschließlich Ziffern, Rücktaste, C, MAX und sechs
kontextbezogene Vorschläge. Er rechnet nicht. Geldbeträge werden als ganze
Anzeigeeinheiten eingegeben, Bestände als Stückzahlen. Die folgende
Bestätigungsseite nennt den Betrag; bei Immobilien und Aktien zusätzlich
den Gesamtpreis. „Ändern“ kehrt zum Entwurf zurück. Abbrechen beziehungsweise
Escape auf beiden Seiten verwirft ihn vollständig.

Der Dialog bleibt auf das aufrufende Menü, die Welt und die Revision gebunden.
Ein inzwischen geänderter Stand erfordert eine neue Eingabe. Die
abschließende `finance`-Aktion prüft Bestand und Grenzen erneut und übernimmt
atomar. Bei Gehaltsangeboten bestätigt der Zahleneditor zunächst nur den
lokalen Vertragsentwurf; die eigentliche Übernahme folgt mit „Angebot machen“.

Tastatur: Tab/Shift-Tab bleiben im Dialog; Enter und Nummernblock-Enter
bestätigen, Escape bricht ab. Die numerischen Felder der Bilanz und Verträge
öffnen den Dialog auch durch Zahlentasten, F4, Pfeil hoch/runter oder Einfügen.
Vorschläge und Tasten führen den Fokus zur Eingabe zurück. Nach dem Schließen
kehrt er zum auslösenden Feld zurück.

Die UI-Demo erlaubt maximal acht Ziffern. Bilanzpläne sind in 1.000er-Schritten,
Vertragsgehälter bis 200.000, Prämien bis 255 möglich. Diese Grenzen sind
Präsentationsregeln des Prototyps. Sie sind keine Änderung an der Engine oder
am OBW-Format. Später liefert ein Engineadapter die konkreten zulässigen
Angebote und Aktionen.
