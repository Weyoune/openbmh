# Modern UI · Dokumentation für 0.3

Hier pflegen wir den Stand der Modern UI sowie die Grundlage für ihre Übernahme
nach RmlUI. Desktop-Menüs erhalten jeweils ein eigenes Unterverzeichnis.
Die HTML-Vorschau ist ein POC; eine Beschreibung hier bestätigt noch keine
Implementierung in der Spiel-Engine.

| Bereich | Dokument |
|---|---|
| Umsetzung | [Abgestimmter Umfang der nativen UI bis zum Anstoß](PLAN.md) |
| Nativer Stand | [Unterbau, erster Durchlauf und noch offene Übertragungen](NATIVE_STATUS.md) |
| Schrift-QA | [Button-/Dropdown-Ausrichtung und unabhängiger Vergleich](typography-qa.md) |
| Menü-QA | [Wiederholbarer Gesamtkatalog und Meilenstein-Abnahme](menu-qa.md) |
| Ressourcen | [GUI-Assets, Weltbilder und spätere Paketbereinigung](assets.md) |
| Desktop | [Statusleiste, Gestaltung und Fenster](desktop/README.md) |
| Eintrittspreis | [Kompakter Schieberegler im Konto-Widget](desktop/entry-price/README.md) |
| Trainingslager | [Alle 18 Angebote, Qualitätswerte und Bedienung](desktop/trainingcamp/README.md) |
| Training | [Übungen, Kader, Intensität und Demo-Automatik](desktop/training/README.md) |
| Taktik | [Interaktive Diagramme und Einsatzregler](desktop/tactics/README.md) |
| Transfers | [Ablöse-Kasten und Positionsfilter](desktop/transfers/README.md) |

Jeder Bereich nennt die geprüfte Quelle, bestehende Funktionen und offene
Entscheidungen für die Engine-Anbindung. Die Architekturgrundlage bleibt der
[Engine-Split](../ENGINE_SPLIT.md).

Was die Modern-Engine der Oberfläche liefert und ab welcher Phase, steht in der
[UI-Schnittstelle der Modern-Engine](../new_engine/ui-schnittstelle.md); Plan
und Stand der Engine in [new_engine](../new_engine/README.md).
