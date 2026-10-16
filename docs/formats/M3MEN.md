# Desktop layout: `.MEN`

A desktop layout is 864 bytes: 54 entries of 16 bytes, without a header or encryption. `src/ui/desk_state.cpp` interprets the list. New careers initialise manager slots from the installation's layout; saved careers retain their layouts in [FBM](FBM.md).

Entry zero is the list head. Byte 5 contains the first visible entry; each following entry uses byte 5 for its successor and byte 6 for its predecessor. Index zero terminates a chain and `0xff` denotes an unlisted entry. The reader checks indices and detects cycles.

| Offset within entry | Type | Meaning |
|---|---|---|
| 0 | u16 | Horizontal coordinate; bit `0x1000` hides the entry |
| 2 | u8 | Vertical coordinate |
| 3 | u8 | Width |
| 4 | u8 | Height |
| 5 | u8 | Next entry |
| 6 | u8 | Previous entry |
| 7–11 | bytes | Reserved |
| 12 | u8 | Item kind |
| 13 | u8 | Drawer state; bit 7 indicates open |
| 14–15 | bytes | Reserved |

Icon kinds occupy 0–29; drawer kinds occupy 50–72. The graphical client decides which kinds it can display and which actions are available in the current mode. A kind number is not a console window ID. Unrecognised or inactive layout data must not be interpreted as an executable action.
