# Transfers · Modern UI

Stand: 23. Oktober 2026. HTML-POC; die native RmlUI-Umsetzung und die
Modern-Engine-Anbindung erfolgen separat.

## Abstand der Positionsfilter

Die Filter **Alle / Tor / ABW / MIT / STU** sind als eigene Bediengruppe
unterhalb der Angaben „Geforderte Ablöse“ und „Restkapital nach Kauf“ angeordnet.
Seit dieser Anpassung stehen sie 32 logische Pixel tiefer. Bei Full HD entspricht
das 16 Bildschirmpixeln mehr Abstand. Zwischen Ablösebereich und Filterzeile
liegen nun 44 statt 12 logische Pixel.

Die Filter behalten ihre Größe. Für den zusätzlichen Abstand wird freier Raum
aus der entfallenen Fußzeile der früheren Stärkegrafik verwendet. Die numerischen
Stärkewerte und die Kauf-/Leihaktionen bleiben ohne zusätzliche Scrollstrecke
sichtbar. Positionsfilter können weiterhin einzeln umgeschaltet werden.

## Quelle und Prüfung

Privates Repository `bmh_4k_ui`, Commit `bab56691c340574b6068ead1c0858c1c13b5f35a`.
Die Änderung betrifft `desktop/transfer-window.css`.

Vorher-/Nachher-Screenshots und zugehörige POC-Zustände stehen dort unter
`docs/qa/transfer-filter-spacing-2026-10-23/`. 21 gezielte Browserprüfungen
bestanden: Abstände, bedienbare Filter, keine Überschneidungen und vollständige
Darstellung in allen drei Themes sowie bei 1280 × 720, 1920 × 1080 und
3440 × 1440. Die Midnight-Ansicht wurde auch visuell geprüft.
