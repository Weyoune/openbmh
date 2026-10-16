# Savegame container: `.FBM`

`src/formats/fbm.cpp` reads the outer container. `src/formats/savestate.cpp` splits the decoded state; `src/game/save.cpp` writes the normal save representation. Saving goes through `CareerSession` so the manager, phase and committed state are validated.

## Container layout

| Part | Size | Meaning |
|---|---|---|
| Plain header | Variable | Manager/date information for the load list |
| Signature area | 10 bytes | Eight-byte `KRON_FBM` signature and two following bytes |
| First ID | u32 | Container identifier |
| Version | 6 bytes | NUL-terminated version field, normally `V2.24` for the supported state layout |
| Second ID | u32 | Save identifier |
| Random byte | u8 | Header value |
| Initial key | u8 | Rolling-cipher seed |
| Stream | Remaining bytes except checksum | Encrypted state and name pools |
| Checksum | u8 | Initial key plus all decoded stream bytes, modulo 256 |

The loader searches for the signature after the plain header and checks the fixed fields fit. A bad checksum is represented by `Savegame::checksum_ok`; callers must decide whether to accept the save. Neither the cipher nor this checksum provides security or authenticity.

For each byte, decrypt with `plain = (encrypted - key) XOR key`, or encrypt with `encrypted = (plain XOR key) + key`. Reduce all byte arithmetic modulo 256, then subtract `0x29` from the key before the next byte.

## Decoded stream

The supported layout contains a fixed ordered block table, followed by four name pools. Blocks have no tags or encoded lengths. Their sizes depend on the layout and database counts; `kBlocks` in `src/formats/savestate.cpp` is the authoritative order. Some blocks describe fixed arrays and others hold the contents of legacy dynamically allocated tables. The final optional team-record block is present only when its saved presence field is nonzero.

Each name pool has a u16 byte length followed by that many bytes, in club, player, referee and stadium order. These pools preserve renamed entities. The splitter rejects missing/truncated blocks or pools and requires exact stream consumption. See [STATE](STATE.md) for interpreted records and [M3D](M3D.md) for pool references.

## Edition compatibility

Original Football Limited V2.20 saves use a different state block list despite sharing the signature and rolling cipher. They are not interchangeable with the V2.24 layout. Reading an English installation and importing its original saves are separate features; do not edit a version string to bypass the distinction. OpenBMH's own English careers use the session's supported save representation.

Keep a backup before using a save with another engine version. Unknown bytes retained for compatibility are not safe targets for direct client manipulation.
