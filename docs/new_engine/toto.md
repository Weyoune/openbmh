# Toto mit mehreren Spalten (Modern)

Michaels Wunsch: Man kann mehr als eine Spalte spielen (bis zu 6); die Regel soll das Gewinnen nicht zu einfach machen. Regel abgestimmt am 24.10.2026.

## Original (Classic, unverändert)

- Ein Tippschein mit 11 Spielen zwischen etwa gleich starken Vereinen, je Spiel 1 (Heimsieg), 0 (Unentschieden) oder 2 (Auswärtssieg).
- Einsatz 10.000–400.000 €, abgebucht am Ende des Spieltags.
- Feste Quoten auf den Einsatz: 9 Richtige ×10, 10 Richtige ×20, 11 Richtige ×50.

## Modern

1. Bis zu **6 Spalten**; jede Spalte kostet den Einsatz und wird einzeln mit den Original-Quoten ausgewertet.
2. Einsatz **je Spalte** 10.000–400.000 €, alle Spalten zusammen höchstens **400.000 €**.
3. Jede Spalte ab der zweiten kostet **10 % ihres Einsatzes** als Bearbeitungsgebühr.
4. **Keine zwei gleichen Spalten.**
5. Kosten: `Einsatz × Spalten + Einsatz / 10 × (Spalten − 1)` (`pools_cost`). Reicht das Geld nicht, gilt der ganze Tippschein als nicht abgegeben (Meldung wie im Original).

Warum das nicht zu leicht ist: Die Quoten sind fest, also bringt jede Spalte für sich denselben Erwartungswert, egal wie die Spalten zusammenhängen. Mehr Spalten erhöhen nur die Chance auf irgendeinen Gewinn, nicht den Rückfluss; die Gebühr macht das Absichern über mehrere Spalten zusätzlich teurer.

**Messung** (`SIM_TOTO=<Spalten> SIM_CONTRACTS=1 SIM_ADVERTISING=1 modern_season_sim <welt.obw> 100 0 <seed>`, zwei Saisons, 400.000 € je Spieltag, Tipp: der stärkere Verein gewinnt, Zusatzspalten tippen je ein Unentschieden):

| Spalten | Rückfluss (Seeds 1, 2) | Spalten mit 9+ Richtigen je Tippschein |
|---|---|---|
| 1 | 30 % / 71 % | etwa 3–5 % |
| 3 | 30 % / 64 % | etwa 3–4 % je Spalte, 7–10 % je Tippschein |
| 6 | 12 % / 49 % | etwa 8–18 % je Tippschein |

Toto bleibt im Schnitt ein Verlustgeschäft; mit mehr Spalten gewinnt man öfter etwas, aber nicht mehr.

## Umsetzung

- Spalte 1 liegt im Original-Tippschein (`Coupon + Manager × 0x14`), damit alle bestehenden Abläufe unverändert bleiben; die Spalten 2–6 und die letzte Abrechnung (Treffer je Spalte, bezahlt, gewonnen) in `ModernState::pools`, gespeichert in `state/modern.json` (`pools`).
- Abgabe: `submit_pools_columns` (`desk_actions.hpp`), in der Session `dispatch_pools_columns(manager, stake, columns, revision, error)`; Fehler `too_many_columns`, `stake_out_of_range`, `total_stake_too_high`, `duplicate_column`, `toto_coupon_requires_eleven_matches`, `invalid_toto_coupon`. Die alte Einspalten-Aktion (`dispatch_toto`) verwirft frühere Zusatzspalten.
- Abrechnung: `settle_pools_coupon` (`matchday_setup.cpp`).
- Lesen: `{kind: "toto"}` über `ModernSession::read` ([UI-Abfragen](ui-abfragen.md)): `open`, `matches [{home, away}]`, `stake`, `cost`, `columns`, `rules`, `last {paid, won, hits}`.
- Tests: `modern_toto_tests` (Regeln, Kosten); Spielstand-Rundreise im Sim prüft die Spalten mit.
