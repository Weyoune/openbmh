# Native Beschriftungen · Vergleich mit dem HTML-Mockup

Stand: 23. Oktober 2026. Zwei dedizierte Sol-High-Agenten vergleichen Buttons
und Dropdowns unabhängig mit dem privaten HTML-Mockup. Die Prüfung betrifft
die bereits übertragenen nativen Ansichten; sie nimmt noch ausstehende Menüs
nicht vorweg.

## Behobene Abweichungen

Bei Full HD lagen die Fontboxen der acht Plus-/Minus-Buttons 8,4 Pixel über
der Mitte, die Footerbeschriftungen etwa 4,9 Pixel und die Lagerregionen
2,9 Pixel. Dropdowns mit vorgegebener Höhe zeigten denselben Fehler:
3,9 Pixel in Toolbars, 5,9 Pixel im Tabellenfooter. Die horizontalen
Ausrichtungen waren überwiegend bereits korrekt.

- Einfache Buttonbeschriftungen bekommen ein ausdrückliches Text-`span` und
  werden als Flexelement zentriert. Listen, Kontextmenüs und Hauptmenü bleiben
  wie die Referenz linksbündig. Foto-/Themekarten behalten ihren eigenen Aufbau.
- RmlUI benötigt das ausdrückliche Textelement: Flex auf einem Button mit
  anonymem Text lässt dessen Beschriftung in der verwendeten Version verschwinden.
- Dropdownbeschriftung und Pfeil richten sich nach der tatsächlichen Höhe
  des Inhaltsbereichs. Der gemeinsame Helfer arbeitet nach dem RmlUI-Layout
  mit physischen Pixeln und bleibt von Fenstergröße und Pixeldichte unabhängig.
  Popupzeilen übernehmen die Schriftgröße des zugehörigen Auswahlfeldes.
- Die internen `selectvalue`-/`selectarrow`-Elemente sind keine DOM-Kinder.
  Der Helfer berücksichtigt sie ausdrücklich; `QuerySelector` erreicht sie nicht.
- Escape schließt zuerst eine offene Auswahlliste und verwirft deren Entwurf.
  Der übergeordnete Dialog bleibt offen. Pfeiltasten dürfen mehrere Optionen
  durchlaufen; erst beim Schließen wird die Auswahl an die Ansicht übergeben.
  Anschließend kehrt der Fokus zum Auswahlfeld zurück.
- Die Manager-Auswahlfelder entsprechen mit 88 logischen Pixeln Höhe und
  30 Pixeln Schrift der Referenz. Das Stärke-Feld nutzt die volle Spaltenbreite.
- Das fehlende Kalendersymbol für den diagonalen Pfeil wird durch ein eigenes
  SVG ersetzt und hängt nicht mehr von der installierten Schrift ab.

## Prüfungen und Grenzen

Die dedizierte Button-Prüfung umfasst sechs Ansichten in allen drei Themes,
jeweils bei 1280 × 720 und 1920 × 1080: 36 native OpenGL-Aufnahmen vor und
nach der Korrektur. 144 Zielbeschriftungen liegen nach der Korrektur geometrisch
mittig; die 24 Beschriftungen der Einstellungsnavigation wurden zusätzlich
nachgeprüft. Die Dropdown-Prüfung umfasst Managerwahl, Einstellungen und
Tabellen in derselben Größen-/Themematrix, einschließlich langen Ligabezeichnungen,
Escape, mehrfacher Pfeiltasten-Auswahl und Fokus nach der Übernahme.

`modern_control_layout_tests` verwendet den tatsächlichen Button-Helfer und
das Produktions-RCSS mit synthetischen Daten. Der Test misst Text-/Pfeilmitten,
prüft sichtbare Textzeilen, unveränderte Bildkarten und Popup-Schriftgrößen in
drei Themes bei vier Pixeldichten. Er ergänzt den echten Renderer-/HTML-
Vergleich und ersetzt ihn nicht.

Screenshots, DOM-Ausgaben, Messdaten und Wiederholungsabläufe liegen im privaten
Mockup-Repository unter `docs/qa/native-rmlui-2026-10-23`. Die native Abnahme
unter Windows und macOS steht noch aus. Als weiterer Vergleichspunkt bleibt
das Platzieren kleiner Dropdown-Popups an der Glasfenstergrenze offen; RmlUI
berücksichtigt bisher die Bildschirmgrenze.
Die eigene Markierungsspalte für die ausgewählte Popupzeile ist ebenfalls noch
an die HTML-Vorlage anzugleichen.
