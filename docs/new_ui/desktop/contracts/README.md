# Verträge

Stand: 24. Oktober 2026. HTML-Gold: `bmh_4k_ui`, Revision `ea6c8d97`,
`management-menus.js`, `career-menus.css` und `poc-menus.css`.
Die native Ansicht bietet Daten A, Daten B und Gehalt sowie Verhandlung,
Versicherung und sieben Team-Prämien.

Kader, stabile Spieler-IDs, Namen, Positionen und vorhandene Attribute
stammen unverändert aus der OBW-Welt. Zuständig ist der Verein des ersten
Managers, unabhängig von der Berichtsliga. Der Kader wird vollständig
angezeigt und bei Bedarf gescrollt; fehlende Werte bleiben `—`, ein leerer
Kader wird ausdrücklich benannt. Es werden keine Ersatzspieler erfunden.

Ein vorhandenes OBW-Saisongehalt liefert eine gerundete monatliche
**Vorschau**. Ausdrücklich zur Initialisierung vorgesehene Demoverträge
bekommen fiktive Startgehälter; ohne diese Grundlage bleibt das Gehalt
unbekannt. Herkunftshinweise stehen am Gehalt. Laufzeit, Versicherung,
Forderung und Prämien gehören zum Präsentationszustand, nicht zu einer
Engineverhandlung. Verhandlungsbilder sind GUI-Ressourcen, keine Weltassets.

Gehalt und Laufzeit bleiben lokale Vorschläge. Auch die Bestätigung im
Zahleneditor übernimmt zunächst nur diesen Entwurf; erst „Angebot machen“
schreibt ihn in den Demozustand. Abbrechen und der Wechsel zur Versicherung
verwerfen den Vorschlag. Versicherung mit 0/12/24 Monaten wird direkt
übernommen. Die sieben Prämien akzeptieren 0–255 und nutzen die gemeinsame
Geldbestätigung; dabei wird kein Guthaben abgebucht.

Revision, Welt, Verein und Spieleridentität werden vor dem endgültigen
Angebot geprüft. Ein Vereinswechsel des ersten Managers initialisiert
passende Verträge neu, ohne Bankguthaben oder Bilanzplan zurückzusetzen.
Checkpoints erhalten Vorschauverträge; nur fehlende Altfelder migrieren.
Classic und Engine bleiben unverändert. Prüfnachweise stehen in der
[gemeinsamen Menü-QA](../../menu-qa.md).
