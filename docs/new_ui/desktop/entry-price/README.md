# Eintrittspreis · Konto-/Eintritts-Widget

Stand: 23. Oktober 2026. Im HTML-Mockup umgesetzt; die native Modern UI
übernimmt diese Bedienung bei ihrer Umsetzung.

## Bedienung und Größe

Ein Klick auf den Eintrittspreis im Konto-/Eintritts-Widget öffnet einen
kompakten Dialog mit gut lesbarem Betrag, waagerechtem Schieberegler und
Minus-/Plus-Schritten. Der Taschenrechner wird dafür nicht mehr verwendet.

Der Dialog misst 1120 × 800 logische Pixel im 3840 × 2160 großen Desktop:
bei Full HD 560 × 400, bei 1280 × 720 etwa 373 × 267 Bildschirmpixel. Er liegt
zentriert im vorhandenen Dialog-Layer und wird genau einmal mit dem Desktop
skaliert. Die Widgetgröße wird durch das Öffnen nicht verändert.

Im aktuellen HTML-Datensatz sind 0–25 € in Schritten von 1 € zulässig,
Ausgangswert 13 €. Dies sind die vorhandenen POC-Grenzen, keine neue
Gameplay-Regel für die Modern-Engine. Die native Oberfläche erhält aktuelle
Werte und zulässige Grenzen später vom Datenanbieter beziehungsweise der Session.

## Entwurf und Übernahme

- Öffnen übernimmt den zuletzt akzeptierten Preis in einen lokalen Entwurf.
- Schieberegler und Minus/Plus ändern nur diesen Entwurf. Pfeiltasten ändern
  um 1 €, Home/End wählen die Grenzen.
- „Übernehmen“ zeigt eine Bestätigung mit altem und neuem Betrag. Erst
  „Bestätigen“ speichert den neuen Wert. „Ändern“ führt zum Entwurf zurück.
- Abbrechen, Schließen und Escape verwerfen den Entwurf ohne Schreibzugriff.
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
`47ac47430cf708afb0bc6d4f45e281d341d6e59a`.

Quellen: `desktop/modern-dialogs.js`, `desktop/modern-desktop.js` und
`desktop/modern-desktop.css`. Die Ursache der zuvor winzigen Desktop-Eingabe
war ein CSS-Scope auf `.game-screen`, den der Aufruf unter `#stage` nicht traf.
Der neue Dialog hat eigene, passend skalierte Bedienelemente.

Vorher-/Nachher-Screenshots mit POC-, Layout- und Dialogzustand liegen im
privaten Repository unter `docs/qa/entry-price-slider-2026-10-23/`.
128 Verhaltenstests bestanden in Chromium und WebKit; nach der anschließenden
reinen Größenanpassung bestanden 85 gezielte Darstellungsprüfungen für die
kompakte Fassung in beiden Browsern, allen drei Themes und bei 720p/Full HD.
Dabei wurden auch Bestätigung und Speicherfehler auf Textüberstände geprüft.
Die kompakte Silver-Fassung wurde zusätzlich bei beiden Auflösungen visuell
abgenommen. Keine Browser- oder Ressourcenfehler.
