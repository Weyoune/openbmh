# Bilanz

Stand: 24. Oktober 2026. HTML-Gestaltungsreferenz: `bmh_4k_ui`, Revision
`ea6c8d97`, `report-menus.js`, `table-window.css` und `poc-menus.css`.
Die native RmlUI zeigt aktuelle Vorschau und Zukunftsplanung in derselben
Glasfensterfamilie. Private Referenzbilder bleiben außerhalb des Repositorys.

Die sieben Einnahmenpositionen, elf Kostenpositionen und angezeigten
Spiel-/Zuschauerkennzahlen sind **fiktive Berichtswerte**. Sie stammen
weder aus gespielten Begegnungen noch aus einem Enginekonto. Die
Währungsdefinition kommt aus der geladenen OBW-Welt; der Kontostand ist
hingegen das gemeinsame `cash` der UI-Demo. Eine Bankaktion wird daher
auch in der Bilanz sichtbar.

Die Zukunftsplanung bearbeitet elf Beträge unter `balance.plan`.
Der gemeinsame Zahleneditor nimmt ganze Anzeigeeinheiten in den vorgesehenen
Tausenderschritten an. Eingabe und Bestätigung bleiben getrennt; erst die
Bestätigung übernimmt einen gültigen, revisionsgebundenen Wert. Änderungen
der Planung buchen kein Geld vom gemeinsamen Guthaben ab und ändern keine
Kosten, Einnahmen oder Engineereignisse. Summen werden aus dem jeweiligen
Vorschau-/Planungsstand gebildet.

Speichern/Laden erhält den Plan gemeinsam mit Bank- und Vertragszustand.
Die Migration ergänzt ausschließlich fehlende Felder und erhält vorhandene
Guthaben. Classic und die Spiel-Engine bleiben unverändert; eine spätere
Anbindung echter Finanzberichte ist eine gesonderte Integrationsentscheidung.
Prüfnachweise: [gemeinsame Menü-QA](../../menu-qa.md).
