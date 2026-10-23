# Transfers · Modern UI

Stand: 23. Oktober 2026. HTML-POC; die native RmlUI-Umsetzung und die
Modern-Engine-Anbindung erfolgen separat.

## Ablösebereich und Positionsfilter

Die Angaben „Geforderte Ablöse“ und „Restkapital nach Kauf“ stehen in einem
gemeinsamen, dezent umrandeten Kasten. Seine Höhe richtet sich nach dem Inhalt,
einschließlich Beschriftungen, Zahlen und Innenabstand. Eine feste Zeilenhöhe
hatte bei größeren Textzeilen zu überstehenden Beträgen geführt.
Auch der Bereich der numerischen Mannschaftsstärken verwendet inhaltsabhängige
Zeilenhöhen, damit die Zahlen innerhalb seiner Fläche bleiben.

Die Filter **Alle / Tor / ABW / MIT / STU** folgen als eigene Bediengruppe mit
44 logischen Pixeln Abstand zum unteren Rand des Ablösekastens. Bei Full HD
entspricht das 22 Bildschirmpixeln. Die Filter sind einzeln umschaltbar.
Stärkewerte und Kauf-/Leihaktionen sind im geprüften Standardzustand ohne
zusätzliche Scrollstrecke vollständig sichtbar.

## Quelle und Prüfung

Privates Repository `bmh_4k_ui`, Commit `cacf8fdd5376110529afbf97c7b977aaee1d7957`.
Die Änderung betrifft `desktop/transfer-window.css`.

Vorher-/Nachher-Screenshots und zugehörige POC-Zustände stehen dort unter
`docs/qa/transfer-panel-containment-2026-10-23/`. 33 gezielte Browserprüfungen
bestanden: Inhalte einschließlich Innenabstand innerhalb der Flächen,
Hover-Vorschauen anderer Spieler, Abstand zur Filterzeile, höhere Textzeilen
(`line-height: 1.5`) und vollständige Darstellung in allen drei Themes sowie bei
1280 × 720, 1920 × 1080 und 3440 × 1440. Die Midnight-Ansicht wurde auch visuell
geprüft.

Die vorherige Anpassung des Filterabstands bleibt unter
`docs/qa/transfer-filter-spacing-2026-10-23/` im privaten POC dokumentiert.
