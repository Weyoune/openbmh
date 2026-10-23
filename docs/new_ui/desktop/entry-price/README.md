# Eintrittspreis · Konto-/Eintritts-Widget

Stand: 23. Oktober 2026. Im HTML-Mockup und im experimentellen nativen
RmlUI-Desktop umgesetzt. Siehe [nativer Umsetzungsstand](../../NATIVE_STATUS.md).

## Bedienung und Größe

Ein Klick auf den Eintrittspreis im Konto-/Eintritts-Widget öffnet einen
kleinen Popover direkt am Preisfeld: Überschrift, Betrag, waagerechter
Schieberegler und Bestätigung. Die große zentrierte Dialogfläche entfällt.

Der Popover misst in der normalen Ansicht 330 × 196 CSS-Pixel und bleibt bei
720p und Full HD gleich groß; die Schrift ist etwa 14 Pixel, der Betrag 26 Pixel groß. Er liegt
außerhalb des transformierten Desktop-Zeichenraums und erhält keine zusätzliche
Desktop-Skalierung. Seine Position wird aus dem tatsächlich sichtbaren
Preis-Button bestimmt und am Bildschirmrand angepasst. Der Hintergrund bleibt
unabgedunkelt; ein Klick außerhalb verwirft den Entwurf.

Im aktuellen HTML-Datensatz sind 0–25 € in Schritten von 1 € zulässig,
Ausgangswert 13 €. Dies sind die vorhandenen POC-Grenzen, keine neue
Gameplay-Regel für die Modern-Engine. Die native Oberfläche erhält aktuelle
Werte und zulässige Grenzen später vom Datenanbieter beziehungsweise der Session.

## Entwurf und Übernahme

- Öffnen übernimmt den zuletzt akzeptierten Preis in einen lokalen Entwurf.
- Der Schieberegler ändert nur diesen Entwurf. Pfeiltasten ändern
  um 1 €, Home/End wählen die Grenzen.
- „Übernehmen“ zeigt eine Bestätigung mit altem und neuem Betrag. Erst
  „Bestätigen“ speichert den neuen Wert. „Ändern“ führt zum Entwurf zurück.
- Abbrechen, Escape und ein Klick außerhalb verwerfen den Entwurf ohne Schreibzugriff.
  Erneutes Öffnen beginnt beim zuletzt akzeptierten Preis.
- Bei einem Speicherfehler bleibt der akzeptierte Wert erhalten; der Dialog
  zeigt den Fehler und erlaubt einen erneuten Versuch.
- Ein zwischenzeitlicher Preis-, Manager-, Spielstand- oder Karrierewechsel
  verwirft den veralteten Dialog. Nach erfolgreicher Übernahme kehrt der Fokus
  zum neu gerenderten Eintrittspreis-Button zurück.

Die vier Konto-Widgetgrößen und ihre Fixierung beziehungsweise Verschiebbarkeit
bleiben bedienbar. Andere Zahleneingaben verwenden weiterhin den gemeinsamen
Taschenrechner. In der nativen Demo verändern Bestätigungen nur den Demo-Zustand;
Diamond-Inhalte bleiben schreibgeschützt.

## Quelle und Prüfung

Privates Repository `bmh_4k_ui`, Commit
`0bf0f2afd2ff8d5dabb96c795db2a5d0abaaf278`.

Quellen: `desktop/modern-dialogs.js`, `desktop/modern-desktop.js` und
`desktop/modern-desktop.css`. Die Ursache der zuvor winzigen Desktop-Eingabe
war ein CSS-Scope auf `.game-screen`, den der Aufruf unter `#stage` nicht traf.
Der Popover verwendet eigene Bedienelemente in Bildschirmkoordinaten. Sein
Host kehrt beim Schließen oder Wechsel zu einer anderen Dialogart in den
Desktop-Zeichenraum zurück; andere Fenster behalten ihre normale Skalierung.

Vorher-/Nachher-Screenshots mit POC-, Layout- und Dialogzustand liegen im
privaten Repository unter `docs/qa/entry-price-popover-2026-10-23/`.
119 gezielte Prüfungen bestanden in Chromium und WebKit: alle drei Themes bei
720p/Full HD, konstante Schrift-/Popovergröße, Verankerung am Preis-Button,
Ausweichen an Bildschirmrändern mit allen vier Widgetgrößen, Tastatur,
Außenklick/Abbrechen, Größenwechsel, Bestätigung, veraltete Zustände,
Speicherfehler und Rückgabe des gemeinsamen Hosts an andere Desktopdialoge.
Die Silver-Fassung wurde bei beiden Auflösungen samt Bestätigung auch visuell
abgenommen. Keine Browser- oder Ressourcenfehler.
