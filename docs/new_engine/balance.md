# Balance (M7)

Laufende Notizen zur Balance der Modern-Engine: Befunde, Entscheidungen von Michael, Messungen.

## Spielerwerte des Datensatzes

**Befund (24.10.2026):** Beim Karrierestart würfelt das Original (`startup_players`, CareerFirstSteps2) Kondition, Technik, Form und Eigenschaften **aller** Ligaspieler neu, nach Liga und Tabellenplatz (Stärkebänder `kLeagueStrength`, z. B. Bundesliga 71–93). Die Werte der Datenbank haben im Original deshalb nie gespielt. Im Modern-Datensatz blieben so 0 von 400 Spielern bei ihren Werten.

Die Werte des Datensatzes passen zwischen den Vereinen nicht zur Spielstärke: Wolfsburg (2. Liga) 92, stärker als jeder Bundesligist, Elversberg (Bundesliga) 42, Kaiserslautern (2. Liga) 28. Streuung der Vereinsstärke (Mittel der besten 14) in den deutschen Ligen 14–21 Punkte statt rund 6 im Original. 1:1 übernommen ergab das 3,3 Tore pro Spiel und Tabellen mit 100 bzw. 6 Punkten.

**Entscheidung (Michael): Rangfolge übernehmen.**
- Die Vereine einer Liga nehmen die Plätze im Stärkeband der Liga nach der Rangfolge im Datensatz ein (Mittel von Kondition und Technik der besten 14), statt nach dem Tabellenplatz.
- Innerhalb des Kaders behalten die Spieler ihren Abstand zum Vereinsniveau, gestaucht auf die Streuung erzeugter Spieler (W(Niveau ± 10 %), Standardabweichung ≈ 5,8 % des Niveaus), wo der Kader breiter ist. Form und Eigenschaften bleiben aus dem Datensatz.
- Der Verein des Menschen (Mannschaftsstärke beim Start) und Vereine, die beim Start den Platz tauschen, werden als Ganzes auf ihr Niveau verschoben; die Abstände bleiben.
- Neue Personen (Jugend, Ersatz für Abgänge) erzeugt die Engine wie das Original; ihre Werte liegen damit auf derselben Skala.
- Ohne Modern-Welt (Classic-Daten) ändert sich nichts.

Code: `src/modern/game/career.cpp` (`startup_players`, `set_club_strength`, `place_club`), `keeps_world_values` in `modern_state.hpp`.

**Messung (Modern-Datensatz, Seed 1, zwei Saisons):**

| | Original (gewürfelt) | Datensatz 1:1 | Rangfolge |
|---|---|---|---|
| Tore pro Spiel | 2,67 | 3,27 | 2,77 |
| Vereinsstärke Bundesliga (min–max) | 71–93 | 42–91 | 72–93 |
| Streuung Bundesliga / 2. Liga | ≈ 6 / 6 | 13,7 / 17,4 | 6,3 / 6,4 |
| Bundesliga-Meister / Letzter | – | 79 / 15 Pkt. | 75–77 / 15–18 Pkt. |

Messwerkzeug: `MODERN_SPREAD=1 modern_season_sim <welt.obw> 1 0 1` zeigt die Vereinsstärken je Liga beim Start.

## Offen

- Trainingslager: Saisonwirkung von 6 Sternen messen (Ziel: höchstens etwa +1 Tabellenplatz).
- Ermüdung und Verletzungen bei 38 Spieltagen, Marktwerte und Gehälter der neuen Spieler, Finanzen des menschlichen Vereins (Langläufe über 10 Saisons).
- Spieler in den Pools (vereinslos) aus dem Datensatz behalten ihre Rohwerte; der Datensatz 1.3 hat keine.
- Datenpflege: Die Werte im Datensatz könnten auf eine gemeinsame Skala gebracht werden; die Engine braucht das wegen der Rangfolge nicht.
