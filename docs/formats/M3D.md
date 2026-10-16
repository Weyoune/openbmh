# Master database: `M3D.MAN` / `M3E.MAN`

`load_database` in `src/formats/m3d.cpp` reads an unencrypted sequence of string pools and parallel tables. Counts are supplied by `DbCounts`, not stored in the file. Default counts are 419 clubs, 2,574 players, 168 team records and an auxiliary extent of 3,096. Readers must agree on these counts; changing a count without rebuilding dependent tables does not define a valid new database.

Let `C`, `P`, `T` and `A` denote those counts. The stream order is:

| Size | Content |
|---|---|
| 4 × u16 | Byte lengths of club, player, referee and stadium name pools |
| `A + 50` bytes | Player bonus/flag table and auxiliary entries |
| Sum of pool lengths | Four consecutive NUL-separated name pools |
| `C` × u16 | Club name offsets into pool 0 |
| `P` × u16 | Player name offsets into pool 1 |
| 100 bytes | Auxiliary table |
| `T` × u16 | Auxiliary team table |
| `C` × u16 | Club crest/flag words; low nine bits select a crest |
| 251 + 189 + 50 + 419 + 168 bytes | Five fixed-size auxiliary tables |
| `2 * (A - 4) + 108` bytes | Squad-list words |
| `P` × u32, three times | Three parallel player-word tables |

The loader checks bounds and requires complete consumption. Pool references are byte offsets; they are neither club IDs nor Unicode indices. The name pools also occur in [savegame trailers](FBM.md). A career initialises and reorders parts of the database, so its runtime team IDs can differ from the master club index.

## Player words

A squad-list word stores a twelve-bit player ID and four flag bits. Player ID `0xfff` means no player. The `Player` accessors in `src/formats/m3d.hpp` interpret the three words in file order:

| Word | Bits | Meaning |
|---|---|---|
| 1 | 0–6 | Form |
| 1 | 7–13 | Technique |
| 1 | 14–20 | Condition |
| 2 | 0–13 | Seven two-bit skills: teamplay, speed, shooting, passing, tackling, free kick, corner |
| 2 | 27–31 | Nationality |
| 3 | 0–1 | Position: goalkeeper, defender, midfielder, forward |
| 3 | 21–25 | Age minus 16 |

Other bits carry additional state or remain uninterpreted by these accessors. Preserve them when changing a supported field. Display strength is `min((((form >> 3) + 94) * (technique + condition) / 100) / 2, 99)` with integer division in that order. It is a derived value, not an extra database field.
