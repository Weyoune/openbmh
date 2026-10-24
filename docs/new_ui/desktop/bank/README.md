# Bank

Stand: 24. Oktober 2026. Gestaltungsreferenz ist der private HTML-Prototyp
`bmh_4k_ui`, Revision `ea6c8d97`, insbesondere `finance-menus.js`,
`bank-window.css` und `finance-menus.css`. Die Immobilienkacheln verwenden
das korrigierte native Seitenverhältnis des 2×3-Atlas; dessen vorherige
HTML-Streckung ist im selben Paket berichtigt. Die native RmlUI übernimmt diese
Ansicht; private Bilder und Weltdaten gehören nicht in diese Dokumentation.

Die geladenen OBW-Daten liefern Vereinsidentität und Währungsdefinition.
Bankkonditionen, Immobilien-/Aktienpreise und Tilgungsvorschau sind
**fiktive Präsentationsdaten**. Zinsen entstehen nicht durch das Öffnen der
Ansicht; es werden weder Zeit noch Enginezufall fortgeschrieben. Ohne
laufenden Kredit kennzeichnet die Tilgungsvorschau einen Beispielbetrag.

Einzahlen, Abheben, Kreditaufnahme, Rückzahlung und Vermögenskäufe/-verkäufe
verwenden dieselben Zustandsfelder `cash`, `deposit`, `debt` und `finance`.
Es gibt kein zweites Bankkonto neben dem Desktopguthaben. Betrag beziehungsweise
Stückzahl werden im gemeinsamen Zahleneditor eingegeben und anschließend
bestätigt. Erst diese Bestätigung ruft eine geprüfte Finanzaktion auf;
Abbrechen verwirft die Eingabe. Bestand, Grenzen, Revision und Welt werden
vor der Übernahme erneut geprüft.

Geld liegt intern in der kleinsten Währungseinheit. Der Editor verwendet
ganze Anzeigeeinheiten; die Umrechnung erfolgt genau einmal an der
Präsentationsgrenze. UI-Checkpoints erhalten Guthaben und Vermögensbestand.
Alte Checkpoints erhalten nur fehlende neue Finanzfelder; beschädigte
vorhandene Werte werden abgewiesen.

Dies ist die Modern-UI-Demo, keine Anbindung an Enginebuchungen oder Classic.
Prüfnachweise werden gemeinsam im Bank-/Bilanz-/Vertragspaket geführt;
siehe [Menü-QA](../../menu-qa.md).
